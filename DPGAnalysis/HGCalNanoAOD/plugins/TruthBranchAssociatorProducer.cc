// Author Mohamed Darwish
// Matches HGCAL tracksters and tracks to the truth graph built by
// PhysicsTools/TruthInfo (truthLogicalGraphProducer / truthLogicalGraphHitIndexProducer),
// restricted to one truth level's antichain - by default Level::ReconstructableFromSignal,
// the first stable, reconstructable particles a chosen signal resonance produced
// (see PhysicsTools/TruthInfo/interface/TruthLevels.h). This is a fixed-level match
// only: no adaptive climb to ancestor levels, so a match never moves outside the
// configured level.
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/global/EDProducer.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"

#include "DataFormats/CaloRecHit/interface/CaloCluster.h"
#include "DataFormats/Common/interface/View.h"
#include "DataFormats/HGCalReco/interface/Trackster.h"
#include "DataFormats/TrackReco/interface/Track.h"

#include "SimDataFormats/Associations/interface/TICLAssociationMap.h"
#include "SimDataFormats/TruthInfo/interface/Graph.h"
#include "SimDataFormats/TruthInfo/interface/LogicalGraphHitIndex.h"

#include "PhysicsTools/TruthInfo/interface/BranchHitAssociator.h"
#include "PhysicsTools/TruthInfo/interface/RecoHitAdapters.h"
#include "PhysicsTools/TruthInfo/interface/TruthLevels.h"

namespace {
  using BranchAssociationMap = ticl::TICLAssociationMap<ticl::mapWithSharedEnergyAndScore>;
}  // namespace

class TruthBranchAssociatorProducer : public edm::global::EDProducer<> {
public:
  explicit TruthBranchAssociatorProducer(edm::ParameterSet const&);
  void produce(edm::StreamID, edm::Event&, edm::EventSetup const&) const override;
  static void fillDescriptions(edm::ConfigurationDescriptions&);

private:
  const edm::EDGetTokenT<truth::Graph> graphToken_;
  const edm::EDGetTokenT<truth::LogicalGraphHitIndex> hitIndexToken_;
  const edm::EDGetTokenT<std::vector<reco::CaloCluster>> layerClustersToken_;
  std::vector<std::pair<std::string, edm::EDGetTokenT<std::vector<ticl::Trackster>>>> tracksterTokens_;
  std::vector<std::pair<std::string, edm::EDGetTokenT<edm::View<reco::Track>>>> trackTokens_;
  const truth::Level rootLevel_;
};

namespace {
  // "label" or "label:instance", matching the AllTracksterToTruthBranchAssociatorsProducer
  // convention so the same InputTag can be told apart when its instance name matters.
  std::string tagLabel(edm::InputTag const& tag) {
    std::string label = tag.label();
    if (!tag.instance().empty())
      label += tag.instance();
    return label;
  }
}  // namespace

TruthBranchAssociatorProducer::TruthBranchAssociatorProducer(edm::ParameterSet const& cfg)
    : graphToken_(consumes<truth::Graph>(cfg.getParameter<edm::InputTag>("graph"))),
      hitIndexToken_(consumes<truth::LogicalGraphHitIndex>(cfg.getParameter<edm::InputTag>("hitIndex"))),
      layerClustersToken_(consumes<std::vector<reco::CaloCluster>>(cfg.getParameter<edm::InputTag>("layerClusters"))),
      rootLevel_(truth::levelFromName(cfg.getParameter<std::string>("rootLevel"))) {
  for (auto const& tag : cfg.getParameter<std::vector<edm::InputTag>>("tracksterCollections")) {
    const std::string label = tagLabel(tag);
    tracksterTokens_.emplace_back(label, consumes<std::vector<ticl::Trackster>>(tag));
    produces<BranchAssociationMap>(label + "ToTruthBranch");
    produces<BranchAssociationMap>("TruthBranchTo" + label);
  }
  for (auto const& tag : cfg.getParameter<std::vector<edm::InputTag>>("trackCollections")) {
    const std::string label = tagLabel(tag);
    trackTokens_.emplace_back(label, consumes<edm::View<reco::Track>>(tag));
    produces<BranchAssociationMap>(label + "ToTruthBranch");
    produces<BranchAssociationMap>("TruthBranchTo" + label);
  }
  produces<std::vector<unsigned int>>("truthBranchRoots");
}

