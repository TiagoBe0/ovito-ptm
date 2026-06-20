////////////////////////////////////////////////////////////////////////////////////////
//
//  Copyright 2026 OVITO GmbH, Germany
//
//  This file is part of OVITO (Open Visualization Tool).
//
//  OVITO is free software; you can redistribute it and/or modify it either under the
//  terms of the GNU General Public License version 3 as published by the Free Software
//  Foundation (the "GPL") or, at your option, under the terms of the MIT License.
//  If you do not alter this notice, a recipient may use your version of this
//  file under either the GPL or the MIT License.
//
//  You should have received a copy of the GPL along with this program in a
//  file LICENSE.GPL.txt.  You should have received a copy of the MIT License along
//  with this program in a file LICENSE.MIT.txt
//
//  This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND,
//  either express or implied. See the GPL or the MIT License for the specific language
//  governing rights and limitations.
//
////////////////////////////////////////////////////////////////////////////////////////

#include <ovito/crystalanalysis/CrystalAnalysis.h>
#include <ovito/crystalanalysis/objects/DislocationNetwork.h>
#include <ovito/crystalanalysis/objects/Cluster.h>
#include <ovito/crystalanalysis/objects/ClusterVector.h>
#include <ovito/crystalanalysis/objects/MicrostructurePhase.h>
#include <ovito/crystalanalysis/objects/BurgersVectorFamily.h>
#include <ovito/stdobj/simcell/SimulationCell.h>
#include <ovito/core/dataset/pipeline/ModifierEvaluationTask.h>
#include <ovito/core/app/UserInterface.h>
#include <ovito/core/utilities/concurrent/TaskManager.h>
#include <ovito/core/utilities/concurrent/ForEach.h>
#include <ovito/core/utilities/concurrent/LaunchTask.h>
#include <boost/range/irange.hpp>
#include <algorithm>
#include <cmath>
#include <functional>
#include <numeric>
#include <unordered_map>
#include <unordered_set>
#include "DislocationTrackingModifier.h"

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(DislocationTrackingModifier);
OVITO_CLASSINFO(DislocationTrackingModifier, "DisplayName", "Dislocation tracking");
OVITO_CLASSINFO(DislocationTrackingModifier, "Description", "Assigns persistent identities to dislocation lines across the frames of a trajectory using a multi-scale matching scheme.");
OVITO_CLASSINFO(DislocationTrackingModifier, "ModifierCategory", "Analysis");
DEFINE_PROPERTY_FIELD(DislocationTrackingModifier, maxMatchingDistance);
DEFINE_PROPERTY_FIELD(DislocationTrackingModifier, burgersTolerance);
DEFINE_PROPERTY_FIELD(DislocationTrackingModifier, maxBridgeGap);
DEFINE_PROPERTY_FIELD(DislocationTrackingModifier, minTrackLength);
DEFINE_PROPERTY_FIELD(DislocationTrackingModifier, colorByTrack);
DEFINE_PROPERTY_FIELD(DislocationTrackingModifier, timePerFrame);
SET_PROPERTY_FIELD_LABEL(DislocationTrackingModifier, maxMatchingDistance, "Max. matching distance (0 = auto)");
SET_PROPERTY_FIELD_LABEL(DislocationTrackingModifier, burgersTolerance, "Burgers vector tolerance");
SET_PROPERTY_FIELD_LABEL(DislocationTrackingModifier, maxBridgeGap, "Max. bridged gap (frames)");
SET_PROPERTY_FIELD_LABEL(DislocationTrackingModifier, minTrackLength, "Min. track length (frames)");
SET_PROPERTY_FIELD_LABEL(DislocationTrackingModifier, colorByTrack, "Color lines by track");
SET_PROPERTY_FIELD_LABEL(DislocationTrackingModifier, timePerFrame, "Time per frame (for velocity)");
SET_PROPERTY_FIELD_UNITS_AND_MINIMUM(DislocationTrackingModifier, timePerFrame, FloatParameterUnit, 0);
SET_PROPERTY_FIELD_UNITS_AND_MINIMUM(DislocationTrackingModifier, maxMatchingDistance, WorldParameterUnit, 0);
SET_PROPERTY_FIELD_UNITS_AND_RANGE(DislocationTrackingModifier, burgersTolerance, PercentParameterUnit, 0, 1);
SET_PROPERTY_FIELD_UNITS_AND_MINIMUM(DislocationTrackingModifier, maxBridgeGap, IntegerParameterUnit, 0);
SET_PROPERTY_FIELD_UNITS_AND_MINIMUM(DislocationTrackingModifier, minTrackLength, IntegerParameterUnit, 1);

