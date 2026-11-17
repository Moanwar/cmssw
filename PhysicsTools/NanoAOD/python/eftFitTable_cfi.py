import FWCore.ParameterSet.Config as cms

eftFitTable = cms.EDProducer("EFTFitTableProducer",
    lheInfo = cms.VInputTag(cms.InputTag("externalLHEProducer"), cms.InputTag("source")),
    precision = cms.int32(14),
)

eftFitTableTask = cms.Task(eftFitTable)
