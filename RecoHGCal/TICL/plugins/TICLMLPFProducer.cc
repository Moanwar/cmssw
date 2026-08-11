// TICLMLPFProducer: replaces TICLCandidateProducer's linking+PID+regression chain with a single
// MLPF/HEPTv2 model that classifies each input element (track, GSF track, EM trackster, HAD
// trackster) independently and regresses its own kinematics.
//
// ONNX inference: this producer loads exactly ONE model, so it uses a plain
// edm::GlobalCache<cms::Ort::ONNXRuntime> (same pattern as Run3's RecoParticleFlow/PFProducer
// MLPFProducer.cc), NOT ticl::TICLONNXGlobalCache. TICLONNXGlobalCache::initialize()
// unconditionally requires an "inferenceAlgo" string parameter (its
// getParameter<std::string>("inferenceAlgo") call has no existsAs guard) because it's built for
// TICLCandidateProducer/TracksterLinksProducer's multi-session-by-path, multi-plugin-type
// convention -- that doesn't fit a single fixed model, so reusing it here was a mismatch (this
// producer used to do that; switched away after hitting "MissingParameter: Parameter
// 'inferenceAlgo' not found" at construction).
//
// IMPORTANT: HEPTv2's LSH bucketing (block_size=100, n_hashes=3, num_regions=140) requires the
// element (sequence) dimension to be an exact multiple of block_size (100) -- non-multiples cause
// a reshape/expand failure inside ONNX Runtime (confirmed via "invalid expand shape" on the
// Expand node). This producer therefore PADS the element count up to the next multiple of
// kBlockSize before calling run(), using mask=0 for the padding rows, and only reads back
// predictions for the first nElem (real, unpadded) rows afterward.
//
// IMPORTANT DIFFERENCE from TracksterInferenceByPFN's mini-batch loop: that algorithm's "batch"
// dimension is independent tracksters (no cross-trackster attention), so chunking into
// mini-batches of 64 is safe. MLPF/HEPTv2 is a set-transformer where elements attend to EACH
// OTHER across the whole event -- splitting n_elements into mini-batches would silently cut
// real attention edges. So this producer makes exactly ONE run() call per event, with the
// (padded) element count as the sequence length and batch dimension fixed at 1.
//
// Design (agreed in the integration discussion, see project notes):
//   - Skips GeneralInterpretationAlgo entirely: no track<->trackster linking/merging step.
//   - Still consumes tracksters from TracksterLinksProducer (the model was trained on that
//     collection), NOT the raw per-pattern-recognition-algo tracksters.
//   - One non-"none" element prediction => exactly one TICLCandidate (track-only,
//     trackster-only, or GSF-track-only). No merging of multiple elements into one candidate.
//   - Does NOT call PFMuonAlgo to decide muon-or-not: that decision is the network's own
//     argmax==kMu. PFTICLProducer downstream still calls PFMuonAlgo, but only to refine
//     kinematics of candidates that already have abs(pdgId)==13.
//
// TODO before this is production-ready:
//   - nn_had_binary / had-trackster override: not present in this exported graph at all (only
//     cls_binary/cls_pid/momentum/ispu exist) -- so useHadBinaryOverride_ is currently a no-op.
//   - `ispu` output is unused for candidate building; exists in the graph but not consumed here.
//   - Candidate construction (Ptr plumbing, TICLCandidate assembly) still pending -- this file
//     focuses on getting inference itself onto the right API; see produce()'s final TODO block.

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <sstream>
#include <vector>

#include "FWCore/Framework/interface/stream/EDProducer.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/Frameworkfwd.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/AbstractServices/interface/ResourceInformation.h"
#include "FWCore/ServiceRegistry/interface/Service.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"

#include "DataFormats/Common/interface/Handle.h"
#include "DataFormats/TrackReco/interface/Track.h"
#include "DataFormats/GsfTrackReco/interface/GsfTrack.h"
#include "DataFormats/MuonReco/interface/Muon.h"
#include "DataFormats/HGCalReco/interface/Trackster.h"
#include "DataFormats/HGCalReco/interface/TICLCandidate.h"

#include "PhysicsTools/ONNXRuntime/interface/ONNXRuntime.h"
#include "RecoParticleFlow/PFProducer/interface/PFMuonAlgo.h"

