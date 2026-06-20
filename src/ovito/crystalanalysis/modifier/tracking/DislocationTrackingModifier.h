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

#pragma once


#include <ovito/crystalanalysis/CrystalAnalysis.h>
#include <ovito/crystalanalysis/objects/DislocationNetwork.h>
#include <ovito/stdobj/simcell/SimulationCell.h>
#include <ovito/core/dataset/pipeline/Modifier.h>
#include <ovito/core/dataset/pipeline/ModificationNode.h>

namespace Ovito {

/**
 * \brief Tracks the identity of dislocation lines across the frames of a trajectory.
 *
 * The modifier expects an upstream DislocationNetwork (produced e.g. by the Dislocation analysis
 * modifier) for every frame. It scans the whole trajectory once, matches dislocation segments
 * between frames using a multi-scale ("telescopic") coarse-to-fine scheme, and assigns a
 * persistent track ID to each segment. The track ID is stored in DislocationSegment::trackId and
 * can be used to color each dislocation with a stable color throughout the animation.
 */
class OVITO_CRYSTALANALYSIS_EXPORT DislocationTrackingModifier : public Modifier
{
    /// Give this modifier class its own metaclass.
    class DislocationTrackingModifierClass : public ModifierClass
    {
    public:

        /// Inherit constructor from base class.
        using ModifierClass::ModifierClass;

        /// Asks the metaclass whether the modifier can be applied to the given input data.
        virtual bool isApplicableTo(const DataCollection& input) const override;
    };

    OVITO_CLASS_META(DislocationTrackingModifier, DislocationTrackingModifierClass)

public:

    /// Is called by the pipeline system before a new modifier evaluation begins.
    virtual void preevaluateModifier(const ModifierEvaluationRequest& request, PipelineEvaluationResult::EvaluationTypes& evaluationTypes, TimeInterval& validityInterval) const override;

    /// Modifies the input data.
    virtual Future<PipelineFlowState> evaluateModifier(const ModifierEvaluationRequest& request, PipelineFlowState&& state) override;

private:

    /// Maximum distance (in simulation units) between the centroids of two segments in
    /// consecutive frames for them to be considered the same dislocation. A value <= 0 means
    /// the threshold is determined automatically from the simulation cell size.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(FloatType{0}, maxMatchingDistance, setMaxMatchingDistance);

    /// Relative tolerance for comparing the (spatial) Burgers vectors of two candidate segments.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(FloatType{0.25}, burgersTolerance, setBurgersTolerance);

    /// Maximum number of consecutive frames a dislocation may be missing from the DXA output and
    /// still have its track bridged across the gap (the "telescopic" coarse scale). 0 disables bridging.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(int{2}, maxBridgeGap, setMaxBridgeGap);

    /// Minimum number of frames a track must span to be kept and colored. Shorter (noise) tracks
    /// keep trackId = -1.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(int{1}, minTrackLength, setMinTrackLength);

    /// Controls whether each segment is assigned a stable per-track color.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(bool{true}, colorByTrack, setColorByTrack);

    /// Physical time between two consecutive trajectory frames, used to convert per-dislocation
    /// displacements into velocities. The reported velocity is in (length unit) / (this unit).
    DECLARE_MODIFIABLE_PROPERTY_FIELD(FloatType{1}, timePerFrame, setTimePerFrame);
};

/**
 * Stores the precomputed trajectory-wide dislocation tracking information for one application of
 * the DislocationTrackingModifier in a pipeline.
 */
class OVITO_CRYSTALANALYSIS_EXPORT DislocationTrackingModificationNode : public ModificationNode
{
    OVITO_CLASS(DislocationTrackingModificationNode)

public:

    /// Compact per-segment descriptor used for matching.
    struct SegmentDescriptor {
        Point3 centroid = Point3::Origin();
        Vector3 burgersSpatial = Vector3::Zero();
        FloatType length = 0;
        int structure = -1;
        int typeId = -1;   ///< Index into _typeNames (Burgers vector family), or -1 if unknown.
    };

