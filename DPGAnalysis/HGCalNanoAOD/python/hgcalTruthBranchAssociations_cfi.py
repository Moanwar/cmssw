import FWCore.ParameterSet.Config as cms
from PhysicsTools.NanoAOD.common_cff import *

# Matches HGCAL tracksters and tracks to the truth graph (PhysicsTools/TruthInfo),
# restricted to the ReconstructableFromSignal level: the first stable,
# reconstructable particles the chosen signal resonance produced. Requires a RECO(-SIM)
# job with the truth graph enabled, i.e. truthLogicalGraphProducer /
# truthLogicalGraphHitIndexProducer already in the event (see
# PhysicsTools.TruthInfo.addAdaptiveAssociator / customiseTruthMixedReco for how that
# is scheduled upstream). Fixed-level match only: see TruthBranchAssociatorProducer.cc.
truthBranchAssociator = cms.EDProducer(
    "TruthBranchAssociatorProducer",
    graph=cms.InputTag("truthLogicalGraphProducer"),
    hitIndex=cms.InputTag("truthLogicalGraphHitIndexProducer"),
    layerClusters=cms.InputTag("hgcalMergeLayerClusters"),
    rootLevel=cms.string("reconstructableFromSignal"),
    tracksterCollections=cms.VInputTag(
        cms.InputTag("ticlTrackstersCLUE3DHigh"),
        cms.InputTag("ticlTracksterLinks"),
    ),
    trackCollections=cms.VInputTag(cms.InputTag("generalTracks")),
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


# CLUE3D tracksters <-> truth branch (ReconstructableFromSignal)
ticlTrackstersCLUE3DHighToTruthBranchTable = _truthBranchAssocTable(
    "TracksterCLUE3DHigh2TruthBranch",
    "ticlTrackstersCLUE3DHighToTruthBranch",
    "CLUE3D tracksters matched to ReconstructableFromSignal truth branches, by shared HGCAL rechit energy.",
)
truthBranchToTiclTrackstersCLUE3DHighTable = _truthBranchAssocTable(
    "TruthBranch2TracksterCLUE3DHigh",
    "TruthBranchToticlTrackstersCLUE3DHigh",
    "ReconstructableFromSignal truth branches matched to CLUE3D tracksters, by shared HGCAL rechit energy.",
)

# Linked tracksters (post TracksterLinksProducer) <-> truth branch
ticlTracksterLinksToTruthBranchTable = _truthBranchAssocTable(
    "TracksterLinks2TruthBranch",
    "ticlTracksterLinksToTruthBranch",
    "Linked tracksters matched to ReconstructableFromSignal truth branches, by shared HGCAL rechit energy.",
)
truthBranchToTiclTracksterLinksTable = _truthBranchAssocTable(
    "TruthBranch2TracksterLinks",
    "TruthBranchToticlTracksterLinks",
    "ReconstructableFromSignal truth branches matched to linked tracksters, by shared HGCAL rechit energy.",
)

# General tracks <-> truth branch
generalTracksToTruthBranchTable = _truthBranchAssocTable(
    "Track2TruthBranch",
    "generalTracksToTruthBranch",
    "General tracks matched to ReconstructableFromSignal truth branches, by shared tracker hit count.",
)
truthBranchToGeneralTracksTable = _truthBranchAssocTable(
    "TruthBranch2Track",
    "TruthBranchTogeneralTracks",
    "ReconstructableFromSignal truth branches matched to general tracks, by shared tracker hit count.",
)

hgcalTruthBranchAssociationTableSequence = cms.Sequence(
    truthBranchAssociator
    + truthBranchTable
    + ticlTrackstersCLUE3DHighToTruthBranchTable
    + truthBranchToTiclTrackstersCLUE3DHighTable
    + ticlTracksterLinksToTruthBranchTable
    + truthBranchToTiclTracksterLinksTable
    + generalTracksToTruthBranchTable
    + truthBranchToGeneralTracksTable
)
