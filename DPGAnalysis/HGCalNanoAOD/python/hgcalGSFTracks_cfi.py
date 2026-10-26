import FWCore.ParameterSet.Config as cms
from PhysicsTools.NanoAOD.common_cff import *

# dz/dxy w.r.t. the leading PV (offlinePrimaryVertices[0]), same mechanism as
# generalTracks' trackPVInfo in hgcalGeneralTracks_cfi.py - reco::GsfTrack derives
# from reco::Track and inherits dz(Point)/dxy(Point) unchanged.
gsfTrackPVInfo = cms.EDProducer("GsfTrackPVInfoProducer",
    tracks   = cms.InputTag("electronGsfTracks"),
    vertices = cms.InputTag("offlinePrimaryVertices"),
)

# GSF Tracks table for NanoAOD
gsfTracksTable = cms.EDProducer(
    "SimpleGsfTrackFlatTableProducer",
    skipNonExistingSrc=cms.bool(True),
    src=cms.InputTag("electronGsfTracks"),
    cut=cms.string(""),
    name=cms.string("GSFTrack"),
    doc=cms.string("GSF tracks"),
    extension=cms.bool(False),
    variables=cms.PSet(
        # Basic kinematics 
        pt=Var("pt()", "float", doc="GSF track p_T [GeV]"),
        p=Var("p()", "float", doc="GSF track momentum magnitude [GeV]"),
        eta=Var("eta()", "float", doc="GSF track pseudorapidity"),
        phi=Var("phi()", "float", doc="GSF track phi angle [rad]"),
        charge=Var("charge()", "int", doc="GSF track charge"),
        
        # Position 
        vx=Var("vx()", "float", doc="GSF track vertex x [cm]"),
        vy=Var("vy()", "float", doc="GSF track vertex y [cm]"),
        vz=Var("vz()", "float", doc="GSF track vertex z [cm]"),
        
        nhits=Var("recHitsSize()", "int", doc="GSF track nHits"),
        ptMode=Var("ptMode()", "float", doc="GSF track p_T Mode [GeV]"),
        ptModeError=Var("ptModeError()", "float", doc="GSF track p_T Mode Error"),
        pxMode=Var("pxMode()", "float", doc="GSF track px Mode [GeV]"),
	pyMode=Var("pyMode()", "float", doc="GSF track py Mode [GeV]"),
	pzMode=Var("pzMode()", "float", doc="GSF track pz Mode [GeV]"),
        pMode=Var("pMode()", "float", doc="GSF track momentum Mode [GeV]"),
        etaMode=Var("etaMode()", "float", doc="GSF track Mode pseudorapidity"),
        etaModeError=Var("etaModeError()", "float", doc="GSF track Mode pseudorapidity Error"),
        phiMode=Var("phiMode()", "float", doc="GSF track phi angle Mode [rad]"),
        phiModeError=Var("phiModeError()", "float", doc="GSF track phi angle Mode Error"),
        chargeMode=Var("chargeMode()", "int", doc="GSF track charge Mode"),
        lambdaMode=Var("lambdaMode()", "float", doc="GSF track lambda Mode"),
        lambdaModeError=Var("lambdaModeError()", "float", doc="GSF track lambda Mode Error"),
        thetaMode=Var("thetaMode()", "float", doc="GSF track theta Mode"),
        thetaModeError=Var("thetaModeError()", "float", doc="GSF track theta Mode Error"),
        qoverpMode=Var("qoverpMode()", "float", doc="GSF track qoverp Mode"),
        qoverpModeError=Var("qoverpModeError()", "float", doc="GSF track qoverp Mode Error"),

    ),
    externalVariables = cms.PSet(
        pv_dz = ExtVar(cms.InputTag("gsfTrackPVInfo", "dz"), "float", doc="GSF track dz w.r.t. the leading PV [cm]"),
        pv_dxy = ExtVar(cms.InputTag("gsfTrackPVInfo", "dxy"), "float", doc="GSF track dxy w.r.t. the leading PV [cm]"),
        pv_dzError = ExtVar(cms.InputTag("gsfTrackPVInfo", "dzError"), "float",
            doc="GSF track dz uncertainty [cm] (from the track's own covariance; PV position uncertainty not propagated)"),
        pv_dxyError = ExtVar(cms.InputTag("gsfTrackPVInfo", "dxyError"), "float",
            doc="GSF track dxy uncertainty [cm] (from the track's own covariance; PV position uncertainty not propagated)"),
    ),
)

# Sequence for gsf tracks
hgcalGSFTracksTableSequence = cms.Sequence(gsfTrackPVInfo + gsfTracksTable)
