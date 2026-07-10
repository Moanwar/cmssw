// Authors: HGCAL TICL collaboration
// Custom track producer to include muon and GSF association info in NanoAOD

#include "FWCore/Framework/interface/Frameworkfwd.h"
#include "FWCore/Framework/interface/one/EDProducer.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "DataFormats/TrackReco/interface/Track.h"
#include "DataFormats/TrackReco/interface/TrackFwd.h"
#include "DataFormats/GsfTrackReco/interface/GsfTrack.h"
#include "DataFormats/MuonReco/interface/Muon.h"
#include "RecoParticleFlow/PFProducer/interface/PFMuonAlgo.h"
#include "TrackingTools/TrajectoryState/interface/TrajectoryStateTransform.h"
#include "DataFormats/PatternObjects/interface/TrackingRecHit.h"
#include "CommonTools/UtilAlgos/interface/StringObjectFunction.h"
#include "PhysicsTools/NanoAOD/interface/FlatTable.h"
#include "RecoEgamma/EgammaElectronAlgos/interface/GsfElectronTools.h"

#include <vector>
#include <map>

class TrackExtraTableProducer : public edm::one::EDProducer<> {
public:
  explicit TrackExtraTableProducer(const edm::ParameterSet& iConfig)
      : tracks_token_(consumes<std::vector<reco::Track>>(iConfig.getParameter<edm::InputTag>("tracks"))),
        muons_token_(consumes<std::vector<reco::Muon>>(iConfig.getParameter<edm::InputTag>("muons"))),
        gsf_tracks_token_(consumes<reco::GsfTrackCollection>(iConfig.getParameter<edm::InputTag>("gsfTracks"))),
        name_(iConfig.getParameter<std::string>("name")),
        doc_(iConfig.getParameter<std::string>("doc")) {
    produces<nanoaod::FlatTable>();
  }

  ~TrackExtraTableProducer() override {}

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<edm::InputTag>("tracks", edm::InputTag("generalTracks"))
        ->setComment("Track collection");
    desc.add<edm::InputTag>("muons", edm::InputTag("muons1stStep"))
        ->setComment("Muon collection");
    desc.add<edm::InputTag>("gsfTracks", edm::InputTag("electronGsfTracks"))
        ->setComment("GSF track collection");
    desc.add<std::string>("name", "GeneralTrack")->setComment("Name of output flat table");
    desc.add<std::string>("doc", "General tracks with muon/GSF info")->setComment("Documentation");
    descriptions.addWithDefaultLabel(desc);
  }

private:
  void produce(edm::Event& iEvent, const edm::EventSetup& iSetup) override {
    auto tracks_h = iEvent.getHandle(tracks_token_);
    auto muons_h = iEvent.getHandle(muons_token_);
    auto gsf_tracks_h = iEvent.getHandle(gsf_tracks_token_);

    const auto& tracks = *tracks_h;
    const auto& muons = *muons_h;
    const auto& gsf_tracks = *gsf_tracks_h;

    size_t n_tracks = tracks.size();

    // Build track index → muon association
    std::map<size_t, int> track_to_muon_idx;
    std::map<size_t, int> track_muon_type;
    std::map<size_t, int> track_muon_dt_hits;
    std::map<size_t, int> track_muon_csc_hits;

    for (size_t i = 0; i < muons.size(); ++i) {
      const auto& muon = muons[i];
      reco::TrackRef track_ref = muon.track();
      if (track_ref.isNonnull()) {
        size_t trk_idx = track_ref.key();
        if (trk_idx < n_tracks) {
          track_to_muon_idx[trk_idx] = i;
          track_muon_type[trk_idx] = muon.type();

          // Get muon chamber hits from standalone muon track
          reco::TrackRef sta_muon = muon.standAloneMuon();
          if (sta_muon.isNonnull()) {
            track_muon_dt_hits[trk_idx] = sta_muon->hitPattern().numberOfValidMuonDTHits();
            track_muon_csc_hits[trk_idx] = sta_muon->hitPattern().numberOfValidMuonCSCHits();
          } else {
            track_muon_dt_hits[trk_idx] = 0;
            track_muon_csc_hits[trk_idx] = 0;
          }
        }
      }
    }

    // Build track index → GSF track association
    std::map<size_t, int> track_to_gsf_idx;
    edm::soa::EtaPhiTable ctf_table(tracks);
    edm::soa::EtaPhiTableView ctf_view = ctf_table;

    for (size_t i = 0; i < gsf_tracks.size(); ++i) {
      reco::GsfTrackRef gsf_ref(gsf_tracks_h, i);
      auto result = egamma::getClosestCtfToGsf(gsf_ref, tracks_h, ctf_view);
      if (result.first.isNonnull()) {
        size_t trk_idx = result.first.key();
        if (trk_idx < n_tracks) {
          track_to_gsf_idx[trk_idx] = i;
        }
      }
    }

    // Create output table
    auto out = std::make_unique<nanoaod::FlatTable>(n_tracks, name_, false, true);

    // Fill muon info columns
    std::vector<int> muon_type(n_tracks, -1);
    std::vector<int> muon_dt_hits(n_tracks, -1);
    std::vector<int> muon_csc_hits(n_tracks, -1);
    std::vector<int> gsf_type(n_tracks, 0);

    for (size_t i = 0; i < n_tracks; ++i) {
      if (track_to_muon_idx.count(i)) {
        muon_type[i] = track_muon_type.count(i) ? track_muon_type[i] : 0;
        muon_dt_hits[i] = track_muon_dt_hits.count(i) ? track_muon_dt_hits[i] : 0;
        muon_csc_hits[i] = track_muon_csc_hits.count(i) ? track_muon_csc_hits[i] : 0;
      }
      if (track_to_gsf_idx.count(i)) {
        gsf_type[i] = 1;
      }
    }

    out->addColumn<int>("muonType", muon_type, "Muon type (0=not a muon, 1+=muon type code)");
    out->addColumn<int>("muonDtHits", muon_dt_hits, "Muon DT chamber hits (-1 if not a muon)");
    out->addColumn<int>("muonCscHits", muon_csc_hits, "Muon CSC chamber hits (-1 if not a muon)");
    out->addColumn<int>("gsfType", gsf_type, "GSF track association (0=no GSF, 1=has associated GSF)");

    out->setDoc(doc_);
    iEvent.put(std::move(out));
  }

  const edm::EDGetTokenT<std::vector<reco::Track>> tracks_token_;
  const edm::EDGetTokenT<std::vector<reco::Muon>> muons_token_;
  const edm::EDGetTokenT<reco::GsfTrackCollection> gsf_tracks_token_;
  const std::string name_;
  const std::string doc_;
};

DEFINE_FWK_MODULE(TrackExtraTableProducer);
