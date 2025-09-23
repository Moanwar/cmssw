#include "RecoHGCal/TICL/interface/TICLInterpretationAlgoBase.h"
#include "RecoHGCal/TICL/plugins/GNNInterpretationAlgo.h"
#include "RecoParticleFlow/PFProducer/interface/PFMuonAlgo.h"
#include "TrackingTools/TrajectoryState/interface/TrajectoryStateTransform.h"

using namespace ticl;
using namespace cms::Ort;
using Vector = ticl::Trackster::Vector;

GNNInterpretationAlgo::~GNNInterpretationAlgo() {}

GNNInterpretationAlgo::GNNInterpretationAlgo(const edm::ParameterSet &conf, edm::ConsumesCollector cc)
    : TICLInterpretationAlgoBase(conf, cc),
      onnxLinkingRuntimeFirstDisk_(std::make_unique<cms::Ort::ONNXRuntime>(
	  conf.getParameter<edm::FileInPath>("onnxTrkLinkingModelFirstDisk").fullPath().c_str())),
      onnxLinkingRuntimeInterfaceDisk_(std::make_unique<cms::Ort::ONNXRuntime>(
          conf.getParameter<edm::FileInPath>("onnxTrkLinkingModelInterfaceDisk").fullPath().c_str())),
      inputNames_(conf.getParameter<std::vector<std::string>>("inputNames")),
      output_(conf.getParameter<std::vector<std::string>>("output")),      
      del_tk_ts_(conf.getParameter<double>("delta_tk_ts"))
{
  onnxLinkingSessionFirstDisk_ = onnxLinkingRuntimeFirstDisk_.get();
  onnxLinkingSessionInterfaceDisk_ = onnxLinkingRuntimeInterfaceDisk_.get();
}


void GNNInterpretationAlgo::initialize(const HGCalDDDConstants *hgcons,
				       const hgcal::RecHitTools rhtools,
				       const edm::ESHandle<MagneticField> bfieldH,
				       const edm::ESHandle<Propagator> propH) {
  hgcons_ = hgcons;
  rhtools_ = rhtools;
  buildLayers();

  bfield_ = bfieldH;
  propagator_ = propH;
}