IMPLEMENT_CREATABLE_OVITO_CLASS(DislocationTrackingModificationNode);
SET_MODIFICATION_NODE_TYPE(DislocationTrackingModifier, DislocationTrackingModificationNode);

/******************************************************************************
* Asks the modifier whether it can be applied to the given input data.
******************************************************************************/
bool DislocationTrackingModifier::OOMetaClass::isApplicableTo(const DataCollection& input) const
{
    return input.containsObject<DislocationNetwork>();
}

/******************************************************************************
* Is called by the pipeline system before a new modifier evaluation begins.
******************************************************************************/
void DislocationTrackingModifier::preevaluateModifier(const ModifierEvaluationRequest& request, PipelineEvaluationResult::EvaluationTypes& evaluationTypes, TimeInterval& validityInterval) const
{
    DislocationTrackingModificationNode* modNode = dynamic_object_cast<DislocationTrackingModificationNode>(request.modificationNode());
    if(modNode && !modNode->tracksReady()) {
        if(request.interactiveMode())
            evaluationTypes = PipelineEvaluationResult::EvaluationType::Interactive;
        else
            evaluationTypes = PipelineEvaluationResult::EvaluationType::Noninteractive;
    }
}

/******************************************************************************
* Modifies the input data.
******************************************************************************/
Future<PipelineFlowState> DislocationTrackingModifier::evaluateModifier(const ModifierEvaluationRequest& request, PipelineFlowState&& state)
{
    if(!state)
        return std::move(state);

    DislocationTrackingModificationNode* modNode = dynamic_object_cast<DislocationTrackingModificationNode>(request.modificationNode());
    if(!modNode)
        throw Exception(tr("Internal error: The DislocationTrackingModifier is not associated with a valid modification node."));

    // If the trajectory has already been scanned, we can directly apply the cached tracks.
    if(modNode->tracksReady()) {
        modNode->applyTracks(request, state);
        return std::move(state);
    }

    // In interactive mode we cannot scan the whole trajectory. Inform the user and pass the data through unchanged.
    if(request.interactiveMode()) {
        state.setStatus(PipelineStatus(PipelineStatus::Warning, tr("Dislocation tracks have not been computed yet. Render the animation or step through the trajectory to compute them.")));
        return std::move(state);
    }

    // Scan the whole trajectory, then apply the resulting tracks to the current frame.
    return modNode->computeTracks(request).then(ObjectExecutor(modNode), [state = std::move(state), request]() mutable {
        static_object_cast<DislocationTrackingModificationNode>(request.modificationNode())->applyTracks(request, state);
        return std::move(state);
    });
}

