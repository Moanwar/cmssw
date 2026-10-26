import FWCore.ParameterSet.Config as cms
from PhysicsTools.NanoAOD.common_cff import *

# Barrel calorimeter PFCluster features table for NanoAOD. Barrel ONLY: the endcap is
# HGCal, already covered by the trackster tables, so particleFlowClusterHGCal would
# just be a second, redundant description of the same calo hits. particleFlowClusterECAL
# (final/corrected) and particleFlowClusterHCAL (final, depth-merged from HBHE) are the
# standard barrel PFCluster collections. reco::PFCluster derives from reco::CaloCluster,
# so SimplePFClusterFlatTableProducer (registered in this package, PhysicsTools/NanoAOD
# has no stock one) exposes its own and its inherited accessors directly via method-name
# strings, the same SimpleFlatTableProducer every other NanoAOD table in this package's
# python uses for a reco type with a dictionary.
_pfClusterVariables = cms.PSet(
    energy=Var("energy()", "float", doc="PFCluster energy [GeV]"),
    correctedEnergy=Var("correctedEnergy()", "float", doc="PFCluster corrected energy [GeV]"),
    eta=Var("eta()", "float", doc="PFCluster pseudorapidity"),
    phi=Var("phi()", "float", doc="PFCluster phi angle [rad]"),
    x=Var("x()", "float", doc="PFCluster position x [cm]"),
    y=Var("y()", "float", doc="PFCluster position y [cm]"),
    z=Var("z()", "float", doc="PFCluster position z [cm]"),
    layer=Var("layer()", "int", doc="PFCluster PFLayer::Layer code"),
    depth=Var("depth()", "float", doc="PFCluster shower depth"),
    time=Var("time()", "float", doc="PFCluster time [ns]"),
    timeError=Var("timeError()", "float", doc="PFCluster time error [ns]"),
    size=Var("size()", "int", doc="Number of rechits making up the PFCluster"),
    algoID=Var("algoID()", "int", doc="PFCluster clustering algorithm id"),
)

ecalPFClustersTable = cms.EDProducer(
    "SimplePFClusterFlatTableProducer",
    skipNonExistingSrc=cms.bool(True),
    src=cms.InputTag("particleFlowClusterECAL"),
    cut=cms.string(""),
    name=cms.string("PFClusterECAL"),
    doc=cms.string("Barrel ECAL particle-flow clusters"),
    extension=cms.bool(False),
    variables=_pfClusterVariables,
)

hcalPFClustersTable = cms.EDProducer(
    "SimplePFClusterFlatTableProducer",
    skipNonExistingSrc=cms.bool(True),
    src=cms.InputTag("particleFlowClusterHCAL"),
    cut=cms.string(""),
    name=cms.string("PFClusterHCAL"),
    doc=cms.string("Barrel HCAL particle-flow clusters"),
    extension=cms.bool(False),
    variables=_pfClusterVariables,
)

# Constituent PFRecHits, the PFCluster equivalent of a trackster's "vertices" link
# into LayerClusters - gives a real, energy-weighted-over-constituents shower depth
# for PFClusters (matching compute_trackster_features's ts_depth in
# postprocessing_ticl_nopu_nano_v2.py), instead of only the PFCluster's own
# barycenter z. particleFlowClusterECAL's and particleFlowClusterHCAL's rechit refs
# point into particleFlowRecHitECAL and particleFlowRecHitHBHE respectively:
# CorrectedECALPFClusterProducer copies its input clusters verbatim before only
# rescaling their energy, and PFMultiDepthClusterProducer re-clusters
# particleFlowClusterHBHE (itself built from particleFlowRecHitHBHE, with no HO
# input in this configuration) - so in both cases the original PFRecHitRefs survive
# unchanged, and PFClusterConstituentTableProducer's recHitIndex (= ref.key()) can be
# looked up directly in the matching table below.
_pfRecHitVariables = cms.PSet(
    energy=Var("energy()", "float", doc="PFRecHit energy [GeV]"),
    time=Var("time()", "float", doc="PFRecHit time [ns]"),
    depth=Var("depth()", "int", doc="PFRecHit depth segment index (HCAL longitudinal readout segment; not meaningful for ECAL)"),
    x=Var("position().x()", "float", doc="PFRecHit position x [cm]"),
    y=Var("position().y()", "float", doc="PFRecHit position y [cm]"),
    z=Var("position().z()", "float", doc="PFRecHit position z [cm]"),
)

ecalPFRecHitsTable = cms.EDProducer(
    "SimplePFRecHitFlatTableProducer",
    skipNonExistingSrc=cms.bool(True),
    src=cms.InputTag("particleFlowRecHitECAL"),
    cut=cms.string(""),
    name=cms.string("PFRecHitECAL"),
    doc=cms.string("Barrel ECAL particle-flow rechits"),
    extension=cms.bool(False),
    variables=_pfRecHitVariables,
)

hcalPFRecHitsTable = cms.EDProducer(
    "SimplePFRecHitFlatTableProducer",
    skipNonExistingSrc=cms.bool(True),
    src=cms.InputTag("particleFlowRecHitHBHE"),
    cut=cms.string(""),
    name=cms.string("PFRecHitHBHE"),
    doc=cms.string("Barrel HCAL (HBHE) particle-flow rechits"),
    extension=cms.bool(False),
    variables=_pfRecHitVariables,
)

ecalPFClusterRecHitsTable = cms.EDProducer(
    "PFClusterConstituentTableProducer",
    skipNonExistingSrc=cms.bool(True),
    src=cms.InputTag("particleFlowClusterECAL"),
    name=cms.string("PFClusterECAL"),
)

hcalPFClusterRecHitsTable = cms.EDProducer(
    "PFClusterConstituentTableProducer",
    skipNonExistingSrc=cms.bool(True),
    src=cms.InputTag("particleFlowClusterHCAL"),
    name=cms.string("PFClusterHCAL"),
)

# Barrel PFClusters <-> truth branch, matched by shared calo-hit energy like a
# trackster (see truthBranchAssociator:pfClusterCollections in
# hgcalTruthBranchAssociations_cfi.py). The association tables themselves live there,
# next to their trackster/track/GSF-track counterparts; this cfi only adds the
# PFClusters' own feature tables.
barrelPFClustersTableSequence = cms.Sequence(ecalPFClustersTable + hcalPFClustersTable)

# RecHit-level tables + cluster->rechit constituent links. Kept as a separate
# sequence from barrelPFClustersTableSequence (added to OfflineHGCalValidationTables,
# not OfflineHGCalTables) since this is extra, heavier-weight info not needed unless
# something downstream actually wants a real per-cluster shower depth.
barrelPFRecHitsTableSequence = cms.Sequence(
    ecalPFRecHitsTable + hcalPFRecHitsTable + ecalPFClusterRecHitsTable + hcalPFClusterRecHitsTable
)
