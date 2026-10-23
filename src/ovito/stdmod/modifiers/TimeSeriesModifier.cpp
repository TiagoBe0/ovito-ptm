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

#include <ovito/stdmod/StdMod.h>
#include <ovito/stdobj/table/DataTable.h>
#include <ovito/core/app/UserInterface.h>
#include <ovito/core/utilities/concurrent/TaskManager.h>
#include <ovito/core/utilities/concurrent/ForEach.h>
#include <boost/range/irange.hpp>
#include <limits>
#include "TimeSeriesModifier.h"

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(TimeSeriesModifier);
OVITO_CLASSINFO(TimeSeriesModifier, "DisplayName", "Time series");
OVITO_CLASSINFO(TimeSeriesModifier, "Description", "Plot the evolution of global attributes over the whole trajectory.");
OVITO_CLASSINFO(TimeSeriesModifier, "ModifierCategory", "Analysis");
DEFINE_PROPERTY_FIELD(TimeSeriesModifier, sourceAttributes);
DEFINE_PROPERTY_FIELD(TimeSeriesModifier, timeAttribute);
DEFINE_PROPERTY_FIELD(TimeSeriesModifier, samplingFrequency);
DEFINE_PROPERTY_FIELD(TimeSeriesModifier, useCustomInterval);
DEFINE_PROPERTY_FIELD(TimeSeriesModifier, customIntervalStart);
DEFINE_PROPERTY_FIELD(TimeSeriesModifier, customIntervalEnd);
SET_PROPERTY_FIELD_LABEL(TimeSeriesModifier, sourceAttributes, "Input attributes");
SET_PROPERTY_FIELD_LABEL(TimeSeriesModifier, timeAttribute, "Time axis");
SET_PROPERTY_FIELD_LABEL(TimeSeriesModifier, samplingFrequency, "Sample every Nth frame");
SET_PROPERTY_FIELD_LABEL(TimeSeriesModifier, useCustomInterval, "Custom frame interval");
SET_PROPERTY_FIELD_LABEL(TimeSeriesModifier, customIntervalStart, "Start frame");
SET_PROPERTY_FIELD_LABEL(TimeSeriesModifier, customIntervalEnd, "End frame");
SET_PROPERTY_FIELD_UNITS_AND_MINIMUM(TimeSeriesModifier, samplingFrequency, IntegerParameterUnit, 1);
SET_PROPERTY_FIELD_UNITS_AND_MINIMUM(TimeSeriesModifier, customIntervalStart, IntegerParameterUnit, 0);
SET_PROPERTY_FIELD_UNITS_AND_MINIMUM(TimeSeriesModifier, customIntervalEnd, IntegerParameterUnit, 0);
IMPLEMENT_CREATABLE_OVITO_CLASS(TimeSeriesModificationNode);
SET_MODIFICATION_NODE_TYPE(TimeSeriesModifier, TimeSeriesModificationNode);

/******************************************************************************
* Is called by the pipeline system before a new modifier evaluation begins.
******************************************************************************/
void TimeSeriesModifier::preevaluateModifier(const ModifierEvaluationRequest& request, PipelineEvaluationResult::EvaluationTypes& evaluationTypes, TimeInterval& validityInterval) const
{
    // Until the trajectory has been sampled completely, interactive and full evaluations produce different results.
    TimeSeriesModificationNode* modNode = dynamic_object_cast<TimeSeriesModificationNode>(request.modificationNode());
    if(modNode && !modNode->samplingComplete() && !sourceAttributes().empty()) {
        if(request.interactiveMode())
            evaluationTypes = PipelineEvaluationResult::EvaluationType::Interactive;
        else
            evaluationTypes = PipelineEvaluationResult::EvaluationType::Noninteractive;
    }
}

