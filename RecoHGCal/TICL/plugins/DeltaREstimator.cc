#include "RecoHGCal/TICL/interface/DeltaREstimator.h"
#include <cmath>


namespace ticl {

  DeltaREstimator::DeltaREstimator() : modelLoaded_(false) {
    std::string modelPath = "/eos/user/m/moanwar/ticlv5/linking/CMSSW_15_0_0_pre1/src/RecoHGCal/TICL/data/bdt/BDT1_Bayes.json";
    loadModel(modelPath);
}
  
void DeltaREstimator::loadModel(const std::string& modelPath) {
  if (modelLoaded_) {
    XGBoosterFree(booster_);
    modelLoaded_ = false;
  }
  
  int status = XGBoosterCreate(nullptr, 0, &booster_);
  if (status != 0) {
   throw std::runtime_error("XGBoosterCreate failed: " + std::string(XGBGetLastError()));
  }
  
  status = XGBoosterLoadModel(booster_, modelPath.c_str());
  if (status != 0) {
    XGBoosterFree(booster_);
   throw std::runtime_error("XGBoosterLoadModel failed: " + std::string(XGBGetLastError()));
  }
  
  modelLoaded_ = true;
}
  
  DeltaREstimator::~DeltaREstimator() {
    if (modelLoaded_) {
      XGBoosterFree(booster_);
    }
  }
  
  float DeltaREstimator::evaluateDeltaR(
					const reco::Track& tk,
					reco::TrackRef trackref,
					const GlobalPoint& tsosPos,
					const GlobalVector& tsosMom,
					const AlgebraicMatrix55& localErrMatrix,
					const float &time_mtd_trk, const float &beta_trk, const float &time_quality_trk, const GlobalPoint &tkMtdPos) {
    
    const float x = tsosPos.x();
    const float y = tsosPos.y();
    const float z = tsosPos.z();

    AlgebraicMatrix22 covMatrixXY;
    covMatrixXY(0,0) = localErrMatrix(3,3);
    covMatrixXY(0,1) = localErrMatrix(3,4);
    covMatrixXY(1,0) = localErrMatrix(3,4);
    covMatrixXY(1,1) = localErrMatrix(4,4);

    const double sqrt_term = std::sqrt((x*x + y*y) / (z*z) + 1);
    const double denom_eta = (x*x + y*y) * (x*x + y*y + z*z);
    const double denom_phi = x*x + y*y;

    AlgebraicMatrix22 jacobian;
    jacobian(0,0) = - (x * z * z * sqrt_term) / denom_eta;
    jacobian(0,1) = - (y * z * z * sqrt_term) / denom_eta;
    jacobian(1,0) = - y / denom_phi;
    jacobian(1,1) =   x / denom_phi;

    AlgebraicMatrix22 covMatrixEtaPhi = ROOT::Math::Transpose(jacobian) * covMatrixXY * jacobian;

    const float eta_trk = tk.eta();
    //const float phitrk = tk.phi();
    const float pt_trk  = tk.pt();
    const float en_trk  = tk.p();

    //const float etaErr = std::sqrt(covMatrixEtaPhi(0,0)) * 1.5f;
    const float phiErr = std::sqrt(covMatrixEtaPhi(1,1)) * 1.5f;
    //const float etaphiCov = covMatrixEtaPhi(0,1) * 1.5f * 1.5f;

    //const float deltaR_trk = std::sqrt(phiErr * phiErr + etaErr * etaErr);

    const float hgcal_xErr_trk = std::sqrt(covMatrixXY(0,0));
    const float hgcal_yErr_trk = std::sqrt(covMatrixXY(1,1));
    //const float xyCov = std::sqrt(covMatrixXY(0,1));

    const float outer_hits_trk = tk.missingOuterHits();
    //const int inner_hits = tk.missingInnerHits();
    const float nhits_trk = tk.recHitsSize();
    const float hgcal_pt_trk = tsosMom.perp();
    const float y_mtd_trk = tkMtdPos.x();
    const float x_mtd_trk = tkMtdPos.y();

    featureBuffer_ = {
      eta_trk, phiErr, en_trk, pt_trk, hgcal_pt_trk,
      beta_trk, hgcal_xErr_trk, outer_hits_trk,
      hgcal_yErr_trk, time_quality_trk, y_mtd_trk, time_mtd_trk, x_mtd_trk, nhits_trk
    };

    // Print all feature values for inspection
    //std::cout << "[DEBUG] Feature values (" << featureBuffer_.size() << "): ";
    //for (const auto& val : featureBuffer_) {
    //  std::cout << val << " ";
    //}
    //std::cout << std::endl;
    
    // Create DMatrix
    DMatrixHandle dmat;
    int status = XGDMatrixCreateFromMat(featureBuffer_.data(), 1, featureBuffer_.size(), -999.f, &dmat);
    if (status != 0) {
      std::cerr << "[ERROR] Failed to create DMatrix: " << XGBGetLastError() << std::endl;
       return -1.f;
    } else {
      std::cout << "[DEBUG] DMatrix created successfully." << std::endl;
    }
    
    // Predict using the booster
    bst_ulong outLen = 0;
    const float* outResult = nullptr;
    
    int pred_status = XGBoosterPredict(booster_, dmat, 0, 0, 0, &outLen, &outResult);
     if (pred_status != 0) {
      std::cerr << "[ERROR] Prediction failed: " << XGBGetLastError() << std::endl;
      XGDMatrixFree(dmat);
       return -1.f;
    }
    
    // Debug prediction output
    std::cout << "[DEBUG] Prediction output length: " << outLen << std::endl;
    if (outResult && outLen > 0) {
      for (bst_ulong i = 0; i < outLen; ++i) {
    	std::cout << "[DEBUG] Prediction[" << i << "] = " << outResult[i] << std::endl;
    }
    } else {
     std::cerr << "[WARNING] Prediction output is empty or invalid." << std::endl;
    }
    
    // Free memory
    XGDMatrixFree(dmat);
    
    // Return first prediction if available
    return (outResult && outLen > 0) ? outResult[0] : -1.f;
  }

}  // namespace ticl

