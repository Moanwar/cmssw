// Original author: Felice Pantaleo (CERN) <felice.pantaleo@cern.ch>

#ifndef PhysicsTools_TruthInfo_interface_RecoHitAdapters_h
#define PhysicsTools_TruthInfo_interface_RecoHitAdapters_h

// Adapters that expose a reco object's hits as a range of truth::RecoHit so the
// generic BranchHitAssociator and the reco-to-truth associators can match any reco object to
// the truth Branch graph (the customization point envisaged by the HasTruthHits
// concept in BranchHitAssociator.h). These live here, not as member methods on the
// reco data formats, for two reasons: (a) only reco::Track owns its hits - a
// Trackster/TICLCandidate/PFCandidate references layer clusters / blocks that live
// in separate event collections, which a data-format method cannot reach; and (b)
// returning a PhysicsTools type from a DataFormats class would invert the package
// dependency. Each adapter therefore takes the object plus whatever external
// collection it needs.
//
// Tracker hits are exposed with unit fraction, for the SharedHits metric.
// Calorimeter hits are exposed with the cell fraction, and the shared-energy metric
// weights each cell by its rechit energy from the associator's CellEnergyTable.

#include <algorithm>
#include <vector>

#include "DataFormats/CaloRecHit/interface/CaloCluster.h"
#include "DataFormats/Phase2TrackerCluster/interface/Phase2TrackerCluster1D.h"
#include "DataFormats/HGCalReco/interface/Trackster.h"
#include "DataFormats/TrackReco/interface/Track.h"
#include "DataFormats/TrackerRecHit2D/interface/TrackerSingleRecHit.h"
#include "DataFormats/TrackerRecHit2D/interface/trackerHitRTTI.h"
#include "DataFormats/TrackingRecHit/interface/TrackingRecHit.h"

#include "PhysicsTools/TruthInfo/interface/BranchHitAssociator.h"
#include "PhysicsTools/TruthInfo/interface/TrackerCells.h"

namespace truth {

  // Appends the (detId, cell) entries of one TrackerSingleRecHit's cluster. Shared by the
  // reco::Track adapter below (one call per valid single hit on the track) and by the
  // standalone TrackerSingleRecHit adapter, so the two packings - pixel (row, column) and
  // outer-tracker (firstRow + i, column) - stay written in one place.
  inline void appendTrackerCells(TrackerSingleRecHit const& hit, std::vector<RecoHit>& hits) {
    const uint32_t detId = hit.geographicalId().rawId();
    if (hit.cluster_pixel().isNonnull()) {
      for (auto const& pixel : hit.cluster_pixel()->pixels()) {
        hits.push_back(RecoHit{.detId = detId, .cell = pixelCell(pixel.x, pixel.y)});
      }
    } else if (hit.cluster_phase2OT().isNonnull()) {
      auto const& cluster = *hit.cluster_phase2OT();
      const unsigned int column = cluster.column();
      const unsigned int firstRow = cluster.firstRow();
      for (unsigned int i = 0; i < cluster.size(); ++i) {
        hits.push_back(RecoHit{.detId = detId, .cell = outerTrackerCell(firstRow + i, column)});
      }
    }
  }

  // reco::Track -> its valid rechits (unit weight; tracker shared-hit metric). One entry
  // per cell of the hit's cluster, so the two sides compare the same objects. A hit that
  // carries no cluster names no cell, cannot match the cell-keyed tracker truth, and is
  // not emitted.
  inline std::vector<RecoHit> recoHits(reco::Track const& track) {
    std::vector<RecoHit> hits;
    hits.reserve(track.recHitsSize());
    for (auto it = track.recHitsBegin(); it != track.recHitsEnd(); ++it) {
      TrackingRecHit const* hit = &(**it);
      if (!hit->isValid()) {
        continue;
      }
      // A stub or a matched hit is not a single hit and carries no cluster of its own. A
      // FastSim single hit is not a TrackerSingleRecHit and carries no cluster either, so
      // the tag must be exactly single. The RTTI tag is an int compare, where a
      // dynamic_cast on every hit of every track shows up in a PU200 profile.
      if (trackerHitRTTI::rtti(*hit) != trackerHitRTTI::single) {
        continue;
      }
      appendTrackerCells(static_cast<TrackerSingleRecHit const&>(*hit), hits);
    }
    // One entry per (DetId, cell), ascending, which is what the merge-join in
    // BranchHitAssociator requires. Two valid rechits can carry one geographicalId, and a
    // cell is one object however many rechits name it.
    const auto byCell = [](RecoHit const& a, RecoHit const& b) {
      if (a.detId != b.detId)
        return a.detId < b.detId;
      return a.cell < b.cell;
    };
    std::sort(hits.begin(), hits.end(), byCell);
    hits.erase(std::unique(hits.begin(),
                           hits.end(),
                           [](RecoHit const& a, RecoHit const& b) { return a.detId == b.detId && a.cell == b.cell; }),
               hits.end());
    return hits;
  }