/******************************************************************************
* Modifies the input data.
******************************************************************************/
Future<PipelineFlowState> TimeSeriesModifier::evaluateModifier(const ModifierEvaluationRequest& request, PipelineFlowState&& state)
{
    if(!state)
        return std::move(state);

    TimeSeriesModificationNode* modNode = dynamic_object_cast<TimeSeriesModificationNode>(request.modificationNode());
    if(!modNode)
        throw Exception(tr("Internal error: The TimeSeriesModifier is not associated with a valid modification node."));

    if(sourceAttributes().empty()) {
        state.setStatus(PipelineStatus(PipelineStatus::Warning, tr("No input attributes selected.")));
        return std::move(state);
    }

    // Once the trajectory has been sampled, the cached table can be output directly.
    if(modNode->samplingComplete()) {
        modNode->outputTable(request, state);
        return std::move(state);
    }

    // In interactive mode, output the partial time series collected so far.
    if(request.interactiveMode()) {
        modNode->outputTable(request, state);
        state.setStatus(PipelineStatus(PipelineStatus::Warning, tr("Sampling trajectory frames (%1 so far)...").arg(modNode->samples().size())));
        return std::move(state);
    }

    // Step through the trajectory, then output the complete time series.
    return modNode->sampleTrajectory(request).then(ObjectExecutor(modNode), [state = std::move(state), request]() mutable {
        static_object_cast<TimeSeriesModificationNode>(request.modificationNode())->outputTable(request, state);
        return std::move(state);
    });
}

/******************************************************************************
* Steps through the trajectory (if not already done) and samples the attributes.
******************************************************************************/
SharedFuture<void> TimeSeriesModificationNode::sampleTrajectory(const ModifierEvaluationRequest& request)
{
    OVITO_ASSERT(request.modificationNode() == this);

    if(_samplingComplete)
        return Future<void>::createImmediateEmpty();

    // Reuse a sampling operation that is already in progress.
    if(SharedFuture<void> existing = _samplingWeakFuture.lock())
        return existing;

    const TimeSeriesModifier* mod = static_object_cast<TimeSeriesModifier>(modifier());

    // Determine the range of trajectory frames to be sampled.
    int lastFrame = std::max(numberOfSourceFrames(), 1) - 1;
    int startFrame = 0;
    int endFrame = lastFrame;
    if(mod->useCustomInterval()) {
        startFrame = std::clamp(mod->customIntervalStart(), 0, lastFrame);
        endFrame = std::clamp(mod->customIntervalEnd(), startFrame, lastFrame);
    }
    auto inputFrameRange = boost::irange(startFrame, endFrame + 1, std::max(mod->samplingFrequency(), 1));

    _samples.clear();
    _samples.reserve(boost::size(inputFrameRange));

    setStatus(tr("Sampling %1 trajectory frames...").arg(boost::size(inputFrameRange)));
    auto progress = std::make_unique<TaskProgress>(this_task::ui());
    progress->setText(tr("Computing time series"));
    progress->setMaximum(boost::size(inputFrameRange));

    // Iterate over the frames of the sampling interval in sequential order.
    SharedFuture<void> samplingFuture = for_each_sequential(
        std::move(inputFrameRange),
        DeferredObjectExecutor(this), // Require deferred execution
        // Requests the next frame from the upstream pipeline.
        [request = request](int frame) mutable -> SharedFuture<PipelineFlowState> {
            request.setTime(request.modificationNode()->sourceFrameToAnimationTime(frame));
            return request.modificationNode()->evaluateInput(request).asFuture();
        },
        // Extracts the attribute values of each frame.
        FrameSampler{this, _generation, mod->sourceAttributes(), mod->timeAttribute(), std::move(progress)});

    samplingFuture = samplingFuture.then(ObjectExecutor(this), [this, generation = _generation]() {
        // Ignore the result if the samples were invalidated while the operation was running.
        if(generation == _generation)
            _samplingComplete = true;
    });

    _samplingWeakFuture = samplingFuture;
    return samplingFuture;
}