/******************************************************************************
* Scans the whole trajectory (if not already done) and assigns persistent track IDs.
******************************************************************************/
SharedFuture<void> DislocationTrackingModificationNode::computeTracks(const ModifierEvaluationRequest& request)
{
    OVITO_ASSERT(request.modificationNode() == this);

    if(_tracksComputed)
        return Future<void>::createImmediateEmpty();

    // Reuse a computation that is already in progress.
    if(SharedFuture<void> existing = _trackingWeakFuture.lock())
        return existing;

    int startFrame = 0;
    int endFrame = numberOfSourceFrames();
    if(endFrame < 1)
        endFrame = 1;

    // Prepare the per-frame storage.
    _frameDescriptors.assign(endFrame, {});
    _frameCells.assign(endFrame, {});
    _trackIds.clear();
    _numTracks = 0;

    auto inputFrameRange = boost::irange(startFrame, endFrame);
    setStatus(tr("Tracking dislocations across %1 trajectory frames...").arg(boost::size(inputFrameRange)));
    auto progress = std::make_unique<TaskProgress>(this_task::ui());
    progress->setText(tr("Tracking dislocations"));
    progress->setMaximum(boost::size(inputFrameRange));

    // Iterate over all frames of the input range in sequential order, collecting segment descriptors.
    SharedFuture<void> trackingFuture = for_each_sequential(
        std::move(inputFrameRange),
        DeferredObjectExecutor(this),
        // Requests the next frame from the upstream pipeline.
        [request = request](int frame) mutable -> SharedFuture<PipelineFlowState> {
            request.setTime(request.modificationNode()->sourceFrameToAnimationTime(frame));
            return request.modificationNode()->evaluateInput(request).asFuture();
        },
        // Collects the descriptors of each frame's dislocation network.
        FrameCollector{this, std::move(progress)});

    // Once all frames have been scanned, run the matching to build the persistent tracks.
    trackingFuture = trackingFuture.then(ObjectExecutor(this), [this]() {
        buildTracks();
    });

    _trackingWeakFuture = trackingFuture;
    return trackingFuture;
}

/******************************************************************************
* Extracts the descriptors of all segments of one trajectory frame.
******************************************************************************/
void DislocationTrackingModificationNode::FrameCollector::operator()(int frame, const PipelineFlowState& state)
{
    if(_progress)
        _progress->incrementValue();

    if(frame < 0 || frame >= (int)_node->_frameDescriptors.size())
        return;

    std::vector<SegmentDescriptor>& descriptors = _node->_frameDescriptors[frame];
    descriptors.clear();

    // Remember the simulation cell so we can apply the minimum image convention when matching.
    _node->_frameCells[frame] = state.getObject<SimulationCell>();

    const DislocationNetwork* network = state.getObject<DislocationNetwork>();
    if(!network)
        return;

    const std::vector<DislocationSegment*>& segments = network->segments();
    descriptors.resize(segments.size());
    for(size_t i = 0; i < segments.size(); i++) {
        const DislocationSegment* seg = segments[i];
        SegmentDescriptor& d = descriptors[i];

        // Centroid = average of the sampling points.
        if(!seg->line.empty()) {
            Vector3 sum = Vector3::Zero();
            for(const Point3& p : seg->line)
                sum += p - Point3::Origin();
            d.centroid = Point3::Origin() + sum / (FloatType)seg->line.size();
        }

        // Spatial Burgers vector and crystal structure of the embedding cluster.
        if(Cluster* cluster = seg->burgersVector.cluster()) {
            d.burgersSpatial = cluster->orientation * seg->burgersVector.localVec();
            d.structure = cluster->structure;

            // Classify the segment into a Burgers vector family (dislocation type), same logic as DislocationVis.
            if(const MicrostructurePhase* phase = network->structureById(cluster->structure)) {
                const BurgersVectorFamily* family = phase->defaultBurgersVectorFamily();
                for(const BurgersVectorFamily* f : phase->burgersVectorFamilies()) {
                    if(f->isMember(seg->burgersVector.localVec(), phase)) { family = f; break; }
                }
                if(family)
                    d.typeId = _node->typeIdForName(family->name());
            }
        }

        d.length = seg->isDegenerate() ? FloatType(0) : seg->calculateLength();
    }
}

