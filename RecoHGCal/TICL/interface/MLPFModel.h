#ifndef RecoHGCal_TICL_interface_MLPFModel_h
#define RecoHGCal_TICL_interface_MLPFModel_h

// Constants, feature layout, and output-decoding helpers for the MLPF (Machine Learning
// Particle Flow) model integration in TICL.
//
// The feature order, output layout, and decode arithmetic here MUST stay in lockstep with
// the training-side code: mlpf/model/mlpf.py (MLPF.forward) and
// ticl_workflow/postprocessing_ticl_ttbar_nopu.py (elem_branches / particle_feature_order).
// If either changes, this file needs to change with it.

#include <array>
#include <cstdint>

namespace ticl {
  namespace mlpf {

    // ------------------------------------------------------------------------------------
    // Input feature layout (35 features per element), after dropping ts_time/ts_time_err
    // from the raw dumper branches. Order matters -- this is the exact column order the
    // network was trained on.
    // ------------------------------------------------------------------------------------
    constexpr unsigned int NUM_ELEMENT_FEATURES = 35;

    enum FeatureIndex {
      kTypIdx = 0,
      kPt = 1,
      kEta = 2,
      kSinPhi = 3,
      kCosPhi = 4,
      kEnergy = 5,
      kCharge = 6,
      kPx = 7,
      kPy = 8,
      kPz = 9,
      kEmEnergy = 10,
      kBaryZ = 11,
      kNhits = 12,
      kMinDrTrack = 13,
      kNearTrackPt = 14,
      kShowerDepth = 15,
      kSumPtDr10 = 16,
      kNTrkDr01 = 17,
      kNTrkDr02 = 18,
      kNTrkDr03 = 19,
      kNTrkDr04 = 20,
      kNTrkDr05 = 21,
      kNClusters = 22,
      kTrackMuonType = 23,
      kTrackMuonDtHits = 24,
      kTrackMuonCscHits = 25,
      kTrackGsfType = 26,
      kTrackPtErr = 27,
      kTrackEtaErr = 28,
      kTrackPhiErr = 29,
      kTrackLambdaErr = 30,
      kTrackQoverpErr = 31,
      kTrackVx = 32,
      kTrackVy = 33,
      kTrackVz = 34,
    };
    static_assert(kTrackVz + 1 == static_cast<int>(NUM_ELEMENT_FEATURES), "feature index/count mismatch");

    // Element "typ" codes -- the value stored in kTypIdx, must match the training-side
    // convention (elem_branches "typ" / make_graph() typ assignment).
    enum ElementType { kTrack = 1, kEmTrackster = 2, kHadTrackster = 3, kGsfTrack = 4 };

    // ------------------------------------------------------------------------------------
    // Output layout
    // ------------------------------------------------------------------------------------
    // 6-class PID head: [0=none, 1=ch.had, 2=n.had, 3=gamma, 4=ele, 5=mu]
    constexpr unsigned int NUM_PID_CLASSES = 6;
    enum PredictedClass { kNone = 0, kChHad = 1, kNHad = 2, kGamma = 3, kEle = 4, kMu = 5 };

    // Binary "is there a particle at all" head: 2 logits, [no-particle, particle]
    constexpr unsigned int NUM_BINARY_CLASSES = 2;

    // Momentum head, 5 raw values per element, in this order (see MLPF.forward's
    // preds_momentum = cat([preds_pt, preds_eta, preds_sin_phi, preds_cos_phi, preds_energy])):
    constexpr unsigned int NUM_MOMENTUM_FEATURES = 5;
    enum MomentumIndex { kRawPt = 0, kRawEta = 1, kRawSinPhi = 2, kRawCosPhi = 3, kRawEnergy = 4 };

    // Single flat feature vector for one element, in network input order.
    struct ElementFeatures {
      std::array<float, NUM_ELEMENT_FEATURES> values{};
    };

    // Decoded, physical momentum for one candidate.
    struct DecodedMomentum {
      float pt = 0.f;
      float eta = 0.f;
      float sin_phi = 0.f;
      float cos_phi = 0.f;
      float energy = 0.f;
    };

    // Applies the exact decode transform used at the end of MLPF.forward():
    //   pt_final     = exp(raw_pt)     * input_pt      (pt_mode = "direct-elemtype-split": log-ratio output)
    //   eta_final    = raw_eta                          (eta_mode = "linear": already an absolute value,
    //                                                     the input eta is folded in on the training side)
    //   sin/cos_phi  = raw_sin_phi / raw_cos_phi         (same "linear" mode, already absolute)
    //   energy_final = exp(raw_energy) * input_energy    (energy_mode = "direct-elemtype-split")
    //
    // raw_momentum must point at NUM_MOMENTUM_FEATURES contiguous floats for this element,
    // in MomentumIndex order. input_pt/input_energy are the *input* feature values (i.e.
    // ElementFeatures::values[kPt] / [kEnergy]) fed to the network for this same element --
    // NOT the decoded output of some other element.
    inline DecodedMomentum decodeMomentum(const float* raw_momentum, float input_pt, float input_energy) {
      DecodedMomentum out;
      out.pt = std::exp(raw_momentum[kRawPt]) * input_pt;
      out.eta = raw_momentum[kRawEta];
      out.sin_phi = raw_momentum[kRawSinPhi];
      out.cos_phi = raw_momentum[kRawCosPhi];
      out.energy = std::exp(raw_momentum[kRawEnergy]) * input_energy;
      return out;
    }

    // argmax over a NUM_PID_CLASSES-length contiguous span of PID scores/logits.
    inline int argmaxPidClass(const float* pid_scores) {
      int best = 0;
      float best_val = pid_scores[0];
      for (unsigned int i = 1; i < NUM_PID_CLASSES; ++i) {
        if (pid_scores[i] > best_val) {
          best_val = pid_scores[i];
          best = static_cast<int>(i);
        }
      }
      return best;
    }

    // Maps a PredictedClass + signed charge to a PDG id, following the same
    // "base_pdgid * charge" convention already used throughout TICLCandidate.h /
    // TICLCandidateProducer.cc (no antiparticle-sign special-casing beyond that).
    // Charge is expected to be -1, 0, or +1.
    inline int pdgIdFromClass(int predicted_class, int charge) {
      switch (predicted_class) {
        case kChHad:
          return 211 * charge;
        case kNHad:
          return 130;
        case kGamma:
          return 22;
        case kEle:
          return 11 * charge;
        case kMu:
          return 13 * charge;
        default:
          return 0;
      }
    }

  }  // namespace mlpf
}  // namespace ticl

#endif
