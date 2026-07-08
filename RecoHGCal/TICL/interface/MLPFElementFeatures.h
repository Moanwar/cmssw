#ifndef RecoHGCal_TICL_interface_MLPFElementFeatures_h
#define RecoHGCal_TICL_interface_MLPFElementFeatures_h

// Feature extraction for the MLPF input tensor. Each function here fills one
// ElementFeatures (35 floats, see MLPFModel.h) from a single reco object, mirroring
// the corresponding feature-construction logic in
// ticl_workflow/postprocessing_ticl_ttbar_nopu.py (make_graph / compute_trackster_features).

#include <vector>

#include "DataFormats/TrackReco/interface/Track.h"
#include "DataFormats/GsfTrackReco/interface/GsfTrack.h"
#include "DataFormats/HGCalReco/interface/Trackster.h"
#include "DataFormats/MuonReco/interface/Muon.h"

#include "RecoHGCal/TICL/interface/MLPFModel.h"

namespace ticl {
  namespace mlpf {

    // Precomputed per-trackster neighbor-track features (min_dR_track, near_track_pt,
    // sum_pt_dR10, n_trk_dR01..05), matching compute_trackster_features() in the
    // postprocessing script. Computed once per event over all (trackster, track) pairs
    // rather than per-element, since it's an O(N_trackster * N_track) loop either way.
    struct TracksterNeighborFeatures {
      float min_dR_track = 99.f;
      float near_track_pt = 0.f;
      float sum_pt_dR10 = 0.f;
      float n_trk_dR01 = 0.f;
      float n_trk_dR02 = 0.f;
      float n_trk_dR03 = 0.f;
      float n_trk_dR04 = 0.f;
      float n_trk_dR05 = 0.f;
    };

    // tracks/trackQuality: only tracks passing pt>=1 && quality>=1 count as "near tracks",
    // matching the `good = (trk_pt >= 1) & (trk_qual >= 1)` filter in compute_trackster_features().
    TracksterNeighborFeatures computeTracksterNeighborFeatures(float trackster_eta,
                                                                 float trackster_phi,
                                                                 const std::vector<reco::Track>& tracks);

    // Fills the 35-feature vector for a single reco::Track (typ = kTrack).
    //
    // track_muon_type / track_muon_dt_hits / track_muon_csc_hits and track_gsf_type are NOT
    // native reco::Track quantities -- in the training-side TICLDumper they come from a
    // muon/GSF association done upstream (the same kind of lookup TICLCandidateProducer::
    // filterTracks does via PFMuonAlgo::muAssocToTrack). Callers must do that association
    // themselves and pass the result in here:
    //   matchedMuon : the associated reco::Muon, or nullptr if this track has no muon match
    //   hasGsfMatch : true if this track is associated to a GSF track (informs track_gsf_type)
    // TODO: confirm the exact track_gsf_type encoding (0/1 flag vs. seed-type enum) against
    // the training dumper before relying on this for electron discrimination.
    ElementFeatures buildTrackFeatures(const reco::Track& track, const reco::Muon* matchedMuon, bool hasGsfMatch);

    // Fills the 35-feature vector for a single reco::GsfTrack (typ = kGsfTrack).
    // gsf_pt/eta/phi/px/py/pz should be the "Mode" quantities (ptMode, etaMode, ...),
    // matching gsf_track_*Mode branches used in training.
    ElementFeatures buildGsfTrackFeatures(const reco::GsfTrack& gsfTrack);

    // Fills the 35-feature vector for a single Trackster (typ = kEmTrackster or
    // kHadTrackster depending on `elementType`). `neighbors` must be the result of
    // computeTracksterNeighborFeatures() for this same trackster.
    ElementFeatures buildTracksterFeatures(const ticl::Trackster& trackster,
                                            int elementType,
                                            const TracksterNeighborFeatures& neighbors);

  }  // namespace mlpf
}  // namespace ticl

#endif
