#ifndef PhysicsTools_NanoAOD_interface_EFTWCPoint_h
#define PhysicsTools_NanoAOD_interface_EFTWCPoint_h

#include <string>
#include <unordered_map>
#include <vector>

#include "boost/algorithm/string.hpp"

namespace nanoaod {
  namespace eft {

    // A single EFT reweighting point: the Wilson coefficient values used to generate one LHE
    // reweighting variation, and the resulting event weight. Parses MadGraph5_aMC@NLO's
    // "EFTrwgt<N>_<wc1>_<val1>_<wc2>_<val2>_..." weight-ID convention (the SM point, where all
    // WCs are 0, has no entries in `values`).
    class WCPoint {
    public:
      WCPoint() : weight(0.0) {}
      WCPoint(const std::string& id, double wgt) : weight(wgt) { parse(id); }

      double strength(const std::string& name) const {
        auto it = values.find(name);
        return (it != values.end()) ? it->second : 0.0;
      }

      std::unordered_map<std::string, double> values;  // WC name -> strength
      double weight;

    private:
      void parse(const std::string& id) {
        std::vector<std::string> words;
        boost::split(words, id, boost::is_any_of("_"));
        // words[0] is the "EFTrwgt<N>" tag; (name, value) pairs follow it two at a time.
        for (size_t i = 1; i + 1 < words.size(); i += 2) {
          values[words[i]] = std::stod(words[i + 1]);
        }
      }
    };

  }  // namespace eft
}  // namespace nanoaod

#endif
