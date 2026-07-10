import FWCore.ParameterSet.Config as cms
from PhysicsTools.NanoAOD.common_cff import *

# General Tracks table for NanoAOD - optimized for MLPF with exactly the features needed
generalTracksTable = cms.EDProducer(
    "SimpleTriggerTrackFlatTableProducer",
    skipNonExistingSrc=cms.bool(True),
    src=cms.InputTag("generalTracks"),
    cut=cms.string(""),
    name=cms.string("GeneralTrack"),
    doc=cms.string("General reconstructed tracks for MLPF"),
    extension=cms.bool(False),
    variables=cms.PSet(
        # Basic kinematics (used in make_graph)
        pt=Var("pt()", "float", doc="track p_T [GeV]"),
        p=Var("p()", "float", doc="track momentum magnitude [GeV]"),
        eta=Var("eta()", "float", doc="track pseudorapidity"),
        phi=Var("phi()", "float", doc="track phi angle [rad]"),
        charge=Var("charge()", "int", doc="track charge"),
        
        # Position (used for track vertex)
        vx=Var("vx()", "float", doc="track vertex x [cm]"),
        vy=Var("vy()", "float", doc="track vertex y [cm]"),
        vz=Var("vz()", "float", doc="track vertex z [cm]"),
        
        # Quality metrics
        quality=Var("qualityByName('highPurity')", "uint8", doc="track quality (1=highPurity, 0=other)"),
        nhits=Var("numberOfValidHits()", "uint16", doc="number of valid hits"),
        missingOuterHits=Var("missingOuterHits()", "uint8", doc="number of missing outer hits"),
        
        # Muon association info (used for PF)
        muonType=Var("pt", "int", doc="muon type (will be filled by TICLDumper logic)"),
        muonDtHits=Var("pt", "int", doc="muon DT hits (will be filled by TICLDumper logic)"),
        muonCscHits=Var("pt", "int", doc="muon CSC hits (will be filled by TICLDumper logic)"),
        
        # GSF and electron tracking
        gsfType=Var("pt", "uint8", doc="GSF track association flag (0=no GSF, 1=has GSF)"),
        
        # Error parameters (for uncertainty estimation in MLPF)
        ptErr=Var("ptError()", "float", doc="track p_T error [GeV]"),
        etaErr=Var("etaError()", "float", doc="track eta error"),
        phiErr=Var("phiError()", "float", doc="track phi error"),
        lambdaErr=Var("lambdaError()", "float", doc="track lambda error"),
        qoverpErr=Var("qoverpError()", "float", doc="q/p error"),
    ),
)

# Sequence for general tracks
hgcalGeneralTracksTableSequence = cms.Sequence(generalTracksTable)
