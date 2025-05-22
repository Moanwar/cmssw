#ifndef RecoHGCal_TICL_DeltaREstimator_H
#define RecoHGCal_TICL_DeltaREstimator_H

#include <vector>
#include <string>

#include "DataFormats/TrackReco/interface/TrackFwd.h"
#include "DataFormats/TrackReco/interface/Track.h"
#include "DataFormats/GeometryVector/interface/GlobalPoint.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "PhysicsTools/XGBoost/interface/XGBooster.h"

namespace ticl {

  class DeltaREstimator {
  public:
    DeltaREstimator();  // constructor will load model
    ~DeltaREstimator();

    // Optional: allow loading model dynamically
    void loadModel(const std::string& modelPath);

    float evaluateDeltaR(const reco::Track& tk, reco::TrackRef trackref,
                         const GlobalPoint& tsosPos, const GlobalVector& tsosMom,
                         const AlgebraicMatrix55& localErrMatrix,
			 const float &time_mtd_trk, const float &beta_trk, const float &time_quality_trk, const GlobalPoint &tkMtdPos);


  private:
    BoosterHandle booster_;
    std::vector<float> featureBuffer_;
    bool modelLoaded_;
  };

}  // namespace ticl

#endif