#include "RecoHGCal/TICL/interface/MLPFModel.h"
#include "RecoHGCal/TICL/interface/MLPFElementFeatures.h"

using namespace cms::Ort;

class TICLMLPFProducer : public edm::stream::EDProducer<edm::GlobalCache<ONNXRuntime>> {
public:
  explicit TICLMLPFProducer(const edm::ParameterSet&, const ONNXRuntime*);
  ~TICLMLPFProducer() override = default;

  void produce(edm::Event&, const edm::EventSetup&) override;
  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions);

  static std::unique_ptr<ONNXRuntime> initializeGlobalCache(const edm::ParameterSet& iConfig) {
    edm::Service<edm::ResourceInformation> ri;
    Backend backend = Backend::cpu;
    if (ri.isAvailable() && ri->hasGpuNvidia()) {
      backend = Backend::cuda;
      edm::LogInfo("TICLMLPFProducer") << "NVIDIA GPU detected, running MLPF ONNX model on CUDA.";
    } else {
      edm::LogInfo("TICLMLPFProducer") << "No NVIDIA GPU detected, running MLPF ONNX model on CPU.";
    }
    auto sessionOptions = ONNXRuntime::defaultSessionOptions(backend);
    return std::make_unique<ONNXRuntime>(iConfig.getParameter<edm::FileInPath>("modelPath").fullPath(),
                                          &sessionOptions);
  }
  static void globalEndJob(const ONNXRuntime*) {}

private:
  struct ElementSource {
    int type = 0;            // ticl::mlpf::ElementType
    unsigned int index = 0;  // index into the corresponding source collection
  };

  // Order: GSF tracks, tracks, EM tracksters, HAD tracksters (matches training-side
  // prepare_normalized_table() ordering convention).
  void buildInputs(const std::vector<reco::GsfTrack>& gsfTracks,
                    const std::vector<reco::Track>& tracks,
                    const std::vector<reco::MuonRef>& trackMuonRefs,
                    const std::vector<ticl::Trackster>& emTracksters,
                    const std::vector<ticl::Trackster>& hadTracksters,
                    std::vector<ElementSource>& elementSources,
                    std::vector<float>& flatFeatures,
                    std::vector<float>& mask) const;

  const edm::EDGetTokenT<std::vector<reco::Track>> tracksToken_;
  const edm::EDGetTokenT<std::vector<reco::GsfTrack>> gsfTracksToken_;
  const edm::EDGetTokenT<std::vector<ticl::Trackster>> emTrackstersToken_;
  const edm::EDGetTokenT<std::vector<ticl::Trackster>> hadTrackstersToken_;
  const edm::EDGetTokenT<std::vector<reco::Muon>> muonsToken_;

  const bool useHadBinaryOverride_;
  const std::vector<std::string> onnxInputNames_;   // {"X", "mask"}
  const std::vector<std::string> onnxOutputNames_;  // {"cls_binary", "cls_pid", "momentum", "ispu"}
};

TICLMLPFProducer::TICLMLPFProducer(const edm::ParameterSet& ps, const ONNXRuntime* /*cache*/)
    : tracksToken_(consumes<std::vector<reco::Track>>(ps.getParameter<edm::InputTag>("tracks"))),
      gsfTracksToken_(consumes<std::vector<reco::GsfTrack>>(ps.getParameter<edm::InputTag>("gsfTracks"))),
      emTrackstersToken_(consumes<std::vector<ticl::Trackster>>(ps.getParameter<edm::InputTag>("emTracksters"))),
      hadTrackstersToken_(consumes<std::vector<ticl::Trackster>>(ps.getParameter<edm::InputTag>("hadTracksters"))),
      muonsToken_(consumes<std::vector<reco::Muon>>(ps.getParameter<edm::InputTag>("muons"))),
      useHadBinaryOverride_(ps.getParameter<bool>("useHadBinaryOverride")),
      onnxInputNames_(ps.getParameter<std::vector<std::string>>("onnxInputNames")),
      onnxOutputNames_(ps.getParameter<std::vector<std::string>>("onnxOutputNames")) {
  produces<std::vector<TICLCandidate>>();
}

