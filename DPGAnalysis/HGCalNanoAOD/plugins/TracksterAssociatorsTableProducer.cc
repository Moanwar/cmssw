#include "PhysicsTools/NanoAOD/interface/AssociationMapFlatTableProducer.h"

#include "DataFormats/HGCalReco/interface/Trackster.h"
#include "SimDataFormats/Associations/interface/TICLAssociationMap.h"
#include "SimDataFormats/CaloAnalysis/interface/SimCluster.h"
#include "SimDataFormats/CaloAnalysis/interface/CaloParticle.h"

typedef AssociationOneToOneFlatTableProducer<AssociationMapOneToOneFraction<SimCluster, CaloParticle>>
    SimClusterCaloParticleFractionFlatTableProducer;

typedef AssociationOneToManyFlatTableProducer<AssociationMapOneToManySharedEnergyScore<ticl::Trackster, ticl::Trackster>>
    TracksterTracksterEnergyScoreFlatTableProducer;

// Generic index-based map (no bound edm collections): what TruthBranchAssociatorProducer
// emits for trackster/track <-> truth-branch associations.
typedef AssociationOneToManyFlatTableProducer<ticl::TICLAssociationMap<ticl::mapWithSharedEnergyAndScore>>
    TruthBranchAssociationFlatTableProducer;

#include "FWCore/Framework/interface/MakerMacros.h"
DEFINE_FWK_MODULE(SimClusterCaloParticleFractionFlatTableProducer);
DEFINE_FWK_MODULE(TracksterTracksterEnergyScoreFlatTableProducer);
DEFINE_FWK_MODULE(TruthBranchAssociationFlatTableProducer);
