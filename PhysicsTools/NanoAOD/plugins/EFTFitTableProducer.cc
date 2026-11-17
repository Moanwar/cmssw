#include <memory>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <utility>
#include <atomic>

#include "FWCore/Framework/interface/global/EDProducer.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/Utilities/interface/InputTag.h"

#include "DataFormats/NanoAOD/interface/FlatTable.h"
#include "SimDataFormats/GeneratorProducts/interface/LHEEventProduct.h"

#include "boost/algorithm/string.hpp"

#include "PhysicsTools/NanoAOD/interface/EFTWCFit.h"
#include "PhysicsTools/NanoAOD/interface/EFTWCPoint.h"

// Opt-in table producer for EFT reweighting information, scheduled only when
// a sample actually carries EFT LHE weights (see PhysicsTools/NanoAOD/python/custom_eft_cff.py).
// It never touches the default GenWeightsTableProducer / nanoSequence path, so it has
// no effect whatsoever on standard (non-EFT) NanoAOD production.
//
// Two weight-ID conventions are understood, matching what MadGraph5_aMC@NLO (+ MadJax or
// standard EFT reweighting) writes into the LHE file:
//   - "MJEFT_<wc1>_<wc2>"   : MadJax analytic weight for the (wc1,wc2) quadratic term
//   - "EFTrwgt<N>_<wc>_<val>_<wc>_<val>_..." : a sampled reweighting point, fit to a
//                                              quadratic form via a least-squares fit
//                                              (see EFTWCFit.h/EFTWCPoint.h; self-contained,
//                                              no dependency on the external EFTGenReader package)
//
// Output, all per event:
//   - EFTfitCoefficients        (float, one row per quadratic term): the fit coefficients
//   - EFTfitCoefficientIndex1/2 (int,   one row per quadratic term): indices into the
//                                ordered WC name list, giving the pair of Wilson coefficients
//                                (or "sm") each term multiplies
// The WC names are constant for the whole sample, so rather than duplicating them in every
// event (NanoAODOutputModule only persists std::string products with instance name "genModel",
// so a generic per-event string product would silently be dropped anyway) they are recorded
// once, in order, as the documentation ("title") of the nEFTfitCoefficients branch -- the same
// mechanism NanoAOD already uses for other static per-table metadata.
class EFTFitTableProducer : public edm::global::EDProducer<> {
public:
  explicit EFTFitTableProducer(const edm::ParameterSet& params)
      : precision_(params.getParameter<int>("precision")) {
    for (const auto& tag : params.getParameter<std::vector<edm::InputTag>>("lheInfo")) {
      lheTags_.push_back(consumes<LHEEventProduct>(tag));
    }
    produces<nanoaod::FlatTable>("Coefficients");
    produces<nanoaod::FlatTable>("CoefficientIndex1");
    produces<nanoaod::FlatTable>("CoefficientIndex2");
  }

  ~EFTFitTableProducer() override {}

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<std::vector<edm::InputTag>>("lheInfo",
                                          std::vector<edm::InputTag>{
                                              edm::InputTag("externalLHEProducer"), edm::InputTag("source")})
        ->setComment("tags to try to find the LHEEventProduct on, in order");
    desc.add<int>("precision", 14)->setComment("number of mantissa bits to keep for the fit coefficients, -1 for all");
    descriptions.add("eftFitTable", desc);
  }

  void produce(edm::StreamID, edm::Event& iEvent, const edm::EventSetup&) const override {
    edm::Handle<LHEEventProduct> lheInfo;
    for (const auto& tag : lheTags_) {
      iEvent.getByToken(tag, lheInfo);
      if (lheInfo.isValid())
        break;
    }

    std::vector<std::string> names;  // WC names, names[0] == "sm"
    std::vector<float> coefs;
    std::vector<int> idx1, idx2;

    if (lheInfo.isValid()) {
      fillFit(*lheInfo, names, coefs, idx1, idx2);
    } else if (!hasWarned_.exchange(true)) {
      edm::LogWarning("EFTFitTableProducer") << "No LHEEventProduct found: EFT fit tables will be empty.\n";
    }

    const std::string namesDoc = "WC names, in index order (index 0 is always \"sm\"): " + boost::algorithm::join(names, ",");

    auto coefTab = std::make_unique<nanoaod::FlatTable>(coefs.size(), "EFTfitCoefficients", false);
    coefTab->addColumn<float>("", coefs, "EFT fit coefficient for the (Index1,Index2) term", precision_);
    coefTab->setDoc("EFT fit coefficients. " + namesDoc);
    auto idx1Tab = std::make_unique<nanoaod::FlatTable>(idx1.size(), "EFTfitCoefficientIndex1", false);
    idx1Tab->addColumn<int>("", idx1, "index of the first WC in this term");
    idx1Tab->setDoc(namesDoc);
    auto idx2Tab = std::make_unique<nanoaod::FlatTable>(idx2.size(), "EFTfitCoefficientIndex2", false);
    idx2Tab->addColumn<int>("", idx2, "index of the second WC in this term");
    idx2Tab->setDoc(namesDoc);

    iEvent.put(std::move(coefTab), "Coefficients");
    iEvent.put(std::move(idx1Tab), "CoefficientIndex1");
    iEvent.put(std::move(idx2Tab), "CoefficientIndex2");
  }

