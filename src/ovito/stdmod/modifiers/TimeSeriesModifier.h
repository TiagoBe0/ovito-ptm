////////////////////////////////////////////////////////////////////////////////////////
//
//  Copyright 2025 OVITO GmbH, Germany
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


#include <ovito/stdmod/StdMod.h>
#include <ovito/core/dataset/pipeline/Modifier.h>
#include <ovito/core/dataset/pipeline/ModificationNode.h>

namespace Ovito {

/**
 * \brief Samples one or more global attributes over the frames of the input trajectory and
 *        outputs their time evolution as a data table (one row per sampled frame).
 *
 * The modifier steps through the trajectory once in the background, caching the sampled
 * attribute values in its modification node. Optionally, the value of another global attribute
 * (e.g. "Timestep") is used as the time axis instead of the animation frame number.
 */
class OVITO_STDMOD_EXPORT TimeSeriesModifier : public Modifier
{
    OVITO_CLASS(TimeSeriesModifier)

public:

    /// The identifier of the generated data table.
    static constexpr const char* tableIdentifier = "time-series";

    /// Is called by the pipeline system before a new modifier evaluation begins.
    virtual void preevaluateModifier(const ModifierEvaluationRequest& request, PipelineEvaluationResult::EvaluationTypes& evaluationTypes, TimeInterval& validityInterval) const override;

    /// Modifies the input data.
    virtual Future<PipelineFlowState> evaluateModifier(const ModifierEvaluationRequest& request, PipelineFlowState&& state) override;

    /// Returns a short piece of information to be displayed next to the modifier's title in the pipeline editor list.
    virtual QVariant getPipelineEditorShortInfo(Scene* scene, ModificationNode* node) const override { return sourceAttributes().join(QStringLiteral(", ")); }

private:

    /// The global attributes to be sampled.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(QStringList{}, sourceAttributes, setSourceAttributes);

    /// The global attribute that serves as time axis (empty = animation frame number).
    DECLARE_MODIFIABLE_PROPERTY_FIELD(QString{}, timeAttribute, setTimeAttribute);

    /// Sample only every N-th frame of the trajectory.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(int{1}, samplingFrequency, setSamplingFrequency);

    /// Restricts the sampling to a sub-range of the trajectory.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(bool{false}, useCustomInterval, setUseCustomInterval);

    /// First frame of the custom sampling interval.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(int{0}, customIntervalStart, setCustomIntervalStart, PROPERTY_FIELD_MEMORIZE);

    /// Last frame of the custom sampling interval.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(int{0}, customIntervalEnd, setCustomIntervalEnd, PROPERTY_FIELD_MEMORIZE);
};

/**
 * Stores the attribute values sampled by the TimeSeriesModifier over the whole trajectory.
 */
class OVITO_STDMOD_EXPORT TimeSeriesModificationNode : public ModificationNode
{
    OVITO_CLASS(TimeSeriesModificationNode)

public:

    /// One sampled trajectory frame.
    struct Sample {
        int frame;
        double time;                ///< Value of the time axis at this frame.
        std::vector<double> values; ///< One value per source attribute (NaN if missing or not numeric).
    };

    /// Indicates whether all frames of the sampling interval have been processed.
    bool samplingComplete() const { return _samplingComplete; }

    /// Returns the samples collected so far, in the order of increasing frame number.
    const std::vector<Sample>& samples() const { return _samples; }

    /// Steps through the trajectory (if not already done) and samples the attributes.
    SharedFuture<void> sampleTrajectory(const ModifierEvaluationRequest& request);

    /// Outputs the samples collected so far as a data table.
    void outputTable(const ModifierEvaluationRequest& request, PipelineFlowState& state) const;

protected:

    /// Sends an event to all dependents of this RefTarget.
    virtual void notifyDependentsImpl(const ReferenceEvent& event) noexcept override;

    /// Rescales the times of all animation keys from the old animation interval to the new interval.
    virtual void rescaleTime(const TimeInterval& oldAnimationInterval, const TimeInterval& newAnimationInterval) override;

    /// Throws away the sampled data and interrupts any sampling in progress.
    void invalidateSamples();

private:

    /// The background operation stepping through the trajectory; kept to deduplicate concurrent requests.
    WeakSharedFuture<void> _samplingWeakFuture;

    /// True once all frames of the sampling interval have been processed.
    bool _samplingComplete = false;

    /// The sampled values.
    std::vector<Sample> _samples;

    /// Incremented whenever the samples are invalidated, so that a sampling operation started earlier,
    /// which may still be running, cannot add stale samples or mark the new sampling as complete.
    unsigned int _generation = 0;

    /// Working object that samples the attributes of each trajectory frame.
    struct FrameSampler {
        TimeSeriesModificationNode* _node;
        unsigned int _generation;
        QStringList _sourceAttributes;
        QString _timeAttribute;
        std::unique_ptr<TaskProgress> _progress;

        /// Extracts the attribute values of one trajectory frame.
        void operator()(int frame, const PipelineFlowState& state);
    };
};

}   // End of namespace
