// Per-track dz/dxy (and their errors) relative to the leading primary vertex
// (offlinePrimaryVertices[0] by default). reco::Track::dz(Point)/dxy(Point) take the
// vertex position as an external argument, so this can't be expressed as a plain
// Var() string in a SimpleFlatTableProducer cfg the way track_vx/vy/vz are - it needs
// a small producer, the same ValueMap-per-track shape as TrackMuonInfoProducer.
//
// Templated so one file serves both generalTracks (reco::Track) and
// electronGsfTracks (reco::GsfTrack, which derives from reco::Track and inherits
// dz/dxy unchanged).
#include <memory>
#include <vector>

#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/global/EDProducer.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"

#include "DataFormats/Common/interface/ValueMap.h"
#include "DataFormats/GsfTrackReco/interface/GsfTrack.h"
#include "DataFormats/TrackReco/interface/Track.h"
#include "DataFormats/VertexReco/interface/Vertex.h"
#include "DataFormats/VertexReco/interface/VertexFwd.h"

template <typename TrackT>
class TrackPVInfoProducerT : public edm::global::EDProducer<> {
public:
  explicit TrackPVInfoProducerT(edm::ParameterSet const& cfg)
      : tracksToken_(consumes<std::vector<TrackT>>(cfg.getParameter<edm::InputTag>("tracks"))),
        verticesToken_(consumes<reco::VertexCollection>(cfg.getParameter<edm::InputTag>("vertices"))) {
    produces<edm::ValueMap<float>>("dz");
    produces<edm::ValueMap<float>>("dxy");
    produces<edm::ValueMap<float>>("dzError");
    produces<edm::ValueMap<float>>("dxyError");
  }

  void produce(edm::StreamID, edm::Event& evt, const edm::EventSetup&) const override {
    edm::Handle<std::vector<TrackT>> tracksH;
    evt.getByToken(tracksToken_, tracksH);
    edm::Handle<reco::VertexCollection> verticesH;
    evt.getByToken(verticesToken_, verticesH);

    const auto& tracks = *tracksH;
    const bool havePV = verticesH.isValid() && !verticesH->empty();
    const reco::Vertex::Point pv = havePV ? (*verticesH)[0].position() : reco::Vertex::Point();

    std::vector<float> dz(tracks.size(), 0.f), dxy(tracks.size(), 0.f);
    std::vector<float> dzErr(tracks.size(), 0.f), dxyErr(tracks.size(), 0.f);

    if (havePV) {
      for (size_t i = 0; i < tracks.size(); ++i) {
        dz[i] = tracks[i].dz(pv);
        dxy[i] = tracks[i].dxy(pv);
        // Propagated from the track's own covariance only - the PV position
        // uncertainty itself is not folded in, same approximation PFMuonAlgo-style
        // consumers of dzError()/dxyError() already make.
        dzErr[i] = tracks[i].dzError();
        dxyErr[i] = tracks[i].dxyError();
      }
    }

    auto fill = [&](std::vector<float> const& v, const char* label) {
      auto vm = std::make_unique<edm::ValueMap<float>>();
      typename edm::ValueMap<float>::Filler filler(*vm);
      filler.insert(tracksH, v.begin(), v.end());
      filler.fill();
      evt.put(std::move(vm), label);
    };
    fill(dz, "dz");
    fill(dxy, "dxy");
    fill(dzErr, "dzError");
    fill(dxyErr, "dxyError");
  }

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<edm::InputTag>("tracks");
    desc.add<edm::InputTag>("vertices", edm::InputTag("offlinePrimaryVertices"));
    descriptions.addWithDefaultLabel(desc);
  }

private:
  const edm::EDGetTokenT<std::vector<TrackT>> tracksToken_;
  const edm::EDGetTokenT<reco::VertexCollection> verticesToken_;
};

using TrackPVInfoProducer = TrackPVInfoProducerT<reco::Track>;
using GsfTrackPVInfoProducer = TrackPVInfoProducerT<reco::GsfTrack>;

#include "FWCore/Framework/interface/MakerMacros.h"
DEFINE_FWK_MODULE(TrackPVInfoProducer);
DEFINE_FWK_MODULE(GsfTrackPVInfoProducer);