void GNNInterpretationAlgo::buildLayers() {
  // build disks at HGCal front & EM-Had interface for track propagation

  float zVal = hgcons_->waferZ(1, true);
  std::pair<float, float> rMinMax = hgcons_->rangeR(zVal, true);

  float zVal_interface = rhtools_.getPositionLayer(rhtools_.lastLayerEE()).z();
  std::pair<float, float> rMinMax_interface = hgcons_->rangeR(zVal_interface, true);

  for (int iSide = 0; iSide < 2; ++iSide) {
    float zSide = (iSide == 0) ? (-1. * zVal) : zVal;
    firstDisk_[iSide] =
        std::make_unique<GeomDet>(Disk::build(Disk::PositionType(0, 0, zSide),
                                              Disk::RotationType(),
                                              SimpleDiskBounds(rMinMax.first, rMinMax.second, zSide - 0.5, zSide + 0.5))
                                      .get());

    zSide = (iSide == 0) ? (-1. * zVal_interface) : zVal_interface;
    interfaceDisk_[iSide] = std::make_unique<GeomDet>(
        Disk::build(Disk::PositionType(0, 0, zSide),
                    Disk::RotationType(),
                    SimpleDiskBounds(rMinMax_interface.first, rMinMax_interface.second, zSide - 0.5, zSide + 0.5))
            .get());
  }
}
Vector GNNInterpretationAlgo::propagateTrackster(const Trackster &t,
                                                     const unsigned idx,
                                                     float zVal,
                                                     std::array<TICLLayerTile, 2> &tracksterTiles) {
  // needs only the positive Z co-ordinate of the surface to propagate to
  // the correct sign is calculated inside according to the barycenter of trackster
  Vector const &baryc = t.barycenter();
  Vector directnv = t.eigenvectors(0);

  // barycenter as direction for tracksters w/ poor PCA
  // propagation still done to get the cartesian coords
  // which are anyway converted to eta, phi in linking
  // -> can be simplified later

  //FP: disable PCA propagation for the moment and fallback to barycenter position
  // if (t.eigenvalues()[0] / t.eigenvalues()[1] < 20)
  directnv = baryc.unit();
  zVal *= (baryc.Z() > 0) ? 1 : -1;
  float par = (zVal - baryc.Z()) / directnv.Z();
  float xOnSurface = par * directnv.X() + baryc.X();
  float yOnSurface = par * directnv.Y() + baryc.Y();
  Vector tPoint(xOnSurface, yOnSurface, zVal);
  if (tPoint.Eta() > 0) {
    tracksterTiles[1].fill(tPoint.Eta(), tPoint.Phi(), idx);
  } else if (tPoint.Eta() < 0) {
    tracksterTiles[0].fill(tPoint.Eta(), tPoint.Phi(), idx);
  }

  return tPoint;
}
std::pair<float, float> GNNInterpretationAlgo::CalculateTrackstersError(const Trackster &trackster) {
  const auto &barycenter = trackster.barycenter();
  const double x = barycenter.x(), y = barycenter.y(), z = barycenter.z();
  
  const auto &s = trackster.sigmasPCA();
  const double s1 = s[0]*s[0], s2 = s[1]*s[1], s3 = s[2]*s[2];

  const auto &v1 = trackster.eigenvectors()[0];
  const auto &v2 = trackster.eigenvectors()[1];
  const auto &v3 = trackster.eigenvectors()[2];

  // Covariance in XY from 3D
  const double cxx = s1*v1.x()*v1.x() + s2*v2.x()*v2.x() + s3*v3.x()*v3.x();
  const double cxy = s1*v1.x()*v1.y() + s2*v2.x()*v2.y() + s3*v3.x()*v3.y();
  const double cyy = s1*v1.y()*v1.y() + s2*v2.y()*v2.y() + s3*v3.y()*v3.y();

  // Geometry helpers
  const double r2 = x*x + y*y;
  const double denom_eta = r2 * (r2 + z*z);
  const double sqrt_term = std::sqrt(r2/(z*z) + 1);

  // Jacobian elements
  const double J00 = -(x * z * z * sqrt_term) / denom_eta;
  const double J01 = -(y * z * z * sqrt_term) / denom_eta;
  const double J10 = -y / r2;
  const double J11 =  x / r2;

  // CovEtaPhi = J * CovXY * J^T
  const double cee = J00*(J00*cxx + J01*cxy) + J01*(J00*cxy + J01*cyy);
  const double cpp = J10*(J10*cxx + J11*cxy) + J11*(J10*cxy + J11*cyy);

  return {std::sqrt(std::abs(cee)), std::sqrt(std::abs(cpp))};
}

