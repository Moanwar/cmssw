import FWCore.ParameterSet.Config as cms


def nanoAOD_customizeEFT(process):
    """Add the EFT fit coefficient tables to the NanoAOD sequence.

    Opt-in: only has an effect when applied explicitly, e.g.
        cmsDriver.py ... --customise PhysicsTools/NanoAOD/custom_eft_cff.nanoAOD_customizeEFT
    It schedules a dedicated producer (EFTFitTableProducer) and does not modify
    GenWeightsTableProducer or any other part of the default nanoSequence, so samples
    without EFT LHE weights (MJEFT_*/EFTrwgt*) are completely unaffected if this is not applied.

    The WC names are written once as the "title" of the nEFTfitCoefficients branch (see
    EFTFitTableProducer.cc) rather than as a separate per-event branch, since NanoAODOutputModule
    only ever persists FlatTable products and (as a hardcoded special case) a std::string product
    named "genModel" -- any other per-event string product is silently dropped regardless of
    outputCommands, so no output-side changes are needed here.
    """
    process.load('PhysicsTools.NanoAOD.eftFitTable_cfi')

    for seqName in ('nanoSequence', 'nanoSequenceMC', 'nanoSequenceFS'):
        if hasattr(process, seqName):
            seq = getattr(process, seqName)
            setattr(process, seqName, cms.Sequence(seq + cms.Sequence(process.eftFitTableTask)))

    return process
