// reco::PFCluster and reco::PFRecHit have no stock Simple*FlatTableProducer in
// PhysicsTools/NanoAOD (unlike reco::Track, reco::GsfTrack, etc.), so this package
// registers its own, the same minimal typedef + DEFINE_FWK_MODULE wrapper
// PhysicsTools/NanoAOD/plugins/SimpleFlatTableProducerPlugins.cc uses for every other
// reco type.
#include "DataFormats/ParticleFlowReco/interface/PFCluster.h"
#include "DataFormats/ParticleFlowReco/interface/PFRecHit.h"
#include "PhysicsTools/NanoAOD/interface/SimpleFlatTableProducer.h"

typedef SimpleFlatTableProducer<reco::PFCluster> SimplePFClusterFlatTableProducer;
typedef SimpleFlatTableProducer<reco::PFRecHit> SimplePFRecHitFlatTableProducer;

#include "FWCore/Framework/interface/MakerMacros.h"
DEFINE_FWK_MODULE(SimplePFClusterFlatTableProducer);
DEFINE_FWK_MODULE(SimplePFRecHitFlatTableProducer);
