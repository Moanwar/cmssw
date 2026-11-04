// reco::GsfElectron has no stock Simple*FlatTableProducer in PhysicsTools/NanoAOD
// (unlike reco::GsfTrack), so this package registers its own, the same minimal
// typedef + DEFINE_FWK_MODULE wrapper PhysicsTools/NanoAOD/plugins/
// SimpleFlatTableProducerPlugins.cc uses for every other reco type.
#include "DataFormats/EgammaCandidates/interface/GsfElectron.h"
#include "PhysicsTools/NanoAOD/interface/SimpleFlatTableProducer.h"

typedef SimpleFlatTableProducer<reco::GsfElectron> SimpleGsfElectronFlatTableProducer;

#include "FWCore/Framework/interface/MakerMacros.h"
DEFINE_FWK_MODULE(SimpleGsfElectronFlatTableProducer);