private:
  // Fills names/coefs/idx1/idx2 from whichever EFT weight convention is present in lheProd.
  // names[0] is always "sm", and idx1[k]/idx2[k] index into names for the k-th quadratic term;
  // if neither convention is found, all outputs are left empty.
  static void fillFit(const LHEEventProduct& lheProd,
                       std::vector<std::string>& names,
                       std::vector<float>& coefs,
                       std::vector<int>& idx1,
                       std::vector<int>& idx2) {
    bool madjaxEFT = false, eftReweight = false;
    for (const auto& weight : lheProd.weights()) {
      if (weight.id.rfind("MJEFT", 0) == 0)
        madjaxEFT = true;
      else if (weight.id.rfind("EFTrwgt", 0) == 0)
        eftReweight = true;
    }

    if (madjaxEFT) {
      // ex id: "MJEFT_sm_ctW" -> the coefficient of the sm*ctW term
      std::set<std::string> seen;
      std::map<std::pair<std::string, std::string>, double> coefMap;
      for (const auto& weight : lheProd.weights()) {
        if (weight.id.rfind("MJEFT", 0) != 0)
          continue;
        std::vector<std::string> words;
        boost::split(words, weight.id, boost::is_any_of("_"));
        if (words.size() < 3) {
          edm::LogWarning("EFTFitTableProducer") << "Malformed MJEFT weight id, skipping: " << weight.id << "\n";
          continue;
        }
        coefMap[std::make_pair(words[1], words[2])] = weight.wgt;
        coefMap[std::make_pair(words[2], words[1])] = weight.wgt;
        for (unsigned int i = 1; i <= 2; ++i) {
          if (seen.insert(words[i]).second)
            names.push_back(words[i]);
        }
      }
      for (unsigned int i = 0; i < names.size(); ++i) {
        for (unsigned int j = 0; j <= i; ++j) {
          coefs.push_back(coefMap[std::make_pair(names[i], names[j])]);
          idx1.push_back(i);
          idx2.push_back(j);
        }
      }
    } else if (eftReweight) {
      std::vector<nanoaod::eft::WCPoint> points;
      for (const auto& weight : lheProd.weights()) {
        if (weight.id.rfind("EFTrwgt", 0) == 0)
          points.emplace_back(weight.id, weight.wgt);
      }
      nanoaod::eft::WCFit fit(points);
      names = fit.names();
      for (double c : fit.coefficients())
        coefs.push_back(c);
      for (const auto& pair : fit.pairs()) {
        idx1.push_back(pair.first);
        idx2.push_back(pair.second);
      }
    }
  }

  std::vector<edm::EDGetTokenT<LHEEventProduct>> lheTags_;
  int precision_;
  mutable std::atomic<bool> hasWarned_{false};
};

#include "FWCore/Framework/interface/MakerMacros.h"
DEFINE_FWK_MODULE(EFTFitTableProducer);