/******************************************************************************
* Extracts the attribute values of one trajectory frame.
******************************************************************************/
void TimeSeriesModificationNode::FrameSampler::operator()(int frame, const PipelineFlowState& state)
{
    // Stop recording if the samples have been invalidated in the meantime.
    if(_generation != _node->_generation)
        return;

    // Converts an attribute value to a number; NaN if the attribute is missing or not numeric.
    auto numericValue = [](const QVariant& v) {
        bool ok = false;
        double x = v.toDouble(&ok);
        return (v.isValid() && ok) ? x : std::numeric_limits<double>::quiet_NaN();
    };

    QVariantMap attributes = state.data() ? state.data()->buildAttributesMap() : QVariantMap{};

    Sample sample;
    sample.frame = frame;
    if(_timeAttribute.isEmpty()) {
        sample.time = frame;
    }
    else {
        auto iter = attributes.constFind(_timeAttribute);
        if(iter == attributes.constEnd())
            throw Exception(tr("Time series: The time axis attribute '%1' does not exist at trajectory frame %2.").arg(_timeAttribute).arg(frame));
        sample.time = numericValue(iter.value());
    }
    sample.values.reserve(_sourceAttributes.size());
    for(const QString& name : _sourceAttributes)
        sample.values.push_back(numericValue(attributes.value(name)));

    _node->_samples.push_back(std::move(sample));
    _progress->incrementValue();
}

/******************************************************************************
* Outputs the samples collected so far as a data table.
******************************************************************************/
void TimeSeriesModificationNode::outputTable(const ModifierEvaluationRequest& request, PipelineFlowState& state) const
{
    const TimeSeriesModifier* mod = static_object_cast<TimeSeriesModifier>(modifier());
    const QStringList& names = mod->sourceAttributes();
    const size_t rowCount = _samples.size();
    const size_t componentCount = names.size();

    PropertyPtr y = DataTable::OOClass().createUserProperty(DataBuffer::Uninitialized, rowCount, Property::FloatDefault, componentCount,
        componentCount == 1 ? names.front() : tr("Value"), 0, componentCount == 1 ? QStringList{} : names);
    PropertyPtr x = DataTable::OOClass().createUserProperty(DataBuffer::Uninitialized, rowCount, Property::FloatDefault, 1,
        mod->timeAttribute().isEmpty() ? tr("Frame") : mod->timeAttribute());
    {
        BufferWriteAccess<FloatType*, access_mode::discard_write> yAcc(y);
        BufferWriteAccess<FloatType, access_mode::discard_write> xAcc(x);
        for(size_t row = 0; row < rowCount; row++) {
            xAcc[row] = (FloatType)_samples[row].time;
            for(size_t c = 0; c < componentCount; c++)
                yAcc.set(row, c, (FloatType)_samples[row].values[c]);
        }
    }

    DataTable* table = state.createObject<DataTable>(QString::fromLatin1(TimeSeriesModifier::tableIdentifier), request.modificationNode(),
        DataTable::Line, tr("Time series"), std::move(y), std::move(x));
    table->setAxisLabelX(mod->timeAttribute().isEmpty() ? tr("Animation frame") : mod->timeAttribute());
    if(componentCount == 1)
        table->setAxisLabelY(names.front());
}

/******************************************************************************
* Throws away the sampled data and interrupts any sampling in progress.
******************************************************************************/
void TimeSeriesModificationNode::invalidateSamples()
{
    _samplingComplete = false;
    _samples.clear();
    _samplingWeakFuture.reset();
    _generation++;
}

/******************************************************************************
* Sends an event to all dependents of this RefTarget.
******************************************************************************/
void TimeSeriesModificationNode::notifyDependentsImpl(const ReferenceEvent& event) noexcept
{
    if(event.type() == ReferenceEvent::TargetChanged) {
        // Do not discard the sampled data if the modifier is only turned off and on again by the user.
        if(static_cast<const TargetChangedEvent&>(event).field() != PROPERTY_FIELD(Modifier::isEnabled) || event.sender() != modifier()) {
            // Throw away the samples when the modifier or the upstream pipeline change.
            invalidateSamples();
        }
    }
    ModificationNode::notifyDependentsImpl(event);
}

/******************************************************************************
* Rescales the times of all animation keys from the old animation interval to the new interval.
******************************************************************************/
void TimeSeriesModificationNode::rescaleTime(const TimeInterval& oldAnimationInterval, const TimeInterval& newAnimationInterval)
{
    ModificationNode::rescaleTime(oldAnimationInterval, newAnimationInterval);
    invalidateSamples();
}

}   // End of namespace
