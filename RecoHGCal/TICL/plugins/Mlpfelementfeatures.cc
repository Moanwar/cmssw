#include "RecoHGCal/TICL/interface/MLPFElementFeatures.h"

#include <cmath>

#include "DataFormats/Math/interface/deltaR.h"

namespace ticl {
  namespace mlpf {

    TracksterNeighborFeatures computeTracksterNeighborFeatures(float trackster_eta,
                                                                 float trackster_phi,
                                                                 const std::vector<reco::Track>& tracks) {
      TracksterNeighborFeatures out;
      for (const auto& trk : tracks) {
        // matches the `good = (trk_pt >= 1) & (trk_qual >= 1)` filter in
        // compute_trackster_features() -- quality>=1 corresponds to at least "loose".
        if (trk.pt() < 1.f || trk.qualityMask() < 1)
          continue;

        const float dR = reco::deltaR(trackster_eta, trackster_phi, trk.eta(), trk.phi());

        if (dR < out.min_dR_track) {
          out.min_dR_track = dR;
          out.near_track_pt = trk.pt();
        }
        if (dR < 0.10f)
          out.sum_pt_dR10 += trk.pt();
        if (dR < 0.01f)
          out.n_trk_dR01 += 1.f;
        if (dR < 0.02f)
          out.n_trk_dR02 += 1.f;
        if (dR < 0.03f)
          out.n_trk_dR03 += 1.f;
        if (dR < 0.04f)
          out.n_trk_dR04 += 1.f;
        if (dR < 0.05f)
          out.n_trk_dR05 += 1.f;
      }
      return out;
    }

    ElementFeatures buildTrackFeatures(const reco::Track& track, const reco::Muon* matchedMuon, bool hasGsfMatch) {
      ElementFeatures f;
      auto& v = f.values;

      v[kTypIdx] = static_cast<float>(kTrack);
      v[kPt] = track.pt();
      v[kEta] = track.eta();
      v[kSinPhi] = std::sin(track.phi());
      v[kCosPhi] = std::cos(track.phi());
      v[kEnergy] = track.p();  // matches track_p / "energy" convention used for typ=1 in training
      v[kCharge] = static_cast<float>(track.charge());
      v[kPx] = track.px();
      v[kPy] = track.py();
      v[kPz] = track.pz();
      v[kEmEnergy] = 0.f;
      v[kBaryZ] = 0.f;
      v[kNhits] = static_cast<float>(track.recHitsSize());
      v[kMinDrTrack] = 0.f;
      v[kNearTrackPt] = 0.f;
      v[kShowerDepth] = 0.f;
      v[kSumPtDr10] = 0.f;
      v[kNTrkDr01] = 0.f;
      v[kNTrkDr02] = 0.f;
      v[kNTrkDr03] = 0.f;
      v[kNTrkDr04] = 0.f;
      v[kNTrkDr05] = 0.f;
      v[kNClusters] = 0.f;

      if (matchedMuon != nullptr) {
        v[kTrackMuonType] = static_cast<float>(matchedMuon->type());
        if (matchedMuon->standAloneMuon().isNonnull()) {
          v[kTrackMuonDtHits] = static_cast<float>(matchedMuon->standAloneMuon()->hitPattern().numberOfValidMuonDTHits());
          v[kTrackMuonCscHits] =
              static_cast<float>(matchedMuon->standAloneMuon()->hitPattern().numberOfValidMuonCSCHits());
        }
      }
      v[kTrackGsfType] = hasGsfMatch ? 1.f : 0.f;

      v[kTrackPtErr] = track.ptError();
      v[kTrackEtaErr] = track.etaError();
      v[kTrackPhiErr] = track.phiError();
      v[kTrackLambdaErr] = track.lambdaError();
      v[kTrackQoverpErr] = track.qoverpError();
      v[kTrackVx] = track.vx();
      v[kTrackVy] = track.vy();
      v[kTrackVz] = track.vz();

      return f;
    }

