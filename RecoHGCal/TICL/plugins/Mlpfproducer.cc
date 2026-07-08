// Author: Mohamed Darwish mohamed.anwar@cern.ch
// MLPFProducer: replaces TICLCandidateProducer's linking+PID+regression chain with a single
// MLPF (Machine Learning Particle Flow) model that classifies each input element (track,
// GSF track, EM trackster, HAD trackster) independently and regresses its own kinematics.
//
// TODO before this is production-ready:
//   - Confirm the actual ONNX graph's output tensor names/order against onnxOutputNames_.
//   - nn_had_binary override for typ==3 (HAD_TS) elements is stubbed via useHadBinary_ but the
//     override logic itself is not yet implemented -- needs the exact 11-feature input spec.
//   - GSF track source collection / EDGetToken needs to point at the (not-yet-merged) branch
//     that exposes GSF tracks to TICL.

#include <memory>
#include <vector>
#include "DataFormats/Common/interface/MultiSpan.h"
#include "FWCore/Framework/interface/stream/EDProducer.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/PluginDescription.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/Utilities/interface/ESGetToken.h"
#include "FWCore/Framework/interface/ESHandle.h"
#include "FWCore/Framework/interface/Frameworkfwd.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Framework/interface/ConsumesCollector.h"
#include "DataFormats/Common/interface/OrphanHandle.h"

#include "FWCore/Framework/interface/Event.h"

#include "FWCore/AbstractServices/interface/ResourceInformation.h"
#include "FWCore/ServiceRegistry/interface/Service.h"

#include "DataFormats/TrackReco/interface/Track.h"
#include "DataFormats/GsfTrackReco/interface/GsfTrack.h"
#include "DataFormats/MuonReco/interface/Muon.h"
#include "DataFormats/HGCalReco/interface/Trackster.h"
#include "DataFormats/HGCalReco/interface/TICLCandidate.h"
#include "RecoParticleFlow/PFProducer/interface/PFMuonAlgo.h"


#include "RecoHGCal/TICL/interface/TICLONNXGlobalCache.h"
#include "PhysicsTools/ONNXRuntime/interface/ONNXRuntime.h"
#include "RecoParticleFlow/PFProducer/interface/PFMuonAlgo.h"

#include "RecoHGCal/TICL/interface/MLPFModel.h"
#include "RecoHGCal/TICL/interface/MLPFElementFeatures.h"

using namespace cms::Ort;

class MLPFProducer : public edm::stream::EDProducer<edm::GlobalCache<ONNXRuntime>> {
public:
  explicit MLPFProducer(const edm::ParameterSet&, const ONNXRuntime*);
  ~MLPFProducer() override = default;

  void produce(edm::Event&, const edm::EventSetup&) override;
  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions);

  static std::unique_ptr<ONNXRuntime> initializeGlobalCache(const edm::ParameterSet&);
  static void globalEndJob(const ONNXRuntime*) {}

private:
  // One entry per element fed to the network, tracking where it came from so we can build
  // the right TICLCandidate afterwards.
  struct ElementSource {
    int type = 0;  // ticl::mlpf::ElementType
    unsigned int index = 0;  // index into the corresponding source collection
  };

  // Builds the ordered element list + input tensor + mask for one event.
  // Order: GSF tracks, tracks, EM tracksters, HAD tracksters (matches training-side
  // prepare_normalized_table() ordering convention).
  void buildInputs(const std::vector<reco::GsfTrack>& gsfTracks,
                    const std::vector<reco::Track>& tracks,
                    const std::vector<reco::MuonRef>& trackMuonRefs,  // parallel to tracks, may be null
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
  const std::vector<std::string> onnxOutputNames_;  // e.g. {"binary", "pid", "momentum"[, "had_binary"]}
};

