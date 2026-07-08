// MLPFProducer: replaces TICLCandidateProducer's linking+PID+regression chain with a single
// MLPF/HEPTv2 model that classifies each input element (track, GSF track, EM trackster, HAD
// trackster) independently and regresses its own kinematics.
//
// ONNX inference follows the same pattern as TracksterInferenceByPFN.cc / TICLCandidateProducer.cc:
// a single ticl::TICLONNXGlobalCache resolves sessions by model-path string, and inference goes
// through ONNXRuntime::runInto(...) with a reusable OrtScratch buffer.
//
// IMPORTANT DIFFERENCE from TracksterInferenceByPFN's mini-batch loop: that algorithm's "batch"
// dimension is independent tracksters (no cross-trackster attention), so chunking into
// mini-batches of 64 is safe. MLPF/HEPTv2 is a set-transformer where elements attend to EACH
// OTHER across the whole event -- splitting n_elements into mini-batches would silently cut
// real attention edges. So this producer makes exactly ONE runInto() call per event, with the
// full element count as the sequence length and batch dimension fixed at 1.
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
//   - mask dtype: the exported graph declares "mask" as BOOL. This code currently builds a
//     float 0/1 mask (matching OrtScratch's float-only buffers as seen in TracksterInferenceByPFN)
//     and relies on ONNXRuntime accepting/casting it. If the session enforces strict BOOL typing,
//     this will fail at runInto() -- the safe fix is to re-export the graph with `mask` declared
//     as float32 and an internal Cast-to-bool node, rather than inventing new bool-tensor plumbing
//     in ONNXRuntime here. VERIFY THIS FIRST with a standalone runInto() smoke test.
//   - nn_had_binary / had-trackster override: not present in this exported graph at all (only
//     cls_binary/cls_pid/momentum/ispu exist) -- so useHadBinaryOverride_ is currently a no-op.
//     Revisit once/if a had-binary head is added to a future export.
//   - `ispu` output is unused for candidate building; exists in the graph but not consumed here.
//   - Candidate construction (Ptr plumbing, TICLCandidate assembly) still pending -- this file
//     focuses on getting inference itself onto the right API; see produce()'s final TODO block.

#include <memory>
#include <vector>

#include "FWCore/Framework/interface/stream/EDProducer.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/Frameworkfwd.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"

#include "DataFormats/Common/interface/Handle.h"
#include "DataFormats/TrackReco/interface/Track.h"
#include "DataFormats/GsfTrackReco/interface/GsfTrack.h"
#include "DataFormats/MuonReco/interface/Muon.h"
#include "DataFormats/HGCalReco/interface/Trackster.h"
#include "DataFormats/HGCalReco/interface/TICLCandidate.h"

#include "RecoHGCal/TICL/interface/TICLONNXGlobalCache.h"
#include "RecoParticleFlow/PFProducer/interface/PFMuonAlgo.h"

#include "RecoHGCal/TICL/interface/MLPFModel.h"
#include "RecoHGCal/TICL/interface/MLPFElementFeatures.h"

class MLPFProducer : public edm::stream::EDProducer<edm::GlobalCache<ticl::TICLONNXGlobalCache>> {
public:
  explicit MLPFProducer(const edm::ParameterSet&, const ticl::TICLONNXGlobalCache*);
  ~MLPFProducer() override = default;

  void produce(edm::Event&, const edm::EventSetup&) override;
  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions);

  static std::unique_ptr<ticl::TICLONNXGlobalCache> initializeGlobalCache(const edm::ParameterSet& iConfig) {
    return ticl::TICLONNXGlobalCache::initialize(iConfig);
  }
  static void globalEndJob(const ticl::TICLONNXGlobalCache*) {}

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

  const std::string modelPath_;
  const bool useHadBinaryOverride_;
  const std::vector<std::string> onnxInputNames_;   // {"X", "mask"}
  const std::vector<std::string> onnxOutputNames_;  // {"cls_binary", "cls_pid", "momentum", "ispu"}

  // Resolved once in the constructor from the global cache; valid for the whole job.
  const cms::Ort::ONNXRuntime* onnxSession_ = nullptr;
};

MLPFProducer::MLPFProducer(const edm::ParameterSet& ps, const ticl::TICLONNXGlobalCache* cache)
    : tracksToken_(consumes<std::vector<reco::Track>>(ps.getParameter<edm::InputTag>("tracks"))),
      gsfTracksToken_(consumes<std::vector<reco::GsfTrack>>(ps.getParameter<edm::InputTag>("gsfTracks"))),
      emTrackstersToken_(consumes<std::vector<ticl::Trackster>>(ps.getParameter<edm::InputTag>("emTracksters"))),
      hadTrackstersToken_(consumes<std::vector<ticl::Trackster>>(ps.getParameter<edm::InputTag>("hadTracksters"))),
      muonsToken_(consumes<std::vector<reco::Muon>>(ps.getParameter<edm::InputTag>("muons"))),
      modelPath_(ps.getParameter<edm::FileInPath>("modelPath").fullPath()),
      useHadBinaryOverride_(ps.getParameter<bool>("useHadBinaryOverride")),
      onnxInputNames_(ps.getParameter<std::vector<std::string>>("onnxInputNames")),
      onnxOutputNames_(ps.getParameter<std::vector<std::string>>("onnxOutputNames")) {
  if (cache != nullptr) {
    onnxSession_ = cache->getByModelPathString(modelPath_);
  }
  if (onnxSession_ == nullptr) {
    throw cms::Exception("MLPFProducer") << "Could not resolve ONNX session for model path: " << modelPath_;
  }
  produces<std::vector<TICLCandidate>>();
}

void MLPFProducer::buildInputs(const std::vector<reco::GsfTrack>& gsfTracks,
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

void MLPFProducer::produce(edm::Event& evt, const edm::EventSetup&) {
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

  // Single runInto() call for the whole event -- see file-header note on why this must NOT be
  // mini-batched the way TracksterInferenceByPFN batches independent tracksters.
  std::vector<std::vector<float>> inputs(2);
  inputs[0] = flatFeatures;  // kept (not moved) -- decodeMomentum() below needs the original
                             // input pt/energy per element after inference runs.
  inputs[1] = maskVec;       // TODO: see mask-dtype note at top of file.

  std::vector<std::vector<int64_t>> inputShapes(2);
  inputShapes[0] = {1, nElem, static_cast<int64_t>(NUM_ELEMENT_FEATURES)};
  inputShapes[1] = {1, nElem};

  std::vector<std::vector<float>> outputs;
  onnxSession_->runInto(onnxInputNames_, inputs, inputShapes, onnxOutputNames_, outputs, {}, /*batchSize=*/1);

  if (outputs.size() < 3) {
    throw cms::Exception("MLPFProducer") << "Expected at least 3 ONNX outputs (cls_binary, cls_pid, momentum), got "
                                          << outputs.size();
  }
  const auto& outBinary = outputs[0];
  const auto& outPid = outputs[1];
  const auto& outMomentum = outputs[2];
  // outputs[3], if present, is "ispu" -- unused here.

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

void MLPFProducer::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
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

DEFINE_FWK_MODULE(MLPFProducer);