void GNNInterpretationAlgo::constructNodeFromWindow(const MultiVectorManager<Trackster> &tracksters,
						    const std::vector<std::tuple<Vector, unsigned, AlgebraicMatrix55>> &seeding,
						    const std::array<TICLLayerTile, 2> &tracksterTiles,
						    const std::vector<Vector> &tracksterPropPoints,
						    float delta,
						    unsigned trackstersSize,
						    std::vector<GraphNode> &graph) {
  const float delta2 = delta * delta;
  std::vector<int> mask(trackstersSize, 0);
  for (auto &i : seeding) {
    const Vector& pos = std::get<0>(i);
    const unsigned idx = std::get<1>(i);

    float seed_eta = pos.Eta();
    float seed_phi = pos.Phi();
    unsigned seedId = idx;
    auto sideZ = seed_eta > 0;
    const TICLLayerTile &tile = tracksterTiles[sideZ];

    float eta_min = std::max(std::fabs(seed_eta) - delta, (float)TileConstants::minEta);
    float eta_max = std::min(std::fabs(seed_eta) + delta, (float)TileConstants::maxEta);

    std::array<int, 4> search_box = tile.searchBoxEtaPhi(eta_min, eta_max, seed_phi - delta, seed_phi + delta);

    GraphNode node;
    node.index = seedId;
    node.isTrackster = false;

    for (int eta_i = search_box[0]; eta_i <= search_box[1]; ++eta_i) {
      for (int phi_i = search_box[2]; phi_i <= search_box[3]; ++phi_i) {
        const auto &in_tile = tile[tile.globalBin(eta_i, (phi_i % TileConstants::nPhiBins))];
        for (const unsigned &t_i : in_tile) {
          float sep2 = std::pow(tracksterPropPoints[t_i].Eta() - seed_eta, 2) +
                       std::pow(tracksterPropPoints[t_i].Phi() - seed_phi, 2);
          if (sep2 < delta2) {
            GraphEdge edge;
            edge.target_index = t_i;
            edge.weight = sep2; // or sqrt(sep2), or 1/sqrt(sep2), depending on GNN needs
            node.neighbours.push_back(edge);
          }
        }
      }
    }
    graph.push_back(std::move(node));
  }
}

auto padFeatures = [](const std::vector<float>& core_feats,
                      size_t track_block_size,
                      size_t trackster_block_size,
                      bool isTrack) {
    std::vector<float> out;
    out.reserve(track_block_size + trackster_block_size);
    if (isTrack) {
        out.insert(out.end(), core_feats.begin(), core_feats.end());
        out.insert(out.end(), trackster_block_size, 0.0f);
    } else {
        out.insert(out.end(), track_block_size, 0.0f);
        out.insert(out.end(), core_feats.begin(), core_feats.end());
    }
    return out;
};