  // A single tracker rechit standing alone (e.g. siPixelRecHitsPreSplitting), matched
  // directly instead of through a track: the cells of its own cluster, unit fraction.
  // SiPixelRecHit and the outer-tracker single rechit both derive from
  // TrackerSingleRecHit, so one adapter serves either, chosen by the domain's
  // recoCollections product type.
  inline std::vector<RecoHit> recoHits(TrackerSingleRecHit const& hit) {
    std::vector<RecoHit> hits;
    if (!hit.isValid()) {
      return hits;
    }
    appendTrackerCells(hit, hits);
    std::sort(hits.begin(), hits.end(), [](RecoHit const& a, RecoHit const& b) { return a.cell < b.cell; });
    return hits;
  }

  // Sort by detId and coalesce duplicates in place (fractions summed), so the
  // merge-join in BranchHitAssociator sees each cell once, without a second vector.
  inline void sortAndCoalesce(std::vector<RecoHit>& hits) {
    std::sort(hits.begin(), hits.end(), [](RecoHit const& a, RecoHit const& b) { return a.detId < b.detId; });
    std::size_t w = 0;
    for (std::size_t r = 0; r < hits.size(); ++r) {
      if (w > 0 && hits[w - 1].detId == hits[r].detId)
        hits[w - 1].fraction += hits[r].fraction;
      else
        hits[w++] = hits[r];
    }
    hits.resize(w);
  }

  // reco::CaloCluster (a single layer cluster) -> its (DetId, fraction) hits. Sorted by detId,
  // and coalesced (fractions summed) so a repeated cell is seen once by the merge-join
  // in BranchHitAssociator. HGCAL layer clusters list each cell once, but the overload
  // is generic over reco::CaloCluster, so the dedup keeps it correct for any input.
  inline std::vector<RecoHit> recoHits(reco::CaloCluster const& layerCluster) {
    std::vector<RecoHit> hits;
    hits.reserve(layerCluster.hitsAndFractions().size());
    for (auto const& [detId, fraction] : layerCluster.hitsAndFractions())
      hits.push_back(RecoHit{.detId = detId.rawId(), .fraction = fraction});
    sortAndCoalesce(hits);
    return hits;
  }

  // ticl::Trackster -> the (DetId, fraction) of its layer clusters. A layer cluster shared
  // by several tracksters contributes 1/multiplicity of its fraction, as in the TICL
  // trackster associations. Duplicate cells across the trackster's layer clusters
  // are coalesced (fractions summed) so the merge-join in BranchHitAssociator sees
  // each cell once.
  inline std::vector<RecoHit> recoHits(ticl::Trackster const& trackster,
                                       std::vector<reco::CaloCluster> const& layerClusters) {
    std::vector<RecoHit> hits;
    auto const& vertices = trackster.vertices();
    auto const& multiplicities = trackster.vertex_multiplicity();
    std::size_t nHits = 0;
    for (const unsigned int lc : vertices) {
      if (lc < layerClusters.size()) {
        nHits += layerClusters[lc].hitsAndFractions().size();
      }
    }
    hits.reserve(nHits);
    for (std::size_t v = 0; v < vertices.size(); ++v) {
      const unsigned int lc = vertices[v];
      if (lc >= layerClusters.size())
        continue;
      const float multiplicity = v < multiplicities.size() && multiplicities[v] > 0.f ? multiplicities[v] : 1.f;
      for (auto const& [detId, fraction] : layerClusters[lc].hitsAndFractions())
        hits.push_back(RecoHit{.detId = detId.rawId(), .fraction = fraction / multiplicity});
    }
    sortAndCoalesce(hits);
    return hits;
  }

}  // namespace truth

#endif
