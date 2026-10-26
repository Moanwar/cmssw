import FWCore.ParameterSet.Config as cms
from PhysicsTools.NanoAOD.common_cff import *

# Offline primary vertices table for NanoAOD. offlinePrimaryVertices is built purely
# from general tracks + the beamspot (RecoVertex/Configuration/python/RecoVertex_cff.py),
# in globalreco, before particleFlowRecoTask (highlevelreco) runs - so it is already
# available to dump as-is, with no reconstruction-ordering concerns. reco::Vertex has
# a stock SimpleVertexFlatTableProducer typedef in PhysicsTools/NanoAOD, no new plugin
# needed here.
pvTable = cms.EDProducer(
    "SimpleVertexFlatTableProducer",
    skipNonExistingSrc=cms.bool(True),
    src=cms.InputTag("offlinePrimaryVertices"),
    cut=cms.string(""),
    name=cms.string("PV"),
    doc=cms.string("Offline primary vertices (track-based, built before particle flow runs)"),
    singleton=cms.bool(False),
    extension=cms.bool(False),
    variables=cms.PSet(
        x=Var("x()", "float", doc="PV position x [cm]"),
        y=Var("y()", "float", doc="PV position y [cm]"),
        z=Var("z()", "float", doc="PV position z [cm]"),
        xError=Var("xError()", "float", doc="PV position x uncertainty [cm]"),
        yError=Var("yError()", "float", doc="PV position y uncertainty [cm]"),
        zError=Var("zError()", "float", doc="PV position z uncertainty [cm]"),
        chi2=Var("chi2()", "float", doc="PV fit chi2"),
        ndof=Var("ndof()", "float", doc="PV fit number of degrees of freedom"),
        isFake=Var("isFake()", "bool", doc="PV is fake (chi2==0 && ndof==0 && no tracks)"),
        isValid=Var("isValid()", "bool", doc="PV fit is valid"),
        nTracks=Var("tracksSize()", "uint16", doc="number of tracks associated to the PV"),
    ),
)

# Index 0 is the leading/chosen PV (CMS convention: offlinePrimaryVertices is sorted
# by sum-pt2, descending). TrackPVInfoProducer (see hgcalGeneralTracks_cfi.py /
# hgcalGSFTracks_cfi.py) computes track/GSF-track dz, dxy against this same vertex.
hgcalPVTableSequence = cms.Sequence(pvTable)