void GNNInterpretationAlgo::buildGraphFromNodes(
    const std::tuple<Vector, AlgebraicMatrix55, int>& TrackInfo,
    const reco::Track &track,
    const MultiVectorManager<Trackster> &tracksters,
    const std::vector<reco::CaloCluster> &clusters,
    const std::vector<GraphNode>& nodeVec,
    const float &tkT, const float &tkTErr, const float &tkQual,
    const GlobalPoint &tkMtdPos, GraphData& outGraphData) {

    outGraphData = {}; // clears all vectors/maps

    std::unordered_map<int, std::vector<float>> track_node_features;
    std::unordered_map<int, std::vector<float>> trackster_node_features;

    const Vector& pos = std::get<0>(TrackInfo);
    const AlgebraicMatrix55& localErrMatrix = std::get<1>(TrackInfo);
    int track_idx = std::get<2>(TrackInfo);

    // Build track features once
    float eta = pos.Eta(), phi = pos.Phi();
    float x = pos.X(), y = pos.Y(), z = pos.Z();

    AlgebraicMatrix22 covMatrixXY;
    covMatrixXY(0,0) = localErrMatrix(3,3);
    covMatrixXY(0,1) = localErrMatrix(3,4);
    covMatrixXY(1,0) = localErrMatrix(3,4);
    covMatrixXY(1,1) = localErrMatrix(4,4);

    const double sqrt_term = std::sqrt((x*x + y*y) / (z*z) + 1);
    const double denom_eta = (x*x + y*y) * (x*x + y*y + z*z);
    const double denom_phi = x*x + y*y;

    AlgebraicMatrix22 jacobian;
    jacobian(0,0) = -(x * z * z * sqrt_term) / denom_eta;
    jacobian(0,1) = -(y * z * z * sqrt_term) / denom_eta;
    jacobian(1,0) = -y / denom_phi;
    jacobian(1,1) =  x / denom_phi;

    AlgebraicMatrix22 covMatrixEtaPhi = ROOT::Math::Transpose(jacobian) * covMatrixXY * jacobian;
    float track_etaErr = std::sqrt(covMatrixEtaPhi(0,0));
    float track_phiErr = std::sqrt(covMatrixEtaPhi(1,1));

    float track_p    = track.p();
    float track_pt   = track.pt();
    float trackHits  = track.recHitsSize();

    std::vector<float> trk_feats = {
        std::abs(eta), phi,
        track_etaErr, track_phiErr,
        x, y, std::abs(z),
        track_p, tkT, tkTErr, tkQual,
        track_pt,
        tkMtdPos.x(), tkMtdPos.y(), std::abs(tkMtdPos.z()),
        trackHits
    };
    trk_feats.resize(track_block_size); // pad if fewer

    // Loop over nodes
    for (const auto& node : nodeVec) {
        if (!node.isTrackster && static_cast<int>(node.index) == track_idx) {
            track_node_features[node.index] = padFeatures(trk_feats, track_block_size, trackster_block_size, true);

            // Loop over neighbours (trackster seeds)
            for (const auto& edge : node.neighbours) {
                unsigned ts_seed = edge.target_index;
                if (ts_seed >= tracksters.size()) continue;
                if (!trackster_node_features.count(ts_seed)) {
                    const auto& ts = tracksters[ts_seed];
                    float ts_eta = ts.barycenter().eta();
                    float ts_phi = ts.barycenter().phi();
                    auto error   = CalculateTrackstersError(ts);
                    float xpos   = ts.barycenter().x();
                    float ypos   = ts.barycenter().y();
                    float zpos   = ts.barycenter().z();
                    float rawEnergy      = ts.raw_energy();
                    float time           = ts.time();
                    float timeErr        = ts.timeError();
                    float rawEmEnergy    = ts.raw_em_energy();
                    float rawPt          = ts.raw_pt();
                    float rawEmPt        = ts.raw_em_pt();
		    
                    std::vector<float> ts_feats = {
                        std::abs(ts_eta), ts_phi,
                        error.first, error.second,
                        xpos, ypos, std::abs(zpos),
                        rawEnergy,time, timeErr, rawEmEnergy, rawEmPt, rawPt
                    };
                    ts_feats.resize(trackster_block_size);
                    trackster_node_features[ts_seed] = padFeatures(ts_feats, track_block_size, trackster_block_size, false);
                }

                outGraphData.edge_index.emplace_back(node.index, ts_seed);
            }
        }
    }

    // Insert nodes into final structure
    size_t row_idx = 0;
    for (const auto& [idx, feats] : track_node_features)
        outGraphData.nodeIndexToRow[{false, idx}] = row_idx++, outGraphData.node_features.push_back(feats);
    for (const auto& [idx, feats] : trackster_node_features)
        outGraphData.nodeIndexToRow[{true, idx}]  = row_idx++, outGraphData.node_features.push_back(feats);

    outGraphData.num_nodes = outGraphData.node_features.size();

    // Edge attributes
    auto wrapPhi = [](float dphi) -> float {
        const float pi = M_PI, two_pi = 2.0f * M_PI;
        dphi = std::fmod(dphi + pi, two_pi);
        if (dphi < 0) dphi += two_pi;
        return dphi - pi;
    };

    for (const auto& edge : outGraphData.edge_index) {
        NodeKey src_key = {false, edge.first};
        NodeKey dst_key = {true,  edge.second};
        if (!outGraphData.nodeIndexToRow.count(src_key) ||
            !outGraphData.nodeIndexToRow.count(dst_key)) continue;

        const auto& src_feats = outGraphData.node_features[outGraphData.nodeIndexToRow[src_key]];
        const auto& dst_feats = outGraphData.node_features[outGraphData.nodeIndexToRow[dst_key]];

        // offsets
        const int trkster_offset = track_block_size;

        float trk_eta = src_feats[0], trk_phi = src_feats[1];
        float ts_eta  = dst_feats[trkster_offset], ts_phi = dst_feats[trkster_offset + 1];

        float delta_eta = trk_eta - ts_eta;
        float delta_phi = wrapPhi(trk_phi - ts_phi);
        float deta_sig  = delta_eta / std::sqrt(dst_feats[trkster_offset + 2] * dst_feats[trkster_offset + 2] +
                                                src_feats[2] * src_feats[2] + 1e-8f);
        float dphi_sig  = delta_phi / std::sqrt(dst_feats[trkster_offset + 3] * dst_feats[trkster_offset + 3] +
                                                src_feats[3] * src_feats[3] + 1e-8f);
        float deltaR    = std::sqrt(delta_eta * delta_eta + delta_phi * delta_phi);

        float dx = dst_feats[trkster_offset + 4] - src_feats[4];
        float dy = dst_feats[trkster_offset + 5] - src_feats[5];
        float dz = dst_feats[trkster_offset + 6] - src_feats[6];
        float dist3D = std::sqrt(dx * dx + dy * dy + dz * dz);
        float distXY = std::sqrt(dx * dx + dy * dy);

        float dE      = dst_feats[trkster_offset + 7] - src_feats[7];
        float E_ratio = dst_feats[trkster_offset + 7] / (src_feats[7] + 1e-8f);

        float dtime = dst_feats[trkster_offset + 8] - src_feats[8];
        float time_err_combined = std::sqrt(dst_feats[trkster_offset + 9] * dst_feats[trkster_offset + 9] +
                                            src_feats[9] * src_feats[9] + 1e-8f);
        float dtime_sig = dtime / time_err_combined;

	
	float delta_x = dst_feats[trkster_offset + 4] - src_feats[12] ;
	float delta_y = dst_feats[trkster_offset + 5] - src_feats[13] ;
	float delta_z = dst_feats[trkster_offset + 6] - src_feats[14] ;
	float spatial_distance = std::sqrt(delta_x * delta_x + delta_y * delta_y + delta_z * delta_z) ;
	
        const auto& ts = tracksters[edge.second];
        const auto& vertices = ts.vertices();
        float min_dist = std::numeric_limits<float>::max();
        float max_dist = 0.0f;

        for (const auto& vtx : vertices) {
            const auto& cl = clusters[vtx];
            float ddx = cl.x() - src_feats[4];
            float ddy = cl.y() - src_feats[5];
            float ddz = std::abs(cl.z()) - src_feats[6];
            float dist = std::sqrt(ddx * ddx + ddy * ddy + ddz * ddz);
            min_dist = std::min(min_dist, dist);
            max_dist = std::max(max_dist, dist);
        }

        outGraphData.edge_attr.push_back({
            delta_eta, delta_phi,
            deta_sig, dphi_sig,
            deltaR, dist3D, distXY,
            dE, E_ratio, dtime, dtime_sig,
	    spatial_distance,
            min_dist, max_dist
        });
    }
}
void GNNInterpretationAlgo::makeCandidates(const Inputs &input,
                                               edm::Handle<MtdHostCollection> inputTiming_h,
                                               std::vector<Trackster> &resultTracksters,
                                               std::vector<int> &resultCandidate) {
  bool useMTDTiming = inputTiming_h.isValid();
  const auto tkH = input.tracksHandle;
  const auto maskTracks = input.maskedTracks;
  const auto &tracks = *tkH;
  const auto &tracksters = input.tracksters;
  const auto &clusters = input.layerClusters;
  auto bFieldProd = bfield_.product();
  const Propagator &prop = (*propagator_);

  // propagated point collections
  // elements in the propagated points collecions are used
  // to look for potential linkages in the appropriate tiles
  std::vector<std::tuple<Vector, unsigned, AlgebraicMatrix55>> trackPColl; // propagated track points and index of track in collection
  std::vector<std::tuple<Vector, unsigned, AlgebraicMatrix55>> tkPropIntColl;// tracks propagated to lastLayerEE 

  trackPColl.reserve(tracks.size());
  tkPropIntColl.reserve(tracks.size());
  
  std::array<TICLLayerTile, 2> tracksterPropTiles = {};  // all Tracksters, propagated to layer 1
  std::array<TICLLayerTile, 2> tsPropIntTiles = {};      // all Tracksters, propagated to lastLayerEE

  if (TICLInterpretationAlgoBase::algo_verbosity_ > VerbosityLevel::Advanced)
    LogDebug("GNNInterpretationAlgo") << "------- Geometric Linking ------- \n";

  // Propagate tracks
  std::vector<unsigned> candidateTrackIds;
  candidateTrackIds.reserve(tracks.size());
  for (unsigned i = 0; i < tracks.size(); ++i) {
    if (!maskTracks[i])
      continue;
    candidateTrackIds.push_back(i);
  }

  std::sort(candidateTrackIds.begin(), candidateTrackIds.end(), [&](unsigned i, unsigned j) {
    return tracks[i].p() > tracks[j].p();
  });

  for (auto const i : candidateTrackIds) {
    const auto &tk = tracks[i];
    int iSide = int(tk.eta() > 0);
    const auto &fts = trajectoryStateTransform::outerFreeState((tk), bFieldProd);
    // to the HGCal front
    const auto &tsos = prop.propagate(fts, firstDisk_[iSide]->surface());
    if (tsos.isValid()) {
      AlgebraicMatrix55 localErrorMatrix1 = tsos.localError().matrix();
      Vector trackP(tsos.globalPosition().x(), tsos.globalPosition().y(), tsos.globalPosition().z());
      trackPColl.emplace_back(trackP, i, localErrorMatrix1);
    }
    // to lastLayerEE
    const auto &tsos_int = prop.propagate(fts, interfaceDisk_[iSide]->surface());
    if (tsos_int.isValid()) {
      AlgebraicMatrix55 localErrorMatrix2 = tsos_int.localError().matrix();
      Vector trackP(tsos_int.globalPosition().x(), tsos_int.globalPosition().y(), tsos_int.globalPosition().z());
      tkPropIntColl.emplace_back(trackP, i, localErrorMatrix2);
    }
  }  // Tracks
  tkPropIntColl.shrink_to_fit();
  trackPColl.shrink_to_fit();
  candidateTrackIds.shrink_to_fit();

  // Propagate tracksters

  // Record postions of all tracksters propagated to layer 1 and lastLayerEE,
  // to be used later for distance calculation in the link finding stage
  // indexed by trackster index in event collection
  std::vector<Vector> tsAllProp;
  std::vector<Vector> tsAllPropInt;
  tsAllProp.reserve(tracksters.size());
  tsAllPropInt.reserve(tracksters.size());
  // Propagate tracksters

  for (unsigned i = 0; i < tracksters.size(); ++i) {
    const auto &t = tracksters[i];
    if (TICLInterpretationAlgoBase::algo_verbosity_ > VerbosityLevel::Advanced)
      LogDebug("GNNInterpretationAlgo")
          << "trackster " << i << " - eta " << t.barycenter().eta() << " phi " << t.barycenter().phi() << " time "
          << t.time() << " energy " << t.raw_energy() << "\n";

    // to HGCal front
    float zVal = hgcons_->waferZ(1, true);
    auto tsP = propagateTrackster(t, i, zVal, tracksterPropTiles);
    tsAllProp.emplace_back(tsP);

    // to lastLayerEE
    zVal = rhtools_.getPositionLayer(rhtools_.lastLayerEE()).z();
    tsP = propagateTrackster(t, i, zVal, tsPropIntTiles);
    tsAllPropInt.emplace_back(tsP);

  }  // TS
  // Step 1: Construct nodes from tracksters and tracks
  std::vector<GraphNode> tsNearTk_node, tsTkAtInt_node;
  constructNodeFromWindow(tracksters, trackPColl, tracksterPropTiles, tsAllProp, del_tk_ts_, tracksters.size(), tsNearTk_node);
  constructNodeFromWindow(tracksters, tkPropIntColl, tsPropIntTiles, tsAllPropInt, del_tk_ts_, tracksters.size(), tsTkAtInt_node);
  
  std::vector<std::vector<unsigned int>> trackstersInTrackIndices(tracks.size());
  std::vector<bool> chargedMask(tracksters.size(), true);

  auto processTrack = [&](unsigned trkId, const auto& trackColl, auto& tsNodes, bool isIntLayer) {
    float track_time = 0.f, track_timeErr = 0.f, track_quality = 0.f;
    GlobalPoint track_MtdPos{0.f, 0.f, 0.f};
    
    if (useMTDTiming) {
      auto const &inputTimingView = (*inputTiming_h).const_view();
        track_time = inputTimingView.time()[trkId];
        track_timeErr = inputTimingView.timeErr()[trkId];
        track_quality = inputTimingView.MVAquality()[trkId];
        track_MtdPos = {inputTimingView.posInMTD_x()[trkId], 
	  inputTimingView.posInMTD_y()[trkId], 
	  inputTimingView.posInMTD_z()[trkId]};
    }
    
    auto it = std::find_if(trackColl.begin(), trackColl.end(),
			   [trkId](const auto& seed) { return trkId == std::get<1>(seed); });
    if (it == trackColl.end()) return false;
    
    auto trackInfo = std::make_tuple(std::get<0>(*it), std::get<2>(*it), std::get<1>(*it));
    GraphData graphData;
    buildGraphFromNodes(trackInfo, tracks[trkId], tracksters, clusters, tsNodes,
			track_time, track_timeErr, track_quality, track_MtdPos, graphData);
    
    // Prepare ONNX input
    std::vector<std::vector<float>> input_data(3);
    std::vector<std::vector<int64_t>> input_shapes;
    
    // Node features
    int64_t num_nodes    = graphData.node_features.size();
    int64_t num_features = graphData.node_features.empty() ? 0 : graphData.node_features[0].size();

    for (const auto& node : graphData.node_features) {
      input_data[0].insert(input_data[0].end(), node.begin(), node.end());
    }
    input_shapes.push_back({num_nodes, num_features});
    
    // Edge indices
    std::vector<float> src_nodes, dst_nodes;
    for (const auto& edge : graphData.edge_index) {
      NodeKey src_key = {false, edge.first};
      NodeKey dst_key = {true, edge.second};
      src_nodes.push_back(graphData.nodeIndexToRow.at(src_key));
      dst_nodes.push_back(graphData.nodeIndexToRow.at(dst_key));
    }
    input_data[1].insert(input_data[1].end(), src_nodes.begin(), src_nodes.end());
    input_data[1].insert(input_data[1].end(), dst_nodes.begin(), dst_nodes.end());
    input_shapes.push_back({2, (int64_t)graphData.edge_index.size()});
    
    // Edge attributes
    int64_t num_edges    = graphData.edge_attr.size();
    int64_t num_edges_features = graphData.edge_attr.empty() ? 0 : graphData.edge_attr[0].size();
    
    for (const auto& attr : graphData.edge_attr) {
      input_data[2].insert(input_data[2].end(), attr.begin(), attr.end());
    }    
    input_shapes.push_back({num_edges, num_edges_features});

    if (input_data[1].empty()) return false;

    // Run inference and process results
    auto result = [&]() {
      if (isIntLayer) {
        return onnxLinkingSessionInterfaceDisk_->run(inputNames_, input_data, input_shapes, output_);
      } else {
        return onnxLinkingSessionFirstDisk_->run(inputNames_, input_data, input_shapes, output_);
      }
    }();
    const auto& output_scores = result[0];

    const float threshold = isIntLayer ? 0.5f : 0.45f;
    
    for (size_t i = 0; i < graphData.edge_index.size(); ++i) {
      const auto& edge = graphData.edge_index[i];
      NodeKey srcKey = {false, edge.first};
      NodeKey dstKey = {true, edge.second};
      
      if (srcKey.first || !dstKey.first) continue;
      if (srcKey.second < 0 || dstKey.second < 0) continue;
      if (srcKey.second >= (int)tracks.size() || dstKey.second >= (int)tracksters.size()) continue;
      if (output_scores[i] > threshold && chargedMask[dstKey.second]) {
	trackstersInTrackIndices[srcKey.second].push_back(dstKey.second);
	chargedMask[dstKey.second] = false;
      }
    }
    return true;
  };
  
  for (unsigned trkId : candidateTrackIds) {
    processTrack(trkId, tkPropIntColl, tsTkAtInt_node, true);
    processTrack(trkId, trackPColl, tsNearTk_node, false);
  }
  
  for (size_t iTrack = 0; iTrack < trackstersInTrackIndices.size(); iTrack++) {
    if (!trackstersInTrackIndices[iTrack].empty()) {
      if (trackstersInTrackIndices[iTrack].size() == 1) {
        auto tracksterId = trackstersInTrackIndices[iTrack][0];
        resultCandidate[iTrack] = resultTracksters.size();
        resultTracksters.push_back(input.tracksters[tracksterId]);
      } else {
        // in this case mergeTracksters() clears the pid probabilities and the regressed energy is not set
        // TODO: fix probabilities when CNN will be splitted
        Trackster outTrackster;
        //float regr_en = 0.f;
        bool isHadron = false;
        for (auto const tracksterId : trackstersInTrackIndices[iTrack]) {
          //maskTracksters[tracksterId] = 0;
          outTrackster.mergeTracksters(input.tracksters[tracksterId]);
          //regr_en += input.tracksters[tracksterId].regressed_energy();
          if (input.tracksters[tracksterId].isHadronic())
            isHadron = true;
        }
        resultCandidate[iTrack] = resultTracksters.size();
        resultTracksters.push_back(outTrackster);
        //resultTracksters.back().setRegressedEnergy(regr_en);
        // since a track has been linked it can only be electron or charged hadron
        if (isHadron)
          resultTracksters.back().setIdProbability(ticl::Trackster::ParticleType::charged_hadron, 1.f);
        else
          resultTracksters.back().setIdProbability(ticl::Trackster::ParticleType::electron, 1.f);
      }
    }
  }
  
  for (size_t iTrackster = 0; iTrackster < input.tracksters.size(); iTrackster++) {
    if (chargedMask[iTrackster]) {
      resultTracksters.push_back(input.tracksters[iTrackster]);
    }
  }
};