/******************************************************************************
* Runs the multi-scale matching over the collected descriptors and fills the track table.
******************************************************************************/
void DislocationTrackingModificationNode::buildTracks()
{
    const int nFrames = (int)_frameDescriptors.size();

    // Read the matching parameters from the modifier.
    FloatType maxDist = 0;
    FloatType burgersTol = FloatType(0.25);
    int maxBridge = 0;
    int minLength = 1;
    FloatType timePerFrame = 1;
    if(DislocationTrackingModifier* mod = dynamic_object_cast<DislocationTrackingModifier>(modifier())) {
        maxDist = mod->maxMatchingDistance();
        burgersTol = mod->burgersTolerance();
        maxBridge = mod->maxBridgeGap();
        minLength = std::max(1, mod->minTrackLength());
        timePerFrame = mod->timePerFrame() > 0 ? mod->timePerFrame() : FloatType(1);
    }

    // Build per-frame offsets into a global node index space (one node per (frame, segment)).
    std::vector<int> offset(nFrames + 1, 0);
    for(int f = 0; f < nFrames; f++)
        offset[f + 1] = offset[f] + (int)_frameDescriptors[f].size();
    const int totalNodes = offset[nFrames];

    // Map every global node back to its frame (used for track length statistics).
    std::vector<int> frameOfNode(totalNodes);
    for(int f = 0; f < nFrames; f++)
        for(int s = 0; s < (int)_frameDescriptors[f].size(); s++)
            frameOfNode[offset[f] + s] = f;

    // Union-find over all (frame, segment) nodes.
    std::vector<int> parent(totalNodes);
    std::iota(parent.begin(), parent.end(), 0);
    std::function<int(int)> findRoot = [&](int x) {
        while(parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; }
        return x;
    };
    auto unite = [&](int a, int b) {
        int ra = findRoot(a), rb = findRoot(b);
        if(ra != rb) parent[ra] = rb;
    };

    // Determine the effective distance threshold: use the user value, or 10% of the smallest cell edge.
    FloatType baseThreshold = maxDist;
    if(baseThreshold <= 0) {
        FloatType minEdge = 0;
        for(const auto& cell : _frameCells) {
            if(!cell) continue;
            const AffineTransformation& m = cell->cellMatrix();
            for(int dim = 0; dim < 3; dim++) {
                FloatType edge = m.column(dim).length();
                if(edge > 0 && (minEdge == 0 || edge < minEdge)) minEdge = edge;
            }
            break;
        }
        baseThreshold = (minEdge > 0) ? FloatType(0.1) * minEdge : FloatType(1e30);
    }

    // Helper: are two segments compatible (same crystal structure, similar Burgers vector and length)?
    auto compatible = [&](const SegmentDescriptor& a, const SegmentDescriptor& b) -> bool {
        if(a.structure != b.structure) return false;
        FloatType bref = std::max(a.burgersSpatial.length(), FloatType(1e-6));
        if((a.burgersSpatial - b.burgersSpatial).length() > burgersTol * bref) return false;
        if(a.length > 0 && b.length > 0) {
            FloatType ratio = std::min(a.length, b.length) / std::max(a.length, b.length);
            if(ratio < FloatType(0.33)) return false;
        }
        return true;
    };

    // Greedy mutual-nearest matching between two frames, uniting the matched segments.
    // 'guard' is an optional predicate to restrict which (frame-a segment, frame-b segment) pairs may be matched.
    auto matchPair = [&](int fa, int fb, FloatType scale, const std::function<bool(int,int)>& guard) {
        const auto& A = _frameDescriptors[fa];
        const auto& B = _frameDescriptors[fb];
        if(A.empty() || B.empty()) return;
        const SimulationCell* cellB = _frameCells[fb] ? _frameCells[fb].get() : nullptr;
        FloatType threshold = baseThreshold * scale;

        struct Candidate { FloatType dist; int i; int j; };
        std::vector<Candidate> candidates;
        for(int i = 0; i < (int)A.size(); i++) {
            for(int j = 0; j < (int)B.size(); j++) {
                if(guard && !guard(i, j)) continue;
                if(!compatible(A[i], B[j])) continue;
                Vector3 delta = B[j].centroid - A[i].centroid;
                if(cellB) delta = cellB->wrapVector(delta);
                FloatType dist = delta.length();
                if(dist > threshold) continue;
                candidates.push_back({dist, i, j});
            }
        }
        std::sort(candidates.begin(), candidates.end(), [](const Candidate& x, const Candidate& y) { return x.dist < y.dist; });

        std::vector<char> usedA(A.size(), 0), usedB(B.size(), 0);
        for(const Candidate& c : candidates) {
            if(usedA[c.i] || usedB[c.j]) continue;
            int ga = offset[fa] + c.i, gb = offset[fb] + c.j;
            if(findRoot(ga) == findRoot(gb)) { usedA[c.i] = usedB[c.j] = 1; continue; }
            usedA[c.i] = usedB[c.j] = 1;
            unite(ga, gb);
        }
    };

    // Fine scale: match every pair of consecutive frames.
    for(int f = 0; f + 1 < nFrames; f++)
        matchPair(f, f + 1, FloatType(1), {});

    // Telescopic coarse scales: bridge dislocations that disappear from the DXA output for up to
    // 'maxBridge' frames. We only connect a track that ends (its component is absent from the
    // intermediate frames) to one that starts after the gap.
    const int maxStride = std::max(1, maxBridge + 1);
    for(int stride = 2; stride <= maxStride; stride++) {
        // Snapshot the frames each component currently appears in.
        std::unordered_set<long long> rootFramePresence;
        rootFramePresence.reserve(totalNodes * 2);
        for(int n = 0; n < totalNodes; n++)
            rootFramePresence.insert((long long)findRoot(n) * nFrames + frameOfNode[n]);
        auto presentInRange = [&](int root, int lo, int hi) -> bool {
            for(int f = lo; f <= hi; f++)
                if(rootFramePresence.count((long long)root * nFrames + f)) return true;
            return false;
        };

        FloatType scale = std::sqrt((FloatType)stride);
        for(int fa = 0; fa + stride < nFrames; fa++) {
            int fb = fa + stride;
            int gapLo = fa + 1, gapHi = fb - 1;
            auto guard = [&](int i, int j) -> bool {
                int ri = findRoot(offset[fa] + i);
                int rj = findRoot(offset[fb] + j);
                if(ri == rj) return false;
                // Only bridge across a genuine gap: neither component may be present inside the gap.
                if(presentInRange(ri, gapLo, gapHi)) return false;
                if(presentInRange(rj, gapLo, gapHi)) return false;
                return true;
            };
            matchPair(fa, fb, scale, guard);
        }
    }

    // Compute, for each component root, the number of distinct frames it spans.
    std::unordered_map<int, std::unordered_set<int>> framesOfRoot;
    for(int n = 0; n < totalNodes; n++)
        framesOfRoot[findRoot(n)].insert(frameOfNode[n]);

    // Assign final, dense track IDs to components long enough to keep; the rest get -1.
    std::unordered_map<int, int> rootToTrack;
    int nextId = 0;
    for(const auto& entry : framesOfRoot) {
        if((int)entry.second.size() >= minLength)
            rootToTrack[entry.first] = nextId++;
        else
            rootToTrack[entry.first] = -1;
    }
    _numTracks = nextId;

    // Fill the per-frame, per-segment track table.
    _trackIds.assign(nFrames, {});
    for(int f = 0; f < nFrames; f++) {
        const int count = (int)_frameDescriptors[f].size();
        _trackIds[f].assign(count, -1);
        for(int s = 0; s < count; s++)
            _trackIds[f][s] = rootToTrack[findRoot(offset[f] + s)];
    }

    // ---- Compute per-frame metrics (velocity, density by type, nucleation/annihilation) ----
    const int numTypes = (int)_typeNames.size();
    _frameMetrics.assign(nFrames, {});
    for(int f = 0; f < nFrames; f++)
        _frameMetrics[f].densityByType.assign(numTypes, FloatType(0));

    // Collect, for each track, its ordered list of (frame, segmentIndex) appearances.
    std::vector<std::vector<std::pair<int,int>>> trackAppearances(_numTracks);
    for(int f = 0; f < nFrames; f++) {
        for(int s = 0; s < (int)_trackIds[f].size(); s++) {
            int tid = _trackIds[f][s];
            if(tid >= 0)
                trackAppearances[tid].push_back({f, s});
        }
    }

    // Per-dislocation velocities, births and deaths from the track appearances.
    std::vector<std::vector<FloatType>> speedsPerFrame(nFrames);
    for(int tid = 0; tid < _numTracks; tid++) {
        const auto& app = trackAppearances[tid];
        if(app.empty()) continue;
        int firstFrame = app.front().first;
        int lastFrame = app.back().first;
        if(firstFrame > 0) _frameMetrics[firstFrame].births++;
        if(lastFrame < nFrames - 1) _frameMetrics[lastFrame].deaths++;
        for(size_t k = 1; k < app.size(); k++) {
            int fa = app[k - 1].first, fb = app[k].first;
            const Point3& ca = _frameDescriptors[fa][app[k - 1].second].centroid;
            const Point3& cb = _frameDescriptors[fb][app[k].second].centroid;
            Vector3 delta = cb - ca;
            if(_frameCells[fb]) delta = _frameCells[fb]->wrapVector(delta);
            FloatType dt = (FloatType)(fb - fa) * timePerFrame;
            if(dt <= 0) dt = timePerFrame;
            speedsPerFrame[fb].push_back(delta.length() / dt);
        }
    }

    // Aggregate density, track counts and velocity statistics per frame.
    for(int f = 0; f < nFrames; f++) {
        FrameMetrics& m = _frameMetrics[f];
        FloatType volume = _frameCells[f] ? _frameCells[f]->volume3D() : FloatType(0);
        FloatType totalLength = 0;
        std::vector<FloatType> lengthByType(numTypes, FloatType(0));
        std::unordered_set<int> presentTracks;
        for(int s = 0; s < (int)_frameDescriptors[f].size(); s++) {
            const SegmentDescriptor& d = _frameDescriptors[f][s];
            totalLength += d.length;
            if(d.typeId >= 0 && d.typeId < numTypes)
                lengthByType[d.typeId] += d.length;
            int tid = _trackIds[f][s];
            if(tid >= 0) presentTracks.insert(tid);
        }
        m.trackCount = (int)presentTracks.size();
        m.totalDensity = (volume > 0) ? totalLength / volume : FloatType(0);
        for(int t = 0; t < numTypes; t++)
            m.densityByType[t] = (volume > 0) ? lengthByType[t] / volume : FloatType(0);

        std::vector<FloatType>& speeds = speedsPerFrame[f];
        if(!speeds.empty()) {
            std::sort(speeds.begin(), speeds.end());
            FloatType sum = 0;
            for(FloatType v : speeds) sum += v;
            m.meanVelocity = sum / (FloatType)speeds.size();
            m.medianVelocity = speeds[speeds.size() / 2];
            m.maxVelocity = speeds.back();
        }
    }

    _tracksComputed = true;
    setStatus(tr("Found %1 dislocation tracks.").arg(_numTracks));
}

