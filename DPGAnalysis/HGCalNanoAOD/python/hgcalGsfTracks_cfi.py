import FWCore.ParameterSet.Config as cms
from PhysicsTools.NanoAOD.common_cff import *

# GSF Tracks table for NanoAOD - optimized for MLPF electron/photon reconstruction
# GSF (Gaussian Sum Filter) tracks provide best estimate of electron momentum accounting for bremsstrahlung
gsfTracksTable = cms.EDProducer(
    "SimpleTriggerTrackFlatTableProducer",
    skipNonExistingSrc=cms.bool(True),
    src=cms.InputTag("electronGsfTracks"),
    cut=cms.string(""),
    name=cms.string("GsfTrack"),
    doc=cms.string("GSF tracks for electron/photon reconstruction in MLPF"),
    extension=cms.bool(False),
    variables=cms.PSet(
        # Track ID
        id=Var("key", "uint32", doc="GSF track index"),
        
        # Regular track parameters
        pt=Var("pt()", "float", doc="track p_T [GeV]"),
        px=Var("px()", "float", doc="track p_x [GeV]"),
        py=Var("py()", "float", doc="track p_y [GeV]"),
        pz=Var("pz()", "float", doc="track p_z [GeV]"),
        eta=Var("eta()", "float", doc="track pseudorapidity"),
        phi=Var("phi()", "float", doc="track phi angle [rad]"),
        charge=Var("charge()", "int", doc="track charge"),
        lambda_=Var("lambda()", "float", doc="track lambda = pi/2 - theta"),
        
        # Vertex position
        vx=Var("vx()", "float", doc="track vertex x [cm]"),
        vy=Var("vy()", "float", doc="track vertex y [cm]"),
        vz=Var("vz()", "float", doc="track vertex z [cm]"),
        
        # Hit information
        nhits=Var("recHitsSize()", "uint16", doc="number of RecHits"),
        missingOuterHits=Var("missingOuterHits()", "uint8", doc="number of missing outer hits"),
        missingInnerHits=Var("missingInnerHits()", "uint8", doc="number of missing inner hits"),
        quality=Var("qualityByName('highPurity')", "uint8", doc="track quality (1=highPurity, 0=other)"),
        
        # ── MODE parameters (KEY FOR MLPF MODEL) ──
        # MODE gives most probable value accounting for bremsstrahlung (essential for electrons)
        ptMode=Var("ptMode()", "float", doc="most probable track p_T [GeV]"),
        ptModeError=Var("ptModeError()", "float", doc="most probable p_T error [GeV]"),
        pxMode=Var("pxMode()", "float", doc="most probable p_x [GeV]"),
        pyMode=Var("pyMode()", "float", doc="most probable p_y [GeV]"),
        pzMode=Var("pzMode()", "float", doc="most probable p_z [GeV]"),
        pMode=Var("pMode()", "float", doc="most probable momentum magnitude [GeV]"),
        etaMode=Var("etaMode()", "float", doc="most probable eta"),
        etaModeError=Var("etaModeError()", "float", doc="most probable eta error"),
        phiMode=Var("phiMode()", "float", doc="most probable phi [rad]"),
        phiModeError=Var("phiModeError()", "float", doc="most probable phi error"),
        
        # Error parameters (used in MLPF for uncertainty estimation)
        ptErr=Var("ptError()", "float", doc="track p_T error [GeV]"),
        etaErr=Var("etaError()", "float", doc="track eta error"),
        phiErr=Var("phiError()", "float", doc="track phi error"),
        qoverpError=Var("qoverpError()", "float", doc="q/p error"),
        qoverpModeError=Var("qoverpModeError()", "float", doc="most probable q/p error"),
        lambdaMode=Var("lambdaMode()", "float", doc="most probable lambda"),
        lambdaModeError=Var("lambdaModeError()", "float", doc="most probable lambda error"),
        thetaMode=Var("thetaMode()", "float", doc="most probable theta"),
        thetaModeError=Var("thetaModeError()", "float", doc="most probable theta error"),
    ),
)

# Sequence for GSF tracks
hgcalGsfTracksTableSequence = cms.Sequence(gsfTracksTable)