void TICLMLPFProducer::buildInputs(const std::vector<reco::GsfTrack>& gsfTracks,
                                    const std::vector<reco::Track>& tracks,
                                    const std::vector<reco::MuonRef>& trackMuonRefs,
                                    const std::vector<ticl::Trackster>& emTracksters,
                                    const std::vector<ticl::Trackster>& hadTracksters,
                                    std::vector<ElementSource>& elementSources,
                                    std::vector<float>& flatFeatures,
                                    std::vector<float>& mask) const {
  using namespace ticl::mlpf;

  const unsigned int n = gsfTracks.size() + tracks.size() + emTracksters.size() + hadTracksters.size();
  elementSources.reserve(n);
  flatFeatures.assign(static_cast<size_t>(n) * NUM_ELEMENT_FEATURES, 0.f);
  mask.assign(n, 1.f);

  unsigned int ielem = 0;
  auto appendFeatures = [&](const ElementFeatures& f) {
    std::copy(f.values.begin(), f.values.end(), flatFeatures.begin() + ielem * NUM_ELEMENT_FEATURES);
    ++ielem;
  };

  for (unsigned int i = 0; i < gsfTracks.size(); ++i) {
    elementSources.push_back({kGsfTrack, i});
    appendFeatures(buildGsfTrackFeatures(gsfTracks[i]));
  }

  for (unsigned int i = 0; i < tracks.size(); ++i) {
    elementSources.push_back({kTrack, i});
    const reco::Muon* muon =
        (i < trackMuonRefs.size() && trackMuonRefs[i].isNonnull()) ? trackMuonRefs[i].get() : nullptr;
    // hasGsfMatch left false -- TODO wire up a real track<->GSF association once the GSF
    // collection's provenance/matching convention is settled.
    appendFeatures(buildTrackFeatures(tracks[i], muon, /*hasGsfMatch=*/false));
  }

  for (unsigned int i = 0; i < emTracksters.size(); ++i) {
    elementSources.push_back({kEmTrackster, i});
    const auto neighbors = computeTracksterNeighborFeatures(
        emTracksters[i].barycenter().eta(), emTracksters[i].barycenter().phi(), tracks);
    appendFeatures(buildTracksterFeatures(emTracksters[i], kEmTrackster, neighbors));
  }

  for (unsigned int i = 0; i < hadTracksters.size(); ++i) {
    elementSources.push_back({kHadTrackster, i});
    const auto neighbors = computeTracksterNeighborFeatures(
        hadTracksters[i].barycenter().eta(), hadTracksters[i].barycenter().phi(), tracks);
    appendFeatures(buildTracksterFeatures(hadTracksters[i], kHadTrackster, neighbors));
  }
}

