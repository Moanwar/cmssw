import FWCore.ParameterSet.Config as cms
from PhysicsTools.NanoAOD.common_cff import *

# Matches HGCAL tracksters, PFClusters, general tracks and GSF tracks to the truth graph
# (PhysicsTools/TruthInfo), restricted to the PFReconstructable level: the first
# particle of each signal decay/interaction chain that physically left a real signature
# in the detector (TrackerSimHit, CaloSimHit or MuonSimHit), walking through BOTH GEN
# decay vertices and SIM continuations -- see TruthLevels.h. Requires a RECO(-SIM)
# job with the truth graph enabled, i.e. truthLogicalGraphProducer /
# truthLogicalGraphHitIndexProducer already in the event (see
# PhysicsTools.TruthInfo.addAdaptiveAssociator / customiseTruthMixedReco for how that
# is scheduled upstream). Fixed-level match only: see TruthBranchAssociatorProducer.cc.
truthBranchAssociator = cms.EDProducer(
    "TruthBranchAssociatorProducer",
    graph=cms.InputTag("truthLogicalGraphProducer"),
    hitIndex=cms.InputTag("truthLogicalGraphHitIndexProducer"),
    layerClusters=cms.InputTag("hgcalMergeLayerClusters"),
    rootLevel=cms.string("pfReconstructable"),
    tracksterCollections=cms.VInputTag(
        cms.InputTag("ticlTrackstersCLUE3DHigh"),
        cms.InputTag("ticlTracksterLinks"),
    ),
    # Barrel calorimeters ONLY: the endcap is HGCal, already covered by the trackster
    # associators above, and particleFlowClusterHGCal would just duplicate that same
    # calo-hit matching a second time. particleFlowClusterECAL (final/corrected) and
    # particleFlowClusterHCAL (final, depth-merged from HBHE) are the standard barrel
    # PFCluster collections everything downstream consumes. reco::PFCluster derives
    # from reco::CaloCluster and carries its own (DetId, fraction) hits directly, so
    # it is matched like a trackster -- shared calo-hit energy -- without needing a
    # separate layerClusters handle.
    pfClusterCollections=cms.VInputTag(
        cms.InputTag("particleFlowClusterECAL"),
        cms.InputTag("particleFlowClusterHCAL"),
    ),
    # Read as edm::View<reco::Track>: electronGsfTracks works here unchanged because
    # reco::GsfTrack derives from reco::Track, matched by shared tracker hit count
    # exactly like generalTracks.
    trackCollections=cms.VInputTag(
        cms.InputTag("generalTracks"),
        cms.InputTag("electronGsfTracks"),
    ),
)

# One row per truth branch root (same row numbering as the "index" field in every
# association table below): pdgId, kinematics, and a pileup flag, read off the
# exact roots list truthBranchAssociator used (see truthBranchAssociator:truthBranchRoots).
truthBranchTable = cms.EDProducer(
    "TruthBranchTableProducer",
    graph=cms.InputTag("truthLogicalGraphProducer"),
    roots=cms.InputTag("truthBranchAssociator", "truthBranchRoots"),
    name=cms.string("TruthBranch"),
)

# Truth-level PV: the signal interaction's Interaction-vertex position in the truth
# graph (see TruthPVTableProducer.cc). Diagnostic only - never a model input, only
# for validating offlinePrimaryVertices (hgcalPV_cfi.py) and any PV-relative features
# built from it.
truthPVTable = cms.EDProducer(
    "TruthPVTableProducer",
    graph=cms.InputTag("truthLogicalGraphProducer"),
    name=cms.string("TruthPV"),
)

_truthBranchAssocLinkVars = cms.PSet(
    index=Var("index", "uint", doc="Index of the associated object on the other side of the link."),
    sharedEnergy=Var("sharedEnergy", "float",
                      doc="Shared energy (trackster links) or shared-hit count (track links)."),
    score=Var("score", "float", doc="Association score, lower is better."),
)


def _truthBranchAssocTable(name, srcInstance, doc):
    """One flat table for one direction of one trackster/track <-> truth-branch map.

    srcInstance is the product instance name TruthBranchAssociatorProducer emits
    (e.g. "ticlTrackstersCLUE3DHighToTruthBranch" or "TruthBranchTogeneralTracks").
    """
    return cms.EDProducer(
        "TruthBranchAssociationFlatTableProducer",
        src=cms.InputTag("truthBranchAssociator", srcInstance),
        skipNonExistingSrc=cms.bool(True),
        name=cms.string(name),
        doc=cms.string(doc),
        collectionVariables=cms.PSet(
            links=cms.PSet(
                name=cms.string(name + "Links"),
                doc=cms.string("Association links."),
                useCount=cms.bool(True),
                useOffset=cms.bool(False),
                variables=_truthBranchAssocLinkVars,
            ),
        ),
    )


