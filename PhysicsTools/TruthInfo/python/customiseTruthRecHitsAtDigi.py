# DIGI-step customise: run pixel local reconstruction (clusterizer +
# siPixelRecHitsPreSplitting, CPE already applied) right after digitisation, enable the
# DIGI-built MC-truth graph (what the enableTruth modifier wires into pdigiTask and the
# mixing digitizers), associate the pixel rechits to it directly
# (truthBranchSiPixelRecHitAssociators, no track required since the rechit is matched on
# its own cluster cells), and persist all three - rechits, merged simHits and the
# association maps - in the step2 output.
#
# Apply with cmsDriver's
#   --customise PhysicsTools.TruthInfo.customiseTruthRecHitsAtDigi.customise
# appended to the step2 (DIGI,L1TrackTrigger,...,DIGI2RAW,HLT) command, or by hand:
#   from PhysicsTools.TruthInfo.customiseTruthRecHitsAtDigi import customise
#   process = customise(process)
#
# Pixel only: for this era the outer tracker produces no rechit by default (siPhase2RecHits
# only exists under the phase2CAExtension modifier), so its channel is left out; tracking
# downstream still has the full truth graph via the Tracker channel of the hit index.

import FWCore.ParameterSet.Config as cms


def customise(process):
    # 1) The DIGI-built truth graph: register the pileup-aware accumulator in the mixing
    # digitizers (what (enableTruth & phase2_hgcal).toModify(...) does in
    # SimGeneral/MixingModule/python/digitizers_cfi.py), then build the logical graph +
    # the unresolved per-particle per-cell hit index right after mixing, from the merged
    # simHits the accumulator produces.
    from PhysicsTools.TruthInfo.truthGraphMixedDigi_cff import (
        truthGraphAccumulator,
        truthLogicalGraphProducer,
        truthLogicalGraphHitIndexProducer,
        truthGraphMixedDigiTask,
    )

    process.mix.digitizers.truthGraph = truthGraphAccumulator
    process.truthLogicalGraphProducer = truthLogicalGraphProducer
    process.truthLogicalGraphHitIndexProducer = truthLogicalGraphHitIndexProducer
    process.truthGraphMixedDigiTask = truthGraphMixedDigiTask
    process.pdigiTask.add(process.truthGraphMixedDigiTask)

    # 2) Pixel local reconstruction. The Phase-2 pixel clusterizer already reads
    # simSiPixelDigis directly (there is no RAW2DIGI in this step to produce siPixelDigis
    # instead), so the standard sequence runs unmodified.
    #
    # pixeltrackerlocalrecoTask holds the two legacy-format producers
    # (siPixelClustersPreSplitting, siPixelRecHitsPreSplitting - the same attribute
    # names whether alpaka.toReplaceWith swapped their implementation or not) plus their
    # upstream Alpaka SoA steps. The Task alone is unscheduled: a module in it runs only
    # if something's static (exact-type) consumes<> is seen to need it, and our
    # associator's edm::View<SiPixelRecHit> is resolved against the producer's actual
    # edmNew::DetSetVector<SiPixelRecHit> type only at runtime, too late for that
    # analysis - so without a concrete chain here, siPixelRecHitsPreSplitting is pruned
    # before the job ever runs. Scheduling the two producers directly (the Task is still
    # passed, for their own upstream Alpaka dependencies, which real exact-type consumes
    # calls resolve normally) makes them unconditional.
    # RecoLocalTracker_cff carries the clusterizer and rechit-converter modules but not
    # the pixel CPE ESProducers (PixelCPEGeneric etc.): a real RECO job gets those
    # transitively from elsewhere in Reconstruction_cff, which this DIGI-step process
    # never loads. Without them SiPixelRecHitConverter logs "at least one CPE is not
    # ready" and silently puts nothing, no exception - load them explicitly.
    process.load('RecoLocalTracker.SiPixelRecHits.PixelCPEESProducers_cff')
    process.load('RecoLocalTracker.Configuration.RecoLocalTracker_cff')
    process.pixelLocalReco_step = cms.Path(
        process.siPixelClustersPreSplitting + process.siPixelRecHitsPreSplitting,
        process.pixeltrackerlocalrecoTask,
    )

    # 3) The new rechit<->truth-branch association, directly on siPixelRecHitsPreSplitting
    # (truthBranchTargets + truthBranchSiPixelRecHitAssociators; see
    # SimGeneral/TruthGraphAssociatorProducers/python/truthGraphAssociators_cff.py). The
    # other domains that cff also defines (tracks, vertices, tracksters, pfClusters) have
    # no input at this step and are left off the schedule; they are pruned by
    # deleteNonConsumedUnscheduledModules, already set in this process's options.
    process.load('SimGeneral.TruthGraphAssociatorProducers.truthGraphAssociators_cff')
    process.truthGraphDigiAssoc_step = cms.Path(process.truthGraphDigiAssociatorsSequence)

    # Right after digitisation_step (index 0): both new paths need only pdigiTask's
    # output, which now includes the truth graph and feeds the pixel rechits.
    process.schedule.insert(1, process.pixelLocalReco_step)
    process.schedule.insert(2, process.truthGraphDigiAssoc_step)

    # 4) Event content: the compact truth (logical graph + hit index + raw merged graph),
    # the merged simHits ('full' level, tracking included - this is the SimHit side of
    # the association), the pixel rechits themselves, and the new association maps.
    from PhysicsTools.TruthInfo.truthEventContent_cff import setTruthEventContent

    setTruthEventContent(process, level='full', includeTrackingHits=True)
    process.FEVTDEBUGHLToutput.outputCommands.extend([
        'keep *_siPixelRecHitsPreSplitting_*_*',
        'keep *_truthBranchTargets_*_*',
        'keep *_truthBranchSiPixelRecHitAssociators_*_*',
    ])

    return process
