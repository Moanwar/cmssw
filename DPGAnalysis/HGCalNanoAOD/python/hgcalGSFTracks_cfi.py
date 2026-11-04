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

# reco::GsfElectron, one level up from the plain GsfTrack: carries the electron-ID DNN
# scores and the track<->SuperCluster matching quantities that live only on the
# electron candidate, never on reco::GsfTrack itself. gedGsfElectronsTmp is the exact
# collection RecoParticleFlow/PFProducer/plugins/MLPFProducer.cc itself reads for these
# same quantities (hardcoded InputTag there, not configurable), so this table is built
# from the same source MLPF's own gsf_electronseed_dnn1..5 come from. Isolation-sum
# variables (dr03/dr04 cone sums) are deliberately left out here: a separate, heavier
# topic (activity around the electron) from the identification/kinematic features below.
gsfElectronsTable = cms.EDProducer(
    "SimpleGsfElectronFlatTableProducer",
    skipNonExistingSrc=cms.bool(True),
    src=cms.InputTag("gedGsfElectronsTmp"),
    cut=cms.string(""),
    name=cms.string("GsfElectron"),
    doc=cms.string("GSF electrons (gedGsfElectronsTmp)"),
    extension=cms.bool(False),
    variables=cms.PSet(
        # Basic kinematics
        pt=Var("pt()", "float", doc="GSF electron p_T [GeV]"),
        eta=Var("eta()", "float", doc="GSF electron pseudorapidity"),
        phi=Var("phi()", "float", doc="GSF electron phi angle [rad]"),
        energy=Var("energy()", "float", doc="GSF electron energy [GeV]"),
        charge=Var("charge()", "int", doc="GSF electron charge"),
        vx=Var("vx()", "float", doc="GSF electron vertex x [cm]"),
        vy=Var("vy()", "float", doc="GSF electron vertex y [cm]"),
        vz=Var("vz()", "float", doc="GSF electron vertex z [cm]"),

        # Link back to the GSFTrack table (electronGsfTracks): same collection
        # gedGsfElectronsTmp's own GsfTrackRef points into, so .key() is the row index
        # directly, the same way MLPFModel.cc matches "ref == gsfEle.gsfTrack()".
        gsfTrackIndex=Var("gsfTrack().key()", "int", doc="Index into the GSFTrack table"),

        # Electron-ID DNN scores (RecoEgamma/EgammaElectronProducers' GsfElectronProducer),
        # the quantities MLPFModel.cc's gsf_electronseed_dnn1..5 are read from directly.
        dnnSignalIsolated=Var("dnn_signal_Isolated()", "float", doc="Electron-ID DNN: signal, isolated"),
        dnnSignalNonIsolated=Var("dnn_signal_nonIsolated()", "float", doc="Electron-ID DNN: signal, non-isolated"),
        dnnBkgNonIsolated=Var("dnn_bkg_nonIsolated()", "float", doc="Electron-ID DNN: background, non-isolated"),
        dnnBkgTau=Var("dnn_bkg_Tau()", "float", doc="Electron-ID DNN: background, tau"),
        dnnBkgPhoton=Var("dnn_bkg_Photon()", "float", doc="Electron-ID DNN: background, photon"),

        # Seeding and charge consistency
        ecalDrivenSeed=Var("ecalDrivenSeed()", "bool", doc="Electron seed found by the ECAL-driven algorithm"),
        trackerDrivenSeed=Var("trackerDrivenSeed()", "bool", doc="Electron seed found by the tracker-driven algorithm"),
        scPixCharge=Var("scPixCharge()", "int", doc="Charge estimate from the SuperCluster/pixel-hit pattern"),
        isGsfCtfChargeConsistent=Var("isGsfCtfChargeConsistent()", "bool",
            doc="GSF and CTF track charges agree"),
        isGsfScPixChargeConsistent=Var("isGsfScPixChargeConsistent()", "bool",
            doc="GSF track and SC/pixel charge estimate agree"),
        isGsfCtfScPixChargeConsistent=Var("isGsfCtfScPixChargeConsistent()", "bool",
            doc="GSF, CTF and SC/pixel charge estimates all agree"),

        # Track<->cluster matching, the standard electron-identification shower-shape
        # variables, evaluated at the GSF track's innermost (vertex) and outermost
        # (calo-extrapolated) states.
        eSuperClusterOverP=Var("eSuperClusterOverP()", "float",
            doc="SuperCluster energy / track momentum at the PCA to the beam spot"),
        eSeedClusterOverP=Var("eSeedClusterOverP()", "float",
            doc="Seed cluster energy / track momentum at the PCA to the beam spot"),
        eSeedClusterOverPout=Var("eSeedClusterOverPout()", "float",
            doc="Seed cluster energy / track momentum at calo, from the outermost track state"),
        eEleClusterOverPout=Var("eEleClusterOverPout()", "float",
            doc="Electron cluster energy / track momentum at calo, from the outermost track state"),
        deltaEtaSuperClusterTrackAtVtx=Var("deltaEtaSuperClusterTrackAtVtx()", "float",
            doc="SuperCluster eta - track eta at calo, from the innermost track state"),
        deltaPhiSuperClusterTrackAtVtx=Var("deltaPhiSuperClusterTrackAtVtx()", "float",
            doc="SuperCluster phi - track phi at calo, from the innermost track state"),
        deltaEtaSeedClusterTrackAtCalo=Var("deltaEtaSeedClusterTrackAtCalo()", "float",
            doc="Seed cluster eta - track eta at calo, from the outermost track state"),
        deltaPhiSeedClusterTrackAtCalo=Var("deltaPhiSeedClusterTrackAtCalo()", "float",
            doc="Seed cluster phi - track phi at calo, from the outermost track state"),
        deltaEtaEleClusterTrackAtCalo=Var("deltaEtaEleClusterTrackAtCalo()", "float",
            doc="Electron cluster eta - track eta at calo, from the outermost track state"),
        deltaPhiEleClusterTrackAtCalo=Var("deltaPhiEleClusterTrackAtCalo()", "float",
            doc="Electron cluster phi - track phi at calo, from the outermost track state"),

        # Fiducial region
        isEB=Var("isEB()", "bool", doc="In the ECAL barrel"),
        isEE=Var("isEE()", "bool", doc="In the ECAL endcap"),
        isGap=Var("isGap()", "bool", doc="In a barrel/endcap or intra-module crack"),
    ),
)

# Sequence for gsf tracks
hgcalGSFTracksTableSequence = cms.Sequence(gsfTrackPVInfo + gsfTracksTable + gsfElectronsTable)
