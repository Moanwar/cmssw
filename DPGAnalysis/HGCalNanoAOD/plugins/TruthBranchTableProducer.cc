// Kinematics / PID / pileup-flag table for the truth-graph branch roots that
// TruthBranchAssociatorProducer matches tracksters and tracks against (by default
// the ReconstructableFromSignal antichain).
//
// Row i of this table IS the truth particle at graph particle id
// roots[i] == (truthBranchAssociator, "truthBranchRoots")[i] - the exact same
// vector TruthBranchAssociatorProducer used to build its compact branch-row
// numbering, read here rather than recomputed, so the two products can never
// disagree on row numbering even if levelAntichain's definition changes later.
// This is exactly the row number every *ToTruthBranch / TruthBranchTo*
// association table's branch-side "index" already points into.

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/global/EDProducer.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"

#include "DataFormats/NanoAOD/interface/FlatTable.h"
#include "SimDataFormats/EncodedEventId/interface/EncodedEventId.h"
#include "SimDataFormats/TruthInfo/interface/Graph.h"

class TruthBranchTableProducer : public edm::global::EDProducer<> {
public:
  explicit TruthBranchTableProducer(edm::ParameterSet const&);
  void produce(edm::StreamID, edm::Event&, edm::EventSetup const&) const override;
  static void fillDescriptions(edm::ConfigurationDescriptions&);

private:
  const edm::EDGetTokenT<truth::Graph> graphToken_;
  const edm::EDGetTokenT<std::vector<unsigned int>> rootsToken_;
  const std::string name_;
};

TruthBranchTableProducer::TruthBranchTableProducer(edm::ParameterSet const& cfg)
    : graphToken_(consumes<truth::Graph>(cfg.getParameter<edm::InputTag>("graph"))),
      rootsToken_(consumes<std::vector<unsigned int>>(cfg.getParameter<edm::InputTag>("roots"))),
      name_(cfg.getParameter<std::string>("name")) {
  produces<nanoaod::FlatTable>();
}

void TruthBranchTableProducer::produce(edm::StreamID, edm::Event& event, edm::EventSetup const&) const {
  auto const& graph = event.get(graphToken_);
  auto const& roots = event.get(rootsToken_);
  const std::size_t n = roots.size();

  std::vector<int32_t> pdgId(n, 0);
  std::vector<float> pt(n, 0.f), eta(n, 0.f), phi(n, 0.f), mass(n, 0.f), energy(n, 0.f);
  std::vector<int32_t> bunchCrossing(n, 0), simEvent(n, 0);
  std::vector<uint8_t> isSignal(n, 0);

  for (std::size_t i = 0; i < n; ++i) {
    const unsigned int id = roots[i];
    if (id >= graph.nParticles())
      continue;
    auto const& p = graph.particles()[id];
    pdgId[i] = p.pdgId;
    pt[i] = p.momentum.pt();
    eta[i] = p.momentum.eta();
    phi[i] = p.momentum.phi();
    mass[i] = p.momentum.mass();
    energy[i] = p.momentum.energy();

    // Same decode as TruthLogicalGraphProducer's packEventId / Branch::isSignal:
    // the packed SIM event id's low word is an EncodedEventId. bunchCrossing==0
    // and event==0 is the signal interaction; anything else is pileup, matching
    // the isPU convention SimTracksterTableProducer already uses.
    uint32_t raw = 0;
    std::memcpy(&raw, &p.eventId, sizeof(raw));
    const EncodedEventId eid(raw);
    bunchCrossing[i] = eid.bunchCrossing();
    simEvent[i] = eid.event();
    isSignal[i] = (eid.bunchCrossing() == 0 && eid.event() == 0) ? 1 : 0;
  }

  auto table = std::make_unique<nanoaod::FlatTable>(n, name_, /*singleton=*/false);
  table->addColumn<int32_t>("pdgId", pdgId, "PDG id of the truth branch root");
  table->addColumn<float>("pt", pt, "Truth branch root p_T [GeV]");
  table->addColumn<float>("eta", eta, "Truth branch root pseudorapidity");
  table->addColumn<float>("phi", phi, "Truth branch root phi");
  table->addColumn<float>("mass", mass, "Truth branch root mass [GeV]");
  table->addColumn<float>("energy", energy, "Truth branch root energy [GeV]");
  table->addColumn<int32_t>("bunchCrossing", bunchCrossing, "SIM bunch crossing of the truth branch root");
  table->addColumn<int32_t>("simEvent", simEvent, "SIM event number of the truth branch root");
  table->addColumn<uint8_t>(
      "isSignal", isSignal, "1 if from the signal interaction (bunchCrossing==0 && event==0), 0 if pileup");
  event.put(std::move(table));
}

void TruthBranchTableProducer::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
  edm::ParameterSetDescription desc;
  desc.add<edm::InputTag>("graph", edm::InputTag("truthLogicalGraphProducer"));
  desc.add<edm::InputTag>("roots", edm::InputTag("truthBranchAssociator", "truthBranchRoots"))
      ->setComment("vector<unsigned int> of truth::Graph particle ids, row i of this table = roots[i].");
  desc.add<std::string>("name", "TruthBranch");
  descriptions.add("truthBranchTable", desc);
}

#include "FWCore/Framework/interface/MakerMacros.h"
DEFINE_FWK_MODULE(TruthBranchTableProducer);
