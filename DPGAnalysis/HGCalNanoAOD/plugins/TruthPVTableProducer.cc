// Truth-level primary vertex: the position of the signal event's hard-scatter vertex
// in the truth logical graph.
//
// truth::VertexRole::Interaction looked like the obvious thing to search for, but it
// is NOT a general "find the primary vertex" marker: TruthLogicalGraphPostProcessor
// only creates an Interaction-role vertex as an artificial placeholder summarizing
// ancestry CUT from a focused/trimmed selection (see VertexRole's doc comment in
// VertexData.h). On the untrimmed graph truthLogicalGraphProducer itself publishes,
// nothing was cut, so no Interaction vertex ever exists - confirmed on a 10-event
// no-PU ZEE sample where every one of ~1000-1800 vertices/event came back
// role==Normal and interactionVertices() was always empty, so TruthPV_found was 0
// for every event. What a real GEN-level hard-scatter vertex gets instead is a
// Normal-role vertex with reason()==VertexReason::HardScatter - confirmed on the
// same sample: exactly one such vertex per event, isSignal()==true, at a sane
// beamspot-scale position, close to (but not touching) a few mm/cm from
// offlinePrimaryVertices' reconstructed position.
//
// Diagnostic only: this is truth information and must never be used as a model
// input, only to validate the reconstructed PV (offlinePrimaryVertices, see
// hgcalPV_cfi.py) and any PV-relative features built from it - same leakage concern
// as the isSignal/isPU columns elsewhere in this package.
#include <memory>
#include <vector>

#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/global/EDProducer.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"

#include "DataFormats/NanoAOD/interface/FlatTable.h"
#include "SimDataFormats/TruthInfo/interface/Graph.h"
#include "SimDataFormats/TruthInfo/interface/Vertex.h"
#include "SimDataFormats/TruthInfo/interface/VertexData.h"

class TruthPVTableProducer : public edm::global::EDProducer<> {
public:
  explicit TruthPVTableProducer(edm::ParameterSet const& cfg)
      : graphToken_(consumes<truth::Graph>(cfg.getParameter<edm::InputTag>("graph"))),
        name_(cfg.getParameter<std::string>("name")) {
    produces<nanoaod::FlatTable>();
  }

  void produce(edm::StreamID, edm::Event& event, edm::EventSetup const&) const override {
    auto const& graph = event.get(graphToken_);

    float x = 0.f, y = 0.f, z = 0.f, t = 0.f;
    uint8_t found = 0;
    for (truth::Graph::size_type i = 0; i < graph.nVertices(); ++i) {
      const truth::Vertex v = graph.vertex(i);
      if (v.data().vertexReason() == truth::VertexReason::HardScatter && v.isSignal()) {
        auto const& pos = v.position();
        x = pos.x();
        y = pos.y();
        z = pos.z();
        t = pos.t();
        found = 1;
        break;
      }
    }

    auto table = std::make_unique<nanoaod::FlatTable>(1, name_, /*singleton=*/true, /*extension=*/false);
    table->addColumn<float>("x", std::vector<float>{x}, "Truth signal hard-scatter vertex position x [cm]");
    table->addColumn<float>("y", std::vector<float>{y}, "Truth signal hard-scatter vertex position y [cm]");
    table->addColumn<float>("z", std::vector<float>{z}, "Truth signal hard-scatter vertex position z [cm]");
    table->addColumn<float>("t", std::vector<float>{t}, "Truth signal hard-scatter vertex time [ns]");
    table->addColumn<uint8_t>(
        "found", std::vector<uint8_t>{found}, "1 if the signal hard-scatter vertex was found in the truth graph, 0 otherwise");
    event.put(std::move(table));
  }

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<edm::InputTag>("graph", edm::InputTag("truthLogicalGraphProducer"));
    desc.add<std::string>("name", "TruthPV");
    descriptions.add("truthPVTable", desc);
  }

private:
  const edm::EDGetTokenT<truth::Graph> graphToken_;
  const std::string name_;
};

#include "FWCore/Framework/interface/MakerMacros.h"
DEFINE_FWK_MODULE(TruthPVTableProducer);
