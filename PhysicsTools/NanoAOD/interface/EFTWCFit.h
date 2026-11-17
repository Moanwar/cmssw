#ifndef PhysicsTools_NanoAOD_interface_EFTWCFit_h
#define PhysicsTools_NanoAOD_interface_EFTWCFit_h

#include <string>
#include <utility>
#include <vector>

#include "PhysicsTools/NanoAOD/interface/EFTWCPoint.h"

#include "TDecompSVD.h"
#include "TMatrixD.h"
#include "TVectorD.h"

namespace nanoaod {
  namespace eft {

    // Fits a set of WCPoints (EFT reweighting samples for one event) to the quadratic form that
    // MadGraph5_aMC@NLO's EFT reweighting assumes:
    //   w(c) = sum_{i<=j} coefficients()[k] * c_i * c_j,  c_0 == 1 ("sm")
    // names()[0] is always "sm"; pairs()[k] gives the (i,j) index pair into names() for
    // coefficients()[k]. All points are assumed to vary the same set of WCs (the names are taken
    // from the first point).
    class WCFit {
    public:
      explicit WCFit(const std::vector<WCPoint>& points) { fit(points); }

      const std::vector<std::string>& names() const { return names_; }
      const std::vector<std::pair<int, int>>& pairs() const { return pairs_; }
      const std::vector<double>& coefficients() const { return coeffs_; }

    private:
      void fit(const std::vector<WCPoint>& points) {
        if (points.empty())
          return;

        names_.push_back("sm");
        for (const auto& wc : points.front().values) {
          names_.push_back(wc.first);
        }
        for (size_t i = 0; i < names_.size(); ++i) {
          for (size_t j = 0; j <= i; ++j) {
            pairs_.emplace_back(i, j);
          }
        }

        const int nRows = points.size();
        const int nCols = pairs_.size();
        TMatrixD A(nRows, nCols);
        TVectorD b(nRows);
        for (int row = 0; row < nRows; ++row) {
          b(row) = points[row].weight;
          for (int col = 0; col < nCols; ++col) {
            const auto& p = pairs_[col];
            double x1 = (p.first == 0) ? 1.0 : points[row].strength(names_[p.first]);
            double x2 = (p.second == 0) ? 1.0 : points[row].strength(names_[p.second]);
            A(row, col) = x1 * x2;
          }
        }

        bool ok = false;
        TDecompSVD svd(A);
        TVectorD x = svd.Solve(b, ok);
        coeffs_.assign(x.GetMatrixArray(), x.GetMatrixArray() + nCols);
      }

      std::vector<std::string> names_;
      std::vector<std::pair<int, int>> pairs_;
      std::vector<double> coeffs_;
    };

  }  // namespace eft
}  // namespace nanoaod

#endif