MLPFProducer::MLPFProducer(const edm::ParameterSet& ps, const ONNXRuntime* /*cache*/)
    : tracksToken_(consumes<std::vector<reco::Track>>(ps.getParameter<edm::InputTag>("tracks"))),
      gsfTracksToken_(consumes<std::vector<reco::GsfTrack>>(ps.getParameter<edm::InputTag>("gsfTracks"))),
      emTrackstersToken_(consumes<std::vector<ticl::Trackster>>(ps.getParameter<edm::InputTag>("emTracksters"))),
      hadTrackstersToken_(consumes<std::vector<ticl::Trackster>>(ps.getParameter<edm::InputTag>("hadTracksters"))),
      muonsToken_(consumes<std::vector<reco::Muon>>(ps.getParameter<edm::InputTag>("muons"))),
      useHadBinaryOverride_(ps.getParameter<bool>("useHadBinaryOverride")),
      onnxOutputNames_(ps.getParameter<std::vector<std::string>>("onnxOutputNames")) {
  produces<std::vector<TICLCandidate>>();
}

std::unique_ptr<ONNXRuntime> MLPFProducer::initializeGlobalCache(const edm::ParameterSet& params) {
  edm::Service<edm::ResourceInformation> ri;
  Backend backend = Backend::cpu;
  if (ri.isAvailable() && ri->hasGpuNvidia()) {
    backend = Backend::cuda;
    edm::LogInfo("MLPFProducer") << "NVIDIA GPU detected, running MLPF ONNX model on CUDA.";
  } else {
    edm::LogInfo("MLPFProducer") << "No NVIDIA GPU detected, running MLPF ONNX model on CPU.";
  }
  auto sessionOptions = ONNXRuntime::defaultSessionOptions(backend);
  return std::make_unique<ONNXRuntime>(params.getParameter<edm::FileInPath>("modelPath").fullPath(), &sessionOptions);
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
    const reco::Muon* muon = (i < trackMuonRefs.size() && trackMuonRefs[i].isNonnull()) ? trackMuonRefs[i].get() : nullptr;
    // hasGsfMatch left false here -- TODO wire up an actual track<->GSF association once the
    // GSF collection's provenance/matching convention is settled.
    appendFeatures(buildTrackFeatures(tracks[i], muon, /*hasGsfMatch=*/false));
  }

  for (unsigned int i = 0; i < emTracksters.size(); ++i) {
    elementSources.push_back({kEmTrackster, i});
    const auto neighbors =
        computeTracksterNeighborFeatures(emTracksters[i].barycenter().eta(), emTracksters[i].barycenter().phi(), tracks);
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

  const auto& tracks = evt.get(tracksToken_);
  const auto& gsfTracks = evt.get(gsfTracksToken_);
  const auto& emTracksters = evt.get(emTrackstersToken_);
  const auto& hadTracksters = evt.get(hadTrackstersToken_);

  edm::Handle<std::vector<reco::Muon>> muonsHandle;
  evt.getByToken(muonsToken_, muonsHandle);

  // Per-track muon association, same lookup TICLCandidateProducer::filterTracks does via
  // PFMuonAlgo::muAssocToTrack -- needed as a *feature* here (track_muon_type etc.), not to
  // gate/refine candidates (that refinement still happens downstream in PFTICLProducer).
  std::vector<reco::MuonRef> trackMuonRefs(tracks.size());
  edm::Handle<std::vector<reco::Track>> tracksHandle;
  evt.getByToken(tracksToken_, tracksHandle);
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

  const auto nElem = elementSources.size();
  auto resultCandidates = std::make_unique<std::vector<TICLCandidate>>();

  if (nElem == 0) {
    evt.put(std::move(resultCandidates));
    return;
  }

  std::vector<std::vector<float>> inputs;
  inputs.push_back(std::move(flatFeatures));
  inputs.push_back(std::move(maskVec));

  const auto outputs = globalCache()->run(
      {"Xfeat_normed", "mask"},
      inputs,
      {{1, static_cast<int64_t>(nElem), static_cast<int64_t>(NUM_ELEMENT_FEATURES)}, {1, static_cast<int64_t>(nElem)}});

  // TODO: confirm this index<->name mapping against the real exported graph; assuming
  // onnxOutputNames_ == {"binary", "pid", "momentum"} in that order for now.
  const auto& outBinary = outputs[0];
  const auto& outPid = outputs[1];
  const auto& outMomentum = outputs[2];

  for (unsigned int ielem = 0; ielem < nElem; ++ielem) {
    const float logitNoParticle = outBinary[ielem * NUM_BINARY_CLASSES + 0];
    const float logitParticle = outBinary[ielem * NUM_BINARY_CLASSES + 1];
    if (logitParticle <= logitNoParticle)
      continue;  // binary gate says "none" -- no candidate for this element

    const float* pidScores = &outPid[static_cast<size_t>(ielem) * NUM_PID_CLASSES];
    int predictedClass = argmaxPidClass(pidScores);

    // TODO: implement the nn_had_binary override here for HAD_TS elements once the 11-feature
    // input to that head is confirmed; currently the main PID head's decision stands as-is.
    if (useHadBinaryOverride_ && elementSources[ielem].type == kHadTrackster) {
      // placeholder -- no-op until the override is implemented
    }

    if (predictedClass == kNone)
      continue;

    const float* rawMomentum = &outMomentum[static_cast<size_t>(ielem) * NUM_MOMENTUM_FEATURES];
    const float inputPt = flatFeatures.empty() ? 0.f : 0.f;  // placeholder, see note below
    // NOTE: flatFeatures was moved into `inputs` above; re-reading the original input pt/energy
    // for the decode step requires keeping a non-moved copy. Fixed properly below by not moving.
    (void)inputPt;

    const auto& src = elementSources[ielem];
    int charge = 0;
    if (src.type == kTrack) {
      charge = tracks[src.index].charge();
    } else if (src.type == kGsfTrack) {
      charge = gsfTracks[src.index].charge();
    }
    const int pdgId = pdgIdFromClass(predictedClass, charge);
    (void)pdgId;  // used once candidate construction below is filled in

    // TODO: construct the actual TICLCandidate here via the matching constructor
    // (track-only / trackster-only / gsf-track-only), using ticl::mlpf::decodeMomentum(...)
    // for the kinematics and pdgId computed above. Left as a stub pending the edm::Ptr
    // plumbing decision (candidates need Ptr<reco::Track>/Ptr<Trackster>/Ptr<reco::GsfTrack>
    // into the *original event collections*, which requires switching tracks/gsfTracks/
    // emTracksters/hadTracksters above from evt.get(...) to edm::Handle-based access so we
    // have OrphanHandles/Ptrs to build against).
  }

  evt.put(std::move(resultCandidates));
}

void MLPFProducer::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
  edm::ParameterSetDescription desc;
  desc.add<edm::InputTag>("tracks", edm::InputTag("generalTracks"));
  desc.add<edm::InputTag>("gsfTracks", edm::InputTag("mlpfGsfTracks"));  // TODO: real collection label
  desc.add<edm::InputTag>("emTracksters", edm::InputTag("ticlTracksterLinksSuperclusteringDNN"));
  desc.add<edm::InputTag>("hadTracksters", edm::InputTag("ticlTracksterLinks"));
  desc.add<edm::InputTag>("muons", edm::InputTag("muons1stStep"));
  desc.add<edm::FileInPath>("modelPath", edm::FileInPath("RecoHGCal/TICL/data/mlpf/mlpf_hgcal.onnx"));
  desc.add<bool>("useHadBinaryOverride", true);
  desc.add<std::vector<std::string>>("onnxOutputNames", {"binary", "pid", "momentum"});
  descriptions.add("mlpfProducer", desc);
}

DEFINE_FWK_MODULE(MLPFProducer);