/******************************************************************************
* Returns or creates a dense type ID for the given Burgers vector family name.
******************************************************************************/
int DislocationTrackingModificationNode::typeIdForName(const QString& name)
{
    for(int i = 0; i < (int)_typeNames.size(); i++)
        if(_typeNames[i] == name)
            return i;
    _typeNames.push_back(name);
    return (int)_typeNames.size() - 1;
}

/******************************************************************************
* Writes the precomputed track IDs (and optional colors) into the dislocation network.
******************************************************************************/
void DislocationTrackingModificationNode::applyTracks(const ModifierEvaluationRequest& request, PipelineFlowState& state)
{
    if(!state || !_tracksComputed)
        return;

    const DislocationNetwork* input = state.getObject<DislocationNetwork>();
    if(!input)
        return;

    // Determine the source frame this data collection corresponds to.
    int currentFrame = state.data() ? state.data()->sourceFrame() : -1;
    if(currentFrame < 0)
        currentFrame = animationTimeToSourceFrame(request.time());

    bool colorByTrack = true;
    if(DislocationTrackingModifier* mod = dynamic_object_cast<DislocationTrackingModifier>(modifier()))
        colorByTrack = mod->colorByTrack();

    DislocationNetwork* network = state.expectMutableObject<DislocationNetwork>();
    for(DislocationSegment* seg : network->segments()) {
        int tid = trackIdOf(currentFrame, seg->id);
        seg->trackId = tid;
        if(colorByTrack && tid >= 0) {
            // Map the track ID to a well-separated hue using the golden ratio.
            FloatType hue = std::fmod((FloatType)tid * FloatType(0.61803398875), FloatType(1));
            seg->customColor = Color::fromHSV(hue, FloatType(0.85), FloatType(0.95));
        }
        else {
            seg->customColor = Color(-1, -1, -1);
        }
    }

    // Emit the per-frame metrics as global attributes. These are graphable over the trajectory and
    // can be exported to CSV via File > Export File > "Table of Values".
    if(currentFrame >= 0 && currentFrame < (int)_frameMetrics.size()) {
        const FrameMetrics& m = _frameMetrics[currentFrame];
        state.addAttribute(QStringLiteral("DislocationTracking.track_count"), QVariant::fromValue(m.trackCount), this);
        state.addAttribute(QStringLiteral("DislocationTracking.births"), QVariant::fromValue(m.births), this);
        state.addAttribute(QStringLiteral("DislocationTracking.deaths"), QVariant::fromValue(m.deaths), this);
        state.addAttribute(QStringLiteral("DislocationTracking.total_density"), QVariant::fromValue(m.totalDensity), this);
        state.addAttribute(QStringLiteral("DislocationTracking.mean_velocity"), QVariant::fromValue(m.meanVelocity), this);
        state.addAttribute(QStringLiteral("DislocationTracking.median_velocity"), QVariant::fromValue(m.medianVelocity), this);
        state.addAttribute(QStringLiteral("DislocationTracking.max_velocity"), QVariant::fromValue(m.maxVelocity), this);
        for(int t = 0; t < (int)m.densityByType.size() && t < (int)_typeNames.size(); t++) {
            QString safe;
            for(QChar ch : _typeNames[t])
                safe += ch.isLetterOrNumber() ? ch : QChar('_');
            state.addAttribute(QStringLiteral("DislocationTracking.density.%1").arg(safe), QVariant::fromValue(m.densityByType[t]), this);
        }
    }

    state.setStatus(PipelineStatus(PipelineStatus::Success, tr("%1 dislocation tracks").arg(_numTracks)));
}

