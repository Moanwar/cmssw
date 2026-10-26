// Flattens each reco::PFCluster's constituent rechits (hitsAndFractions(), exposed
// as recHitFractions()) into a nanoaod table, the PFCluster equivalent of the
// trackster "vertices" link into LayerClusters (see TracksterCollectionTableProducer's
// tracksterVertices collectionVariable in hgcalTracksters_cfi.py). Each
// reco::PFRecHitFraction carries a PFRecHitRef; .key() is the row index into whatever
// PFRecHit collection that ref points into, so as long as the matching
// SimplePFRecHitFlatTableProducer table (hgcalPFClusters_cfi.py) is built from that
// same source collection, recHitIndex here indexes straight into it - no separate
// rechit collection needs to be read here.
//
// Output: an "nRecHits" extension column on the cluster's own table (same row count/
// order as the cluster table) plus a flattened "<name>RecHits" sub-table of
// (recHitIndex, fraction) pairs, nRecHits[i] entries per cluster i in order - the
// same count+flat-subtable shape TICLCandidateExtraTableProducer already uses for
// its own index lists.
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/global/EDProducer.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"

#include "DataFormats/NanoAOD/interface/FlatTable.h"
#include "DataFormats/ParticleFlowReco/interface/PFCluster.h"
#include "DataFormats/ParticleFlowReco/interface/PFRecHitFraction.h"

class PFClusterConstituentTableProducer : public edm::global::EDProducer<> {
public:
  explicit PFClusterConstituentTableProducer(edm::ParameterSet const& cfg)
      : clustersToken_(consumes<std::vector<reco::PFCluster>>(cfg.getParameter<edm::InputTag>("src"))),
        name_(cfg.getParameter<std::string>("name")),
        skipNonExistingSrc_(cfg.getParameter<bool>("skipNonExistingSrc")) {
    produces<nanoaod::FlatTable>();
    produces<nanoaod::FlatTable>(name_ + "RecHits");
  }

  void produce(edm::StreamID, edm::Event& event, edm::EventSetup const&) const override {
    edm::Handle<std::vector<reco::PFCluster>> clustersH;
    event.getByToken(clustersToken_, clustersH);
    const bool present = clustersH.isValid();
    const std::size_t nClusters = present ? clustersH->size() : 0;

    std::vector<uint16_t> nRecHits(nClusters, 0);
    std::vector<uint32_t> recHitIndex;
    std::vector<float> fraction;

    if (present || !skipNonExistingSrc_) {
      for (std::size_t i = 0; i < nClusters; ++i) {
        auto const& fracs = (*clustersH)[i].recHitFractions();
        nRecHits[i] = static_cast<uint16_t>(fracs.size());
        for (auto const& fr : fracs) {
          recHitIndex.push_back(static_cast<uint32_t>(fr.recHitRef().key()));
          fraction.push_back(static_cast<float>(fr.fraction()));
        }
      }
    }

    auto ext = std::make_unique<nanoaod::FlatTable>(nClusters, name_, /*singleton=*/false, /*extension=*/true);
    ext->addColumn<uint16_t>("nRecHits", nRecHits, "Number of constituent PFRecHits");
    event.put(std::move(ext));

    auto hits =
        std::make_unique<nanoaod::FlatTable>(recHitIndex.size(), name_ + "RecHits", /*singleton=*/false, false);
    hits->addColumn<uint32_t>(
        "recHitIndex", recHitIndex, "Index into the corresponding PFRecHits table (see src's matching rechit src)");
    hits->addColumn<float>("fraction", fraction, "Fraction of the RecHit's energy owned by this PFCluster");
    hits->setDoc("Flattened constituent-PFRecHit list for " + name_ + ", nRecHits entries per cluster, in order");
    event.put(std::move(hits), name_ + "RecHits");
  }

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<bool>("skipNonExistingSrc", true)
        ->setComment("whether or not to skip producing the tables on absent input product");
    desc.add<edm::InputTag>("src", edm::InputTag("particleFlowClusterECAL"));
    desc.add<std::string>("name", "PFClusterECAL")
        ->setComment("must match the 'name' of the corresponding cluster feature table, so the nRecHits "
                     "extension lands on it and the RecHits sub-table name stays self-describing");
    descriptions.addWithDefaultLabel(desc);
  }

private:
  const edm::EDGetTokenT<std::vector<reco::PFCluster>> clustersToken_;
  const std::string name_;
  const bool skipNonExistingSrc_;
};

#include "FWCore/Framework/interface/MakerMacros.h"
DEFINE_FWK_MODULE(PFClusterConstituentTableProducer);