void GNNInterpretationAlgo::fillPSetDescription(edm::ParameterSetDescription &desc) {
  desc.add<std::string>("cutTk",
                        "1.48 < abs(eta) < 3.0 && pt > 1. && quality(\"highPurity\") && "
                        "hitPattern().numberOfLostHits(\"MISSING_OUTER_HITS\") < 5");
  desc
    .add<edm::FileInPath>(
			  "onnxTrkLinkingModelFirstDisk",
			  edm::FileInPath("RecoHGCal/TICL/data/ticlv5/onnx_models/GNN_Linking/gnn_model_firstdisk.onnx"))
    ->setComment("Path to ONNX tracks tracksters linking model at first disk ");
  desc
    .add<edm::FileInPath>(
                          "onnxTrkLinkingModelInterfaceDisk",
	                  edm::FileInPath("RecoHGCal/TICL/data/ticlv5/onnx_models/GNN_Linking/gnn_model_interfacedisk.onnx"))
    ->setComment("Path to ONNX tracks tracksters linking model at interface disk ");

  desc.add<std::vector<std::string>>("inputNames", {"x", "edge_index", "edge_attr"});
  desc.add<std::vector<std::string>>("output", {"output"});
  desc.add<double>("delta_tk_ts", 0.31622777);
  TICLInterpretationAlgoBase::fillPSetDescription(desc);
}