    ElementFeatures buildGsfTrackFeatures(const reco::GsfTrack& gsfTrack) {
      ElementFeatures f;
      auto& v = f.values;

      v[kTypIdx] = static_cast<float>(kGsfTrack);
      v[kPt] = gsfTrack.ptMode();
      v[kEta] = gsfTrack.etaMode();
      v[kSinPhi] = std::sin(gsfTrack.phiMode());
      v[kCosPhi] = std::cos(gsfTrack.phiMode());
      v[kEnergy] = gsfTrack.pMode();
      v[kCharge] = static_cast<float>(gsfTrack.charge());
      v[kPx] = gsfTrack.pxMode();
      v[kPy] = gsfTrack.pyMode();
      v[kPz] = gsfTrack.pzMode();
      v[kEmEnergy] = 0.f;
      v[kBaryZ] = 0.f;
      v[kNhits] = static_cast<float>(gsfTrack.numberOfValidHits());
      v[kMinDrTrack] = 0.f;
      v[kNearTrackPt] = 0.f;
      v[kShowerDepth] = 0.f;
      v[kSumPtDr10] = 0.f;
      v[kNTrkDr01] = 0.f;
      v[kNTrkDr02] = 0.f;
      v[kNTrkDr03] = 0.f;
      v[kNTrkDr04] = 0.f;
      v[kNTrkDr05] = 0.f;
      v[kNClusters] = 0.f;
      v[kTrackMuonType] = 0.f;
      v[kTrackMuonDtHits] = 0.f;
      v[kTrackMuonCscHits] = 0.f;
      v[kTrackGsfType] = 1.f;
      v[kTrackPtErr] = gsfTrack.ptModeError();
      v[kTrackEtaErr] = gsfTrack.etaModeError();
      v[kTrackPhiErr] = gsfTrack.phiModeError();
      v[kTrackLambdaErr] = 0.f;  // not exposed as *Mode* error on GsfTrack; training used 0.0 as well
      v[kTrackQoverpErr] = gsfTrack.qoverpModeError();
      v[kTrackVx] = gsfTrack.vx();
      v[kTrackVy] = gsfTrack.vy();
      v[kTrackVz] = gsfTrack.vz();

      return f;
    }

    ElementFeatures buildTracksterFeatures(const ticl::Trackster& trackster,
                                            int elementType,
                                            const TracksterNeighborFeatures& neighbors) {
      ElementFeatures f;
      auto& v = f.values;

      const float eta = trackster.barycenter().eta();
      const float phi = trackster.barycenter().phi();
      const float pt = trackster.raw_pt();
      const float energy = trackster.raw_energy();
      const float theta = 2.f * std::atan(std::exp(-eta));

      v[kTypIdx] = static_cast<float>(elementType);
      v[kPt] = pt;
      v[kEta] = eta;
      v[kSinPhi] = std::sin(phi);
      v[kCosPhi] = std::cos(phi);
      v[kEnergy] = energy;
      v[kCharge] = 0.f;
      v[kPx] = pt * std::cos(phi);
      v[kPy] = pt * std::sin(phi);
      v[kPz] = energy * std::cos(theta);
      v[kEmEnergy] = trackster.raw_em_energy();
      v[kBaryZ] = trackster.barycenter().z();
      v[kNhits] = 0.f;

      v[kMinDrTrack] = neighbors.min_dR_track;
      v[kNearTrackPt] = neighbors.near_track_pt;
      // TODO: shower_depth in training is the energy-weighted |z| of the trackster's
      // constituent layer clusters (see compute_trackster_features()'s vertices_z/vertices_energy
      // weighted average), which needs the layer cluster collection to reproduce exactly.
      // Falling back to |barycenter().z()|, matching the python code's own exception fallback --
      // revisit once layer clusters are wired into this producer if this loses too much accuracy.
      v[kShowerDepth] = std::abs(trackster.barycenter().z());
      v[kSumPtDr10] = neighbors.sum_pt_dR10;
      v[kNTrkDr01] = neighbors.n_trk_dR01;
      v[kNTrkDr02] = neighbors.n_trk_dR02;
      v[kNTrkDr03] = neighbors.n_trk_dR03;
      v[kNTrkDr04] = neighbors.n_trk_dR04;
      v[kNTrkDr05] = neighbors.n_trk_dR05;
      v[kNClusters] = static_cast<float>(trackster.vertices().size());

      v[kTrackMuonType] = 0.f;
      v[kTrackMuonDtHits] = 0.f;
      v[kTrackMuonCscHits] = 0.f;
      v[kTrackGsfType] = 0.f;
      v[kTrackPtErr] = 0.f;
      v[kTrackEtaErr] = 0.f;
      v[kTrackPhiErr] = 0.f;
      v[kTrackLambdaErr] = 0.f;
      v[kTrackQoverpErr] = 0.f;
      v[kTrackVx] = 0.f;
      v[kTrackVy] = 0.f;
      v[kTrackVz] = 0.f;

      return f;
    }

  }  // namespace mlpf
}  // namespace ticl