void TICLMLPFProducer::produce(edm::Event& evt, const edm::EventSetup&) {
  using namespace ticl::mlpf;

  edm::Handle<std::vector<reco::Track>> tracksHandle;
  evt.getByToken(tracksToken_, tracksHandle);
  const auto& tracks = *tracksHandle;

  const auto& gsfTracks = evt.get(gsfTracksToken_);
  const auto& emTracksters = evt.get(emTrackstersToken_);
  const auto& hadTracksters = evt.get(hadTrackstersToken_);

  edm::Handle<std::vector<reco::Muon>> muonsHandle;
  evt.getByToken(muonsToken_, muonsHandle);

  // Per-track muon association used purely as an input FEATURE (track_muon_type etc.), matching
  // what the training-side dumper baked into the 35-feature vector. This does NOT decide
  // muon-or-not -- that's the network's own argmax==kMu, applied further down.
  std::vector<reco::MuonRef> trackMuonRefs(tracks.size());
  for (unsigned int i = 0; i < tracks.size(); ++i) {
    reco::TrackRef trackRef(tracksHandle, i);
    const int muId = PFMuonAlgo::muAssocToTrack(trackRef, *muonsHandle);
    if (muId != -1) {
      trackMuonRefs[i] = reco::MuonRef(muonsHandle, muId);
    }
  }

  std::vector<ElementSource> elementSources;
  std::vector<float> flatFeatures;
  std::vector<float> maskVec;
  buildInputs(gsfTracks, tracks, trackMuonRefs, emTracksters, hadTracksters, elementSources, flatFeatures, maskVec);

  const int nElem = static_cast<int>(elementSources.size());
  auto resultCandidates = std::make_unique<std::vector<TICLCandidate>>();

  if (nElem == 0) {
    evt.put(std::move(resultCandidates));
    return;
  }

  // HEPTv2's LSH bucketing requires the element/sequence dimension to be an exact multiple of
  // kBlockSize (confirmed empirically: non-multiples crash ONNX Runtime with "invalid expand
  // shape" on an Expand node inside the bucketing code). Pad up to the next multiple, using
  // mask=0 for the padding rows; predictions are only read back for the first nElem real rows.
  constexpr int kBlockSize = 100;
  const int nPadded = ((nElem + kBlockSize - 1) / kBlockSize) * kBlockSize;
  if (nPadded > nElem) {
    flatFeatures.resize(static_cast<size_t>(nPadded) * NUM_ELEMENT_FEATURES, 0.f);
    maskVec.resize(static_cast<size_t>(nPadded), 0.f);  // padding rows get mask=0
  }

  // Single inference call for the whole event -- see file-header note on why this must NOT be
  // mini-batched the way TracksterInferenceByPFN batches independent tracksters.
  std::vector<std::vector<float>> inputs;
  inputs.push_back(flatFeatures);  // kept (not moved) -- decodeMomentum() below needs the
                                    // original input pt/energy per element after inference.
  inputs.push_back(maskVec);

  const auto outputs = globalCache()->run(
      onnxInputNames_,
      inputs,
      {{1, static_cast<int64_t>(nPadded), static_cast<int64_t>(NUM_ELEMENT_FEATURES)},
       {1, static_cast<int64_t>(nPadded)}});

  if (outputs.size() < 3) {
    throw cms::Exception("TICLMLPFProducer")
        << "Expected at least 3 ONNX outputs (cls_binary, cls_pid, momentum), got " << outputs.size();
  }
  const auto& outBinary = outputs[0];
  const auto& outPid = outputs[1];
  const auto& outMomentum = outputs[2];
  // outputs[3], if present, is "ispu" -- unused here.
  // NOTE: outBinary/outPid/outMomentum are sized for nPadded elements; all loops below
  // deliberately iterate ielem < nElem only, so padding rows are never read.

  std::cout << "nElem=" << nElem << " numOutputs=" << outputs.size() << std::endl;
  static const char* kClassNames[6] = {"none", "ch.had", "n.had", "gamma", "ele", "mu"};
  for (int ielem = 0; ielem < std::min(nElem, 20); ++ielem) {  // cap to first 20 elements
    const size_t base2 = static_cast<size_t>(ielem) * NUM_BINARY_CLASSES;
    const size_t base6 = static_cast<size_t>(ielem) * NUM_PID_CLASSES;
    const size_t base5 = static_cast<size_t>(ielem) * NUM_MOMENTUM_FEATURES;

    // binary head as a softmax probability of "particle"
    const float bNo = outBinary[base2 + 0];
    const float bYes = outBinary[base2 + 1];
    const float pParticle = 1.f / (1.f + std::exp(bNo - bYes));

    // pid head: softmax over the 6 classes, plus argmax
    float pidExp[6];
    float pidSum = 0.f;
    float pidMax = outPid[base6];
    for (unsigned int c = 1; c < NUM_PID_CLASSES; ++c) {
      pidMax = std::max(pidMax, outPid[base6 + c]);
    }
    for (unsigned int c = 0; c < NUM_PID_CLASSES; ++c) {
      pidExp[c] = std::exp(outPid[base6 + c] - pidMax);
      pidSum += pidExp[c];
    }
    const int predictedClass = argmaxPidClass(&outPid[base6]);

    const float inputPt = flatFeatures[static_cast<size_t>(ielem) * NUM_ELEMENT_FEATURES + kPt];
    const float inputEnergy = flatFeatures[static_cast<size_t>(ielem) * NUM_ELEMENT_FEATURES + kEnergy];
    const auto decoded = decodeMomentum(&outMomentum[base5], inputPt, inputEnergy);

    std::cout << "ielem=" << ielem
               << " typ=" << flatFeatures[static_cast<size_t>(ielem) * NUM_ELEMENT_FEATURES + kTypIdx]
               << " pt_in=" << inputPt << " E_in=" << inputEnergy << " | p(particle)=" << pParticle
               << " | pid_softmax=[";
    for (unsigned int c = 0; c < NUM_PID_CLASSES; ++c) {
      std::cout << kClassNames[c] << ":" << (pidExp[c] / pidSum) << (c + 1 < NUM_PID_CLASSES ? "," : "");
    }
    std::cout << "] argmax=" << kClassNames[predictedClass] << " | decoded pt=" << decoded.pt
               << " eta=" << decoded.eta << " sin_phi=" << decoded.sin_phi << " cos_phi=" << decoded.cos_phi
               << " energy=" << decoded.energy << std::endl;
  }

  for (int ielem = 0; ielem < nElem; ++ielem) {
    const float logitNoParticle = outBinary[static_cast<size_t>(ielem) * NUM_BINARY_CLASSES + 0];
    const float logitParticle = outBinary[static_cast<size_t>(ielem) * NUM_BINARY_CLASSES + 1];
    if (logitParticle <= logitNoParticle)
      continue;  // binary gate says "none" -- no candidate for this element

    const float* pidScores = &outPid[static_cast<size_t>(ielem) * NUM_PID_CLASSES];
    const int predictedClass = argmaxPidClass(pidScores);

    // useHadBinaryOverride_ is currently a no-op: this exported graph has no had-binary output
    // to override with (see file-header TODO). Left here as the hook point for when it exists.
    (void)useHadBinaryOverride_;

    if (predictedClass == kNone)
      continue;

    const auto& src = elementSources[ielem];
    const float inputPt = flatFeatures[static_cast<size_t>(ielem) * NUM_ELEMENT_FEATURES + kPt];
    const float inputEnergy = flatFeatures[static_cast<size_t>(ielem) * NUM_ELEMENT_FEATURES + kEnergy];
    const float* rawMomentum = &outMomentum[static_cast<size_t>(ielem) * NUM_MOMENTUM_FEATURES];
    const auto decoded = decodeMomentum(rawMomentum, inputPt, inputEnergy);

    int charge = 0;
    if (src.type == kTrack) {
      charge = tracks[src.index].charge();
    } else if (src.type == kGsfTrack) {
      charge = gsfTracks[src.index].charge();
    }
    const int pdgId = pdgIdFromClass(predictedClass, charge);
    (void)pdgId;
    (void)decoded;

    // TODO: construct the actual TICLCandidate here (default ctor + addTrackPtr / addTrackster /
    // addGsfTrackPtr depending on src.type, then setPdgId/setCharge/setP4/setRawEnergy from
    // `decoded` and `pdgId` above) -- needs edm::Ptr into the ORIGINAL collections, i.e.
    // switching gsfTracks/emTracksters/hadTracksters above from evt.get(...) to
    // edm::Handle-based access the same way tracksHandle already is, so OrphanHandles/Ptrs are
    // available here. Left as the next concrete step.
  }

  evt.put(std::move(resultCandidates));
}

void TICLMLPFProducer::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
  edm::ParameterSetDescription desc;
  desc.add<edm::InputTag>("tracks", edm::InputTag("generalTracks"));
  desc.add<edm::InputTag>("gsfTracks", edm::InputTag("electronGsfTracks"));  // TODO: confirm real label
  desc.add<edm::InputTag>("emTracksters", edm::InputTag("ticlTracksterLinksSuperclusteringDNN"));
  desc.add<edm::InputTag>("hadTracksters", edm::InputTag("ticlTracksterLinks"));
  desc.add<edm::InputTag>("muons", edm::InputTag("muons1stStep"));
  desc.add<edm::FileInPath>("modelPath", edm::FileInPath("RecoHGCal/TICL/data/mlpf/mlpf_hgcal.onnx"));
  desc.add<bool>("useHadBinaryOverride", true);
  desc.add<std::vector<std::string>>("onnxInputNames", {"X", "mask"});
  desc.add<std::vector<std::string>>("onnxOutputNames", {"cls_binary", "cls_pid", "momentum", "ispu"});
  descriptions.add("mlpfProducer", desc);
}

DEFINE_FWK_MODULE(TICLMLPFProducer);