# CLUE3D tracksters <-> truth branch (PFReconstructable)
ticlTrackstersCLUE3DHighToTruthBranchTable = _truthBranchAssocTable(
    "TracksterCLUE3DHigh2TruthBranch",
    "ticlTrackstersCLUE3DHighToTruthBranch",
    "CLUE3D tracksters matched to PFReconstructable truth branches, by shared HGCAL rechit energy.",
)
truthBranchToTiclTrackstersCLUE3DHighTable = _truthBranchAssocTable(
    "TruthBranch2TracksterCLUE3DHigh",
    "TruthBranchToticlTrackstersCLUE3DHigh",
    "PFReconstructable truth branches matched to CLUE3D tracksters, by shared HGCAL rechit energy.",
)

# Linked tracksters (post TracksterLinksProducer) <-> truth branch
ticlTracksterLinksToTruthBranchTable = _truthBranchAssocTable(
    "TracksterLinks2TruthBranch",
    "ticlTracksterLinksToTruthBranch",
    "Linked tracksters matched to PFReconstructable truth branches, by shared HGCAL rechit energy.",
)
truthBranchToTiclTracksterLinksTable = _truthBranchAssocTable(
    "TruthBranch2TracksterLinks",
    "TruthBranchToticlTracksterLinks",
    "PFReconstructable truth branches matched to linked tracksters, by shared HGCAL rechit energy.",
)

# Barrel PFClusters <-> truth branch (ECAL and HCAL separately, each its own table pair)
ecalPFClustersToTruthBranchTable = _truthBranchAssocTable(
    "PFClusterECAL2TruthBranch",
    "particleFlowClusterECALToTruthBranch",
    "Barrel ECAL PFClusters matched to PFReconstructable truth branches, by shared rechit energy.",
)
truthBranchToEcalPFClustersTable = _truthBranchAssocTable(
    "TruthBranch2PFClusterECAL",
    "TruthBranchToparticleFlowClusterECAL",
    "PFReconstructable truth branches matched to barrel ECAL PFClusters, by shared rechit energy.",
)
hcalPFClustersToTruthBranchTable = _truthBranchAssocTable(
    "PFClusterHCAL2TruthBranch",
    "particleFlowClusterHCALToTruthBranch",
    "Barrel HCAL PFClusters matched to PFReconstructable truth branches, by shared rechit energy.",
)
truthBranchToHcalPFClustersTable = _truthBranchAssocTable(
    "TruthBranch2PFClusterHCAL",
    "TruthBranchToparticleFlowClusterHCAL",
    "PFReconstructable truth branches matched to barrel HCAL PFClusters, by shared rechit energy.",
)

# General tracks <-> truth branch
generalTracksToTruthBranchTable = _truthBranchAssocTable(
    "Track2TruthBranch",
    "generalTracksToTruthBranch",
    "General tracks matched to PFReconstructable truth branches, by shared tracker hit count.",
)
truthBranchToGeneralTracksTable = _truthBranchAssocTable(
    "TruthBranch2Track",
    "TruthBranchTogeneralTracks",
    "PFReconstructable truth branches matched to general tracks, by shared tracker hit count.",
)

# GSF tracks <-> truth branch
gsfTracksToTruthBranchTable = _truthBranchAssocTable(
    "GSFTrack2TruthBranch",
    "electronGsfTracksToTruthBranch",
    "GSF tracks matched to PFReconstructable truth branches, by shared tracker hit count.",
)
truthBranchToGsfTracksTable = _truthBranchAssocTable(
    "TruthBranch2GSFTrack",
    "TruthBranchToelectronGsfTracks",
    "PFReconstructable truth branches matched to GSF tracks, by shared tracker hit count.",
)

hgcalTruthBranchAssociationTableSequence = cms.Sequence(
    truthBranchAssociator
    + truthBranchTable
    + truthPVTable
    + ticlTrackstersCLUE3DHighToTruthBranchTable
    + truthBranchToTiclTrackstersCLUE3DHighTable
    + ticlTracksterLinksToTruthBranchTable
    + truthBranchToTiclTracksterLinksTable
    + ecalPFClustersToTruthBranchTable
    + truthBranchToEcalPFClustersTable
    + hcalPFClustersToTruthBranchTable
    + truthBranchToHcalPFClustersTable
    + generalTracksToTruthBranchTable
    + truthBranchToGeneralTracksTable
    + gsfTracksToTruthBranchTable
    + truthBranchToGsfTracksTable
)
