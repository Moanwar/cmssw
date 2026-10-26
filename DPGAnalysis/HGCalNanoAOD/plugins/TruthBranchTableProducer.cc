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

#include <cmath>
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
#include "SimDataFormats/TruthInfo/interface/Particle.h"

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

  std::vector<int32_t> pdgId(n, 0), charge(n, 0);
  std::vector<float> pt(n, 0.f), eta(n, 0.f), phi(n, 0.f), mass(n, 0.f), energy(n, 0.f);
  std::vector<float> p(n, 0.f), px(n, 0.f), py(n, 0.f), pz(n, 0.f);
  std::vector<int32_t> bunchCrossing(n, 0), simEvent(n, 0);
  std::vector<uint8_t> isSignal(n, 0), backscattered(n, 0), hasTrackerSimHit(n, 0), hasCaloBoundary(n, 0);
  // The calorimeter-boundary checkpoint (truth::Checkpoint id 0): where and when this
  // branch root's own SimTrack crossed into the calorimeter, i.e. the SimTiclCandidate-
  // style "boundary" quantities TICLDumper's simtrackster_boundary* branches carry.
  // 0 (not NaN) when absent, same convention as every other numeric column here.
  std::vector<float> caloBoundaryX(n, 0.f), caloBoundaryY(n, 0.f), caloBoundaryZ(n, 0.f), caloBoundaryTime(n, 0.f);
  std::vector<float> caloBoundaryEta(n, 0.f), caloBoundaryPhi(n, 0.f);
  std::vector<float> caloBoundaryPx(n, 0.f), caloBoundaryPy(n, 0.f), caloBoundaryPz(n, 0.f);

  for (std::size_t i = 0; i < n; ++i) {
    const unsigned int id = roots[i];
    if (id >= graph.nParticles())
      continue;
    auto const& p4 = graph.particles()[id];
    pdgId[i] = p4.pdgId;
    pt[i] = p4.momentum.pt();
    eta[i] = p4.momentum.eta();
    phi[i] = p4.momentum.phi();
    mass[i] = p4.momentum.mass();
    energy[i] = p4.momentum.energy();
    p[i] = p4.momentum.P();
    px[i] = p4.momentum.px();
    py[i] = p4.momentum.py();
    pz[i] = p4.momentum.pz();
    backscattered[i] = p4.backscattered ? 1 : 0;
    hasTrackerSimHit[i] = p4.hasTrackerSimHit ? 1 : 0;

    // Fractional for a quark, but every branch root this table rows over (by
    // construction of the selection levels) is a lepton, a photon or a hadron, so an
    // int charge column is the useful, TICLCandidate-matching representation.
    charge[i] = static_cast<int32_t>(std::lround(truth::Particle(&graph, id).charge()));

    if (const auto cp = truth::Particle(&graph, id).checkpoint(0)) {
      hasCaloBoundary[i] = 1;
      caloBoundaryX[i] = cp->position.x();
      caloBoundaryY[i] = cp->position.y();
      caloBoundaryZ[i] = cp->position.z();
      caloBoundaryTime[i] = cp->position.t();
      caloBoundaryEta[i] = cp->position.eta();
      caloBoundaryPhi[i] = cp->position.phi();
      caloBoundaryPx[i] = cp->momentum.px();
      caloBoundaryPy[i] = cp->momentum.py();
      caloBoundaryPz[i] = cp->momentum.pz();
    }

    // Same decode as TruthLogicalGraphProducer's packEventId / Branch::isSignal:
    // the packed SIM event id's low word is an EncodedEventId. bunchCrossing==0
    // and event==0 is the signal interaction; anything else is pileup, matching
    // the isPU convention SimTracksterTableProducer already uses.
    uint32_t raw = 0;
    std::memcpy(&raw, &p4.eventId, sizeof(raw));
    const EncodedEventId eid(raw);
    bunchCrossing[i] = eid.bunchCrossing();
    simEvent[i] = eid.event();
    isSignal[i] = (eid.bunchCrossing() == 0 && eid.event() == 0) ? 1 : 0;
  }

  auto table = std::make_unique<nanoaod::FlatTable>(n, name_, /*singleton=*/false);
  table->addColumn<int32_t>("pdgId", pdgId, "PDG id of the truth branch root");
  table->addColumn<int32_t>("charge", charge, "Electric charge of the truth branch root, in units of e");
  table->addColumn<float>("pt", pt, "Truth branch root p_T [GeV]");
  table->addColumn<float>("eta", eta, "Truth branch root pseudorapidity");
  table->addColumn<float>("phi", phi, "Truth branch root phi");
  table->addColumn<float>("mass", mass, "Truth branch root mass [GeV]");
  table->addColumn<float>("energy", energy, "Truth branch root energy [GeV]");
  table->addColumn<float>("p", p, "Truth branch root momentum magnitude [GeV]");
  table->addColumn<float>("px", px, "Truth branch root momentum x component [GeV]");
  table->addColumn<float>("py", py, "Truth branch root momentum y component [GeV]");
  table->addColumn<float>("pz", pz, "Truth branch root momentum z component [GeV]");
  table->addColumn<uint8_t>(
      "backscattered", backscattered, "1 if Geant4 flagged this SimTrack as back-scattered (albedo) across the "
                                      "tracker-calo boundary");
  table->addColumn<uint8_t>(
      "hasTrackerSimHit", hasTrackerSimHit, "1 if a tracker SimHit of positive energy loss carries this particle's "
                                           "own SimTrack trackId (LevelFlag::TrackerSimHit)");
  table->addColumn<uint8_t>(
      "hasCaloBoundary", hasCaloBoundary, "1 if this SimTrack was recorded crossing the tracker-calo boundary "
                                         "outward (LevelFlag::CaloBoundary); gates the caloBoundary* columns below");
  table->addColumn<float>("caloBoundaryX", caloBoundaryX, "Calo boundary crossing position x [cm], 0 if absent");
  table->addColumn<float>("caloBoundaryY", caloBoundaryY, "Calo boundary crossing position y [cm], 0 if absent");
  table->addColumn<float>("caloBoundaryZ", caloBoundaryZ, "Calo boundary crossing position z [cm], 0 if absent");
  table->addColumn<float>(
      "caloBoundaryTime", caloBoundaryTime, "Calo boundary crossing time [ns], 0 if absent");
  table->addColumn<float>(
      "caloBoundaryEta", caloBoundaryEta, "Pseudorapidity of the calo boundary crossing position, 0 if absent");
  table->addColumn<float>("caloBoundaryPhi", caloBoundaryPhi, "Phi of the calo boundary crossing position, 0 if absent");
  table->addColumn<float>(
      "caloBoundaryPx", caloBoundaryPx, "Momentum x component at the calo boundary crossing [GeV], 0 if absent");
  table->addColumn<float>(
      "caloBoundaryPy", caloBoundaryPy, "Momentum y component at the calo boundary crossing [GeV], 0 if absent");
  table->addColumn<float>(
      "caloBoundaryPz", caloBoundaryPz, "Momentum z component at the calo boundary crossing [GeV], 0 if absent");
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