void TruthBranchAssociatorProducer::produce(edm::StreamID, edm::Event& event, edm::EventSetup const&) const {
  auto const& graph = event.get(graphToken_);
  auto const& hitIndex = event.get(hitIndexToken_);

  const std::vector<uint32_t> roots = truth::levelAntichain(graph, rootLevel_);

  std::vector<uint32_t> rootRow(graph.nParticles(), truth::BranchMatch::kInvalidRoot);
  for (std::size_t i = 0; i < roots.size(); ++i)
    rootRow[roots[i]] = static_cast<uint32_t>(i);

  event.put(std::make_unique<std::vector<unsigned int>>(roots.begin(), roots.end()), "truthBranchRoots");

  using truth::byAscendingScore;

  if (!tracksterTokens_.empty()) {
    auto const& layerClusters = event.get(layerClustersToken_);
    truth::BranchHitAssociator caloAssoc(hitIndex,
                                         roots,
                                         truth::BranchHitAssociator::Metric::SharedEnergy,
                                         truth::HitChannel::Calo,
                                         /*emptyRootsMeansAll=*/false);

    for (auto const& [label, token] : tracksterTokens_) {
      auto const& tracksters = event.get(token);
      auto tracksterToBranch = std::make_unique<BranchAssociationMap>(static_cast<unsigned int>(tracksters.size()));
      auto branchToTrackster = std::make_unique<BranchAssociationMap>(static_cast<unsigned int>(roots.size()));

      for (unsigned int t = 0; t < tracksters.size(); ++t) {
        const auto hits = truth::recoHits(tracksters[t], layerClusters);
        if (hits.empty())
          continue;
        for (auto const& m : caloAssoc.bestBranches(std::span<const truth::RecoHit>(hits))) {
          const uint32_t row = rootRow[m.rootParticleId];
          tracksterToBranch->insert(t, row, m.sharedEnergy, m.score);
          branchToTrackster->insert(row, t, m.sharedEnergy, m.reverseScore);
        }
      }
      tracksterToBranch->sort(byAscendingScore);
      branchToTrackster->sort(byAscendingScore);
      event.put(std::move(tracksterToBranch), label + "ToTruthBranch");
      event.put(std::move(branchToTrackster), "TruthBranchTo" + label);
    }
  }

  if (!trackTokens_.empty()) {
    truth::BranchHitAssociator trackAssoc(hitIndex,
                                          roots,
                                          truth::BranchHitAssociator::Metric::SharedHits,
                                          truth::HitChannel::Tracker,
                                          /*emptyRootsMeansAll=*/false);

    for (auto const& [label, token] : trackTokens_) {
      auto const& tracks = event.get(token);
      auto trackToBranch = std::make_unique<BranchAssociationMap>(static_cast<unsigned int>(tracks.size()));
      auto branchToTrack = std::make_unique<BranchAssociationMap>(static_cast<unsigned int>(roots.size()));

      for (unsigned int i = 0; i < tracks.size(); ++i) {
        const auto hits = truth::recoHits(tracks[i]);
        if (hits.empty())
          continue;
        for (auto const& m : trackAssoc.bestBranches(std::span<const truth::RecoHit>(hits))) {
          const uint32_t row = rootRow[m.rootParticleId];
          trackToBranch->insert(i, row, m.sharedEnergy, m.score);
          branchToTrack->insert(row, i, m.sharedEnergy, m.reverseScore);
        }
      }
      trackToBranch->sort(byAscendingScore);
      branchToTrack->sort(byAscendingScore);
      event.put(std::move(trackToBranch), label + "ToTruthBranch");
      event.put(std::move(branchToTrack), "TruthBranchTo" + label);
    }
  }
}

void TruthBranchAssociatorProducer::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
  edm::ParameterSetDescription desc;
  desc.add<edm::InputTag>("graph", edm::InputTag("truthLogicalGraphProducer"));
  desc.add<edm::InputTag>("hitIndex", edm::InputTag("truthLogicalGraphHitIndexProducer"));
  desc.add<edm::InputTag>("layerClusters", edm::InputTag("hgcalMergeLayerClusters"));
  desc.add<std::string>("rootLevel", "reconstructableFromSignal")
      ->setComment(
          "truth::Level name (PhysicsTools/TruthInfo/interface/TruthLevels.h) whose "
          "antichain seeds the branch roots.");
  desc.add<std::vector<edm::InputTag>>(
      "tracksterCollections", {edm::InputTag("ticlTrackstersCLUE3DHigh"), edm::InputTag("ticlTracksterLinks")});
  desc.add<std::vector<edm::InputTag>>("trackCollections", {edm::InputTag("generalTracks")});
  descriptions.add("truthBranchAssociator", desc);
}

#include "FWCore/Framework/interface/MakerMacros.h"
DEFINE_FWK_MODULE(TruthBranchAssociatorProducer);