    /// Aggregated dislocation metrics computed for a single trajectory frame.
    struct FrameMetrics {
        int trackCount = 0;            ///< Number of distinct tracks present in this frame.
        int births = 0;                ///< Tracks that start (nucleate) in this frame.
        int deaths = 0;                ///< Tracks that end (annihilate) in this frame.
        FloatType totalDensity = 0;    ///< Total dislocation density (line length / cell volume).
        FloatType meanVelocity = 0;    ///< Mean per-dislocation speed in this frame.
        FloatType medianVelocity = 0;  ///< Median per-dislocation speed in this frame.
        FloatType maxVelocity = 0;     ///< Maximum per-dislocation speed in this frame.
        std::vector<FloatType> densityByType;  ///< Density per Burgers vector family (indexed like _typeNames).
    };

    /// Returns true if the trajectory has already been scanned and tracks have been assigned.
    bool tracksReady() const { return _tracksComputed; }

    /// Returns the number of distinct tracks found in the trajectory (after the length filter).
    int numTracks() const { return _numTracks; }

    /// Returns or creates a dense type ID for the given Burgers vector family name.
    int typeIdForName(const QString& name);

    /// Returns the persistent track ID for the given segment of the given frame, or -1 if none.
    int trackIdOf(int frame, int segmentIndex) const {
        if(frame < 0 || frame >= (int)_trackIds.size()) return -1;
        const auto& frameTracks = _trackIds[frame];
        if(segmentIndex < 0 || segmentIndex >= (int)frameTracks.size()) return -1;
        return frameTracks[segmentIndex];
    }

    /// Scans the whole trajectory (if not already done) and assigns persistent track IDs.
    SharedFuture<void> computeTracks(const ModifierEvaluationRequest& request);

    /// Writes the precomputed track IDs (and optional colors) into the dislocation network of the current frame.
    void applyTracks(const ModifierEvaluationRequest& request, PipelineFlowState& state);

protected:

    /// Sends an event to all dependents of this RefTarget.
    virtual void notifyDependentsImpl(const ReferenceEvent& event) noexcept override;

    /// Rescales the times of all animation keys from the old animation interval to the new interval.
    virtual void rescaleTime(const TimeInterval& oldAnimationInterval, const TimeInterval& newAnimationInterval) override;

    /// Throws away all precomputed tracking information.
    void invalidateTrackData();

    /// Runs the multi-scale matching over the collected descriptors and fills the track table.
    void buildTracks();

private:

    /// Background task that scans the trajectory; kept to deduplicate concurrent requests.
    WeakSharedFuture<void> _trackingWeakFuture;

    /// True once _trackIds has been filled.
    bool _tracksComputed = false;

    /// Number of distinct tracks (after length filtering).
    int _numTracks = 0;

    /// Per-frame, per-segment descriptors collected while scanning the trajectory. [frame][segmentIndex]
    std::vector<std::vector<SegmentDescriptor>> _frameDescriptors;

    /// Per-frame minimum-image helper for each scanned frame. [frame]
    std::vector<DataOORef<const SimulationCell>> _frameCells;

    /// Per-frame, per-segment persistent track IDs. [frame][segmentIndex]
    std::vector<std::vector<int>> _trackIds;

    /// Names of the Burgers vector families encountered (dense type IDs index into this).
    std::vector<QString> _typeNames;

    /// Aggregated metrics for each scanned frame. [frame]
    std::vector<FrameMetrics> _frameMetrics;

    /// Working object that collects descriptors for each scanned frame.
    struct FrameCollector {
        DislocationTrackingModificationNode* _node;
        std::unique_ptr<TaskProgress> _progress;

        /// Extracts the descriptors of all segments of one trajectory frame.
        void operator()(int frame, const PipelineFlowState& state);
    };
};

}   // End of namespace
