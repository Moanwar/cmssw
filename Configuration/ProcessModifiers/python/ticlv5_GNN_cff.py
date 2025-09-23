import FWCore.ParameterSet.Config as cms

from Configuration.ProcessModifiers.ticl_v5_cff import ticl_v5

# This modifier is for running TICL v5 with GNN linking.
ticlv5gnn =  cms.Modifier()
ticlv5_GNN = cms.ModifierChain(ticl_v5, ticlv5gnn)