/******************************************************************************
* Sends an event to all dependents of this RefTarget.
******************************************************************************/
void DislocationTrackingModificationNode::notifyDependentsImpl(const ReferenceEvent& event) noexcept
{
    if(event.type() == ReferenceEvent::TargetChanged) {
        // Discard precomputed tracks when the modifier or the upstream pipeline changes.
        // Keep them when the modifier is only toggled on/off.
        if(static_cast<const TargetChangedEvent&>(event).field() != PROPERTY_FIELD(Modifier::isEnabled) || event.sender() != modifier()) {
            invalidateTrackData();
        }
    }
    ModificationNode::notifyDependentsImpl(event);
}

/******************************************************************************
* Rescales the times of all animation keys from the old interval to the new interval.
******************************************************************************/
void DislocationTrackingModificationNode::rescaleTime(const TimeInterval& oldAnimationInterval, const TimeInterval& newAnimationInterval)
{
    ModificationNode::rescaleTime(oldAnimationInterval, newAnimationInterval);
    invalidateTrackData();
}

/******************************************************************************
* Throws away all precomputed tracking information.
******************************************************************************/
void DislocationTrackingModificationNode::invalidateTrackData()
{
    _tracksComputed = false;
    _numTracks = 0;
    _frameDescriptors.clear();
    _frameCells.clear();
    _trackIds.clear();
    _typeNames.clear();
    _frameMetrics.clear();
    _trackingWeakFuture.reset();
}

}   // End of namespace
