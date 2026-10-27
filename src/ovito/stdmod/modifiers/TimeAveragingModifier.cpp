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
#include <ovito/stdobj/properties/PropertyContainer.h>
#include <ovito/stdobj/simcell/SimulationCell.h>
#include <ovito/stdobj/table/DataTable.h>
#include <ovito/core/app/UserInterface.h>
#include <ovito/core/utilities/concurrent/TaskManager.h>
#include <ovito/core/utilities/concurrent/ForEach.h>
#include <boost/range/irange.hpp>
#include <cmath>
#include "TimeAveragingModifier.h"

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(TimeAveragingModifier);
OVITO_CLASSINFO(TimeAveragingModifier, "DisplayName", "Time averaging");
OVITO_CLASSINFO(TimeAveragingModifier, "Description", "Compute the time average of attributes, data tables, properties or the simulation cell over the whole trajectory.");
OVITO_CLASSINFO(TimeAveragingModifier, "ModifierCategory", "Analysis");
DEFINE_PROPERTY_FIELD(TimeAveragingModifier, operateOn);
DEFINE_PROPERTY_FIELD(TimeAveragingModifier, overwrite);
DEFINE_PROPERTY_FIELD(TimeAveragingModifier, samplingFrequency);
DEFINE_PROPERTY_FIELD(TimeAveragingModifier, useCustomInterval);
DEFINE_PROPERTY_FIELD(TimeAveragingModifier, customIntervalStart);
DEFINE_PROPERTY_FIELD(TimeAveragingModifier, customIntervalEnd);
SET_PROPERTY_FIELD_LABEL(TimeAveragingModifier, operateOn, "Input quantities");
SET_PROPERTY_FIELD_LABEL(TimeAveragingModifier, overwrite, "Replace original values");
SET_PROPERTY_FIELD_LABEL(TimeAveragingModifier, samplingFrequency, "Sample every Nth frame");
SET_PROPERTY_FIELD_LABEL(TimeAveragingModifier, useCustomInterval, "Custom frame interval");
SET_PROPERTY_FIELD_LABEL(TimeAveragingModifier, customIntervalStart, "Start frame");
SET_PROPERTY_FIELD_LABEL(TimeAveragingModifier, customIntervalEnd, "End frame");
SET_PROPERTY_FIELD_UNITS_AND_MINIMUM(TimeAveragingModifier, samplingFrequency, IntegerParameterUnit, 1);
SET_PROPERTY_FIELD_UNITS_AND_MINIMUM(TimeAveragingModifier, customIntervalStart, IntegerParameterUnit, 0);
SET_PROPERTY_FIELD_UNITS_AND_MINIMUM(TimeAveragingModifier, customIntervalEnd, IntegerParameterUnit, 0);
IMPLEMENT_CREATABLE_OVITO_CLASS(TimeAveragingModificationNode);
SET_MODIFICATION_NODE_TYPE(TimeAveragingModifier, TimeAveragingModificationNode);

namespace {

/// Reads all values of a property (element x component), converted to double.
std::vector<double> readValues(const Property* property)
{
    std::vector<double> values(property->size() * property->componentCount());
    property->copyTo(values.begin());
    return values;
}

/// Reads the unique element identifiers of a property container, if it has them.
std::vector<qlonglong> readIdentifiers(const PropertyContainer* container)
{
    std::vector<qlonglong> ids;
    if(container->getOOMetaClass().isValidStandardPropertyId(Property::GenericIdentifierProperty)) {
        if(const Property* idProperty = container->getProperty(Property::GenericIdentifierProperty)) {
            ids.resize(idProperty->size());
            idProperty->copyTo(ids.begin());
        }
    }
    return ids;
}

/// Overwrites the contents of a property with the given values, rounding them for integer properties.
void writeValues(Property* property, const std::vector<double>& values)
{
    OVITO_ASSERT(values.size() == property->size() * property->componentCount());
    property->forAnyType([&](auto _) {
        using T = decltype(_);
        BufferWriteAccess<T*, access_mode::discard_write> access(property);
        T* dst = access.begin();
        for(double v : values) {
            if constexpr(std::is_integral_v<T>)
                *dst++ = static_cast<T>(std::llround(v));
            else
                *dst++ = static_cast<T>(v);
        }
    });
}

const PropertyContainer* lookupContainer(const PipelineFlowState& state, const QString& path)
{
    ConstDataObjectPath objectPath = state.data()->getObject(PropertyContainer::OOClass(), path);
    return objectPath.empty() ? nullptr : static_object_cast<PropertyContainer>(objectPath.back());
}

const DataTable* lookupTable(const PipelineFlowState& state, const QString& identifier)
{
    ConstDataObjectPath objectPath = state.data()->getObject(DataTable::OOClass(), identifier);
    return objectPath.empty() ? nullptr : static_object_cast<DataTable>(objectPath.back());
}

}   // End of anonymous namespace

/******************************************************************************
* Is called by the pipeline system before a new modifier evaluation begins.
******************************************************************************/
void TimeAveragingModifier::preevaluateModifier(const ModifierEvaluationRequest& request, PipelineEvaluationResult::EvaluationTypes& evaluationTypes, TimeInterval& validityInterval) const
{
    // Until the averages are complete, interactive and full evaluations produce different results.
    TimeAveragingModificationNode* modNode = dynamic_object_cast<TimeAveragingModificationNode>(request.modificationNode());
    if(modNode && !modNode->averagingComplete() && !operateOn().empty()) {
        if(request.interactiveMode())
            evaluationTypes = PipelineEvaluationResult::EvaluationType::Interactive;
        else
            evaluationTypes = PipelineEvaluationResult::EvaluationType::Noninteractive;
    }
}

/******************************************************************************
* Modifies the input data.
******************************************************************************/
Future<PipelineFlowState> TimeAveragingModifier::evaluateModifier(const ModifierEvaluationRequest& request, PipelineFlowState&& state)
{
    if(!state)
        return std::move(state);

    TimeAveragingModificationNode* modNode = dynamic_object_cast<TimeAveragingModificationNode>(request.modificationNode());
    if(!modNode)
        throw Exception(tr("Internal error: The TimeAveragingModifier is not associated with a valid modification node."));

    if(operateOn().empty()) {
        state.setStatus(PipelineStatus(PipelineStatus::Warning, tr("No input quantities selected.")));
        return std::move(state);
    }

    // Once the averages are complete, they can be output directly.
    if(modNode->averagingComplete()) {
        modNode->outputAverages(request, state);
        return std::move(state);
    }

    // In interactive mode, pass the input through until the averaging is done.
    if(request.interactiveMode()) {
        state.setStatus(PipelineStatus(PipelineStatus::Warning, tr("Averaging over trajectory frames...")));
        return std::move(state);
    }

    // Step through the trajectory, then output the averages.
    return modNode->averageTrajectory(request).then(ObjectExecutor(modNode), [state = std::move(state), request]() mutable {
        static_object_cast<TimeAveragingModificationNode>(request.modificationNode())->outputAverages(request, state);
        return std::move(state);
    });
}

/******************************************************************************
* Parses one operateOn() entry.
******************************************************************************/
TimeAveragingModificationNode::Accumulator TimeAveragingModificationNode::parseInputKey(const QString& key)
{
    Accumulator acc;
    if(key == QStringLiteral("cell")) {
        acc.kind = Accumulator::Cell;
    }
    else if(key.startsWith(QStringLiteral("attribute:"))) {
        acc.kind = Accumulator::Attribute;
        acc.name = key.mid(10);
    }
    else if(key.startsWith(QStringLiteral("table:"))) {
        acc.kind = Accumulator::Table;
        acc.name = key.mid(6);
    }
    else if(key.startsWith(QStringLiteral("property:")) && key.lastIndexOf(QChar('/')) > 9) {
        acc.kind = Accumulator::ElementProperty;
        qsizetype slash = key.lastIndexOf(QChar('/'));
        acc.containerPath = key.mid(9, slash - 9);
        acc.name = key.mid(slash + 1);
    }
    else {
        throw Exception(tr("Invalid input quantity '%1'. Expected 'attribute:<name>', 'table:<identifier>', 'property:<container>/<name>' or 'cell'.").arg(key));
    }
    if(acc.kind != Accumulator::Cell && acc.name.isEmpty())
        throw Exception(tr("Invalid input quantity '%1': missing name.").arg(key));
    return acc;
}

/******************************************************************************
* Steps through the trajectory (if not already done) and accumulates the sums.
******************************************************************************/
SharedFuture<void> TimeAveragingModificationNode::averageTrajectory(const ModifierEvaluationRequest& request)
{
    OVITO_ASSERT(request.modificationNode() == this);

    if(_averagingComplete)
        return Future<void>::createImmediateEmpty();

    // Reuse an averaging operation that is already in progress.
    if(SharedFuture<void> existing = _averagingWeakFuture.lock())
        return existing;

    const TimeAveragingModifier* mod = static_object_cast<TimeAveragingModifier>(modifier());

    _accumulators.clear();
    for(const QString& key : mod->operateOn())
        _accumulators.push_back(parseInputKey(key));

    // Determine the range of trajectory frames to be averaged.
    int lastFrame = std::max(numberOfSourceFrames(), 1) - 1;
    int startFrame = 0;
    int endFrame = lastFrame;
    if(mod->useCustomInterval()) {
        startFrame = std::clamp(mod->customIntervalStart(), 0, lastFrame);
        endFrame = std::clamp(mod->customIntervalEnd(), startFrame, lastFrame);
    }
    auto inputFrameRange = boost::irange(startFrame, endFrame + 1, std::max(mod->samplingFrequency(), 1));

    setStatus(tr("Averaging over %1 trajectory frames...").arg(boost::size(inputFrameRange)));
    auto progress = std::make_unique<TaskProgress>(this_task::ui());
    progress->setText(tr("Computing time averages"));
    progress->setMaximum(boost::size(inputFrameRange));

    // Iterate over the frames of the averaging interval in sequential order.
    SharedFuture<void> averagingFuture = for_each_sequential(
        std::move(inputFrameRange),
        DeferredObjectExecutor(this), // Require deferred execution
        // Requests the next frame from the upstream pipeline.
        [request = request](int frame) mutable -> SharedFuture<PipelineFlowState> {
            request.setTime(request.modificationNode()->sourceFrameToAnimationTime(frame));
            return request.modificationNode()->evaluateInput(request).asFuture();
        },
        // Adds each frame's values to the running sums.
        FrameAccumulator{this, _generation, std::move(progress)});

    averagingFuture = averagingFuture.then(ObjectExecutor(this), [this, generation = _generation]() {
        // Ignore the result if the sums were invalidated while the operation was running.
        if(generation == _generation)
            _averagingComplete = true;
    });

    _averagingWeakFuture = averagingFuture;
    return averagingFuture;
}

/******************************************************************************
* Adds the values of one trajectory frame to the running sums.
******************************************************************************/
void TimeAveragingModificationNode::FrameAccumulator::operator()(int frame, const PipelineFlowState& state)
{
    // Stop if the sums have been invalidated in the meantime.
    if(_generation != _node->_generation)
        return;
    if(!state)
        throw Exception(tr("Time averaging: No input data at trajectory frame %1.").arg(frame));

    for(Accumulator& acc : _node->_accumulators) {
        switch(acc.kind) {

        case Accumulator::Attribute: {
            QVariant value = state.getAttributeValue(acc.name);
            bool ok = false;
            double x = value.toDouble(&ok);
            if(!value.isValid() || !ok)
                throw Exception(tr("Time averaging: Attribute '%1' does not exist or is not numeric at trajectory frame %2.").arg(acc.name).arg(frame));
            acc.sums.resize(1, 0.0);
            acc.sums[0] += x;
            break;
        }

        case Accumulator::Cell: {
            const SimulationCell* cell = state.getObject<SimulationCell>();
            if(!cell)
                throw Exception(tr("Time averaging: No simulation cell at trajectory frame %1.").arg(frame));
            acc.sums.resize(12, 0.0);
            for(size_t col = 0; col < 4; col++)
                for(size_t row = 0; row < 3; row++)
                    acc.sums[col * 3 + row] += cell->cellMatrix()(row, col);
            break;
        }

        case Accumulator::Table: {
            const DataTable* table = lookupTable(state, acc.name);
            if(!table)
                throw Exception(tr("Time averaging: Data table '%1' does not exist at trajectory frame %2.").arg(acc.name).arg(frame));
            if(acc.sampleCount == 0) {
                size_t offset = 0;
                for(const Property* column : table->properties()) {
                    acc.columns.emplace_back(column->name(), offset);
                    offset += column->size() * column->componentCount();
                }
                acc.sums.assign(offset, 0.0);
                acc.firstIntervalStart = table->intervalStart();
                acc.firstIntervalEnd = table->intervalEnd();
            }
            else if(table->intervalStart() != acc.firstIntervalStart || table->intervalEnd() != acc.firstIntervalEnd) {
                acc.intervalChanged = true;
            }
            // Without an x column the rows are bins of the interval; if it changes (e.g. a histogram with an
            // automatic range, or spatial bins of a deforming cell), bins are averaged index by index.
            acc.intervalStartSum += table->intervalStart();
            acc.intervalEndSum += table->intervalEnd();
            if(table->properties().size() != acc.columns.size())
                throw Exception(tr("Time averaging: The columns of data table '%1' change at trajectory frame %2.").arg(acc.name).arg(frame));
            for(size_t c = 0; c < acc.columns.size(); c++) {
                const Property* column = table->getProperty(acc.columns[c].first);
                size_t end = (c + 1 < acc.columns.size()) ? acc.columns[c + 1].second : acc.sums.size();
                if(!column || column->size() * column->componentCount() != end - acc.columns[c].second)
                    throw Exception(tr("Time averaging: The size of data table '%1' changes at trajectory frame %2. Only tables with a fixed number of rows can be averaged.").arg(acc.name).arg(frame));
                std::vector<double> values = readValues(column);
                for(size_t i = 0; i < values.size(); i++)
                    acc.sums[acc.columns[c].second + i] += values[i];
            }
            break;
        }

        case Accumulator::ElementProperty: {
            const PropertyContainer* container = lookupContainer(state, acc.containerPath);
            if(!container)
                throw Exception(tr("Time averaging: Data object '%1' does not exist at trajectory frame %2.").arg(acc.containerPath).arg(frame));
            const Property* property = container->getProperty(acc.name);
            if(!property)
                throw Exception(tr("Time averaging: Property '%1' of '%2' does not exist at trajectory frame %3.").arg(acc.name).arg(acc.containerPath).arg(frame));
            std::vector<double> values = readValues(property);
            std::vector<qlonglong> ids = readIdentifiers(container);
            const size_t numComponents = property->componentCount();
            if(acc.sampleCount == 0) {
                acc.componentCount = numComponents;
                acc.sums.assign(values.size(), 0.0);
                for(size_t i = 0; i < ids.size(); i++)
                    acc.idToIndex.emplace(ids[i], i);
                if(!ids.empty() && acc.idToIndex.size() != ids.size())
                    throw Exception(tr("Time averaging: The identifiers of '%1' are not unique.").arg(acc.containerPath));
            }
            if(numComponents != acc.componentCount || values.size() != acc.sums.size())
                throw Exception(tr("Time averaging: The number of elements in '%1' changes at trajectory frame %2. Place the time averaging modifier before any filter that removes elements.").arg(acc.containerPath).arg(frame));
            if(!acc.idToIndex.empty()) {
                // Match elements across frames by their unique identifiers.
                for(size_t i = 0; i < ids.size(); i++) {
                    auto iter = acc.idToIndex.find(ids[i]);
                    if(iter == acc.idToIndex.end())
                        throw Exception(tr("Time averaging: Element with identifier %1 of '%2' appears at trajectory frame %3 but was not present before.").arg(ids[i]).arg(acc.containerPath).arg(frame));
                    for(size_t c = 0; c < numComponents; c++)
                        acc.sums[iter->second * numComponents + c] += values[i * numComponents + c];
                }
            }
            else {
                for(size_t i = 0; i < values.size(); i++)
                    acc.sums[i] += values[i];
            }
            break;
        }
        }
        acc.sampleCount++;
    }
    _progress->incrementValue();
}

/******************************************************************************
* Writes the averaged quantities into the given pipeline state.
******************************************************************************/
void TimeAveragingModificationNode::outputAverages(const ModifierEvaluationRequest& request, PipelineFlowState& state) const
{
    const TimeAveragingModifier* mod = static_object_cast<TimeAveragingModifier>(modifier());
    const bool overwrite = mod->overwrite();

    for(const Accumulator& acc : _accumulators) {
        if(acc.sampleCount == 0)
            continue;
        std::vector<double> means(acc.sums.size());
        for(size_t i = 0; i < means.size(); i++)
            means[i] = acc.sums[i] / acc.sampleCount;

        switch(acc.kind) {

        case Accumulator::Attribute:
            state.setAttribute(overwrite ? acc.name : acc.name + QStringLiteral(" (Average)"), QVariant::fromValue(means[0]), request.modificationNode());
            break;

        case Accumulator::Cell: {
            AffineTransformation cellMatrix;
            for(size_t col = 0; col < 4; col++)
                for(size_t row = 0; row < 3; row++)
                    cellMatrix(row, col) = static_cast<FloatType>(means[col * 3 + row]);
            if(overwrite) {
                state.expectMutableObject<SimulationCell>()->setCellMatrix(cellMatrix);
            }
            else {
                // Same output as OVITO Pro: one vector attribute per cell vector, plus the cell origin.
                for(size_t col = 0; col < 4; col++) {
                    QString name = (col < 3) ? QStringLiteral("Cell vector %1 (Average)").arg(col + 1) : QStringLiteral("Cell origin (Average)");
                    state.setAttribute(name, QVariant::fromValue(cellMatrix.column(col)), request.modificationNode());
                }
            }
            break;
        }

        case Accumulator::Table: {
            const DataTable* input = lookupTable(state, acc.name);
            if(!input)
                break;
            if(overwrite) {
                DataTable* table = static_object_cast<DataTable>(state.getMutableObject(DataTable::OOClass(), acc.name).back());
                for(size_t c = 0; c < acc.columns.size(); c++) {
                    const Property* column = table->getProperty(acc.columns[c].first);
                    size_t end = (c + 1 < acc.columns.size()) ? acc.columns[c + 1].second : means.size();
                    if(!column || column->size() * column->componentCount() != end - acc.columns[c].second)
                        throw Exception(tr("Time averaging: The size of data table '%1' in the current frame differs from the averaged frames.").arg(acc.name));
                    // Like OVITO Pro, the averaged columns become floating-point (histogram counts are not rounded).
                    PropertyPtr averaged = DataTable::OOClass().createUserProperty(DataBuffer::Uninitialized, column->size(), Property::FloatDefault,
                        column->componentCount(), column->name(), 0, column->componentNames());
                    writeValues(averaged.get(), std::vector<double>(means.begin() + acc.columns[c].second, means.begin() + end));
                    const bool isX = table->x() == column, isY = table->y() == column;
                    table->removeProperty(column);
                    table->addProperty(averaged);
                    if(isX) table->setX(averaged);
                    if(isY) table->setY(averaged);
                }
                table->setIntervalStart(acc.intervalChanged ? acc.intervalStartSum / acc.sampleCount : acc.firstIntervalStart);
                table->setIntervalEnd(acc.intervalChanged ? acc.intervalEndSum / acc.sampleCount : acc.firstIntervalEnd);
            }
            else {
                DataTable* table = state.createObject<DataTable>(acc.name + QStringLiteral("[average]"), request.modificationNode(),
                    input->plotMode(), input->title());
                table->setAxisLabelX(input->axisLabelX());
                table->setAxisLabelY(input->axisLabelY());
                table->setIntervalStart(acc.intervalChanged ? acc.intervalStartSum / acc.sampleCount : acc.firstIntervalStart);
                table->setIntervalEnd(acc.intervalChanged ? acc.intervalEndSum / acc.sampleCount : acc.firstIntervalEnd);
                for(size_t c = 0; c < acc.columns.size(); c++) {
                    const Property* column = input->getProperty(acc.columns[c].first);
                    if(!column)
                        continue;
                    size_t end = (c + 1 < acc.columns.size()) ? acc.columns[c + 1].second : means.size();
                    table->setElementCount((end - acc.columns[c].second) / column->componentCount());
                    Property* averaged = table->createProperty(DataBuffer::Uninitialized, column->name(), Property::FloatDefault, column->componentCount(), column->componentNames());
                    writeValues(averaged, std::vector<double>(means.begin() + acc.columns[c].second, means.begin() + end));
                    if(input->x() == column) table->setX(averaged);
                    if(input->y() == column) table->setY(averaged);
                }
            }
            if(acc.intervalChanged && !input->x())
                state.setStatus(PipelineStatus(PipelineStatus::Warning, tr("The x-axis range of data table '%1' changes between frames. "
                    "Its rows were averaged bin by bin and the range was averaged too. Use a fixed range for a strict average.").arg(acc.name)));
            break;
        }

        case Accumulator::ElementProperty: {
            DataObjectPath containerPath = state.getMutableObject(PropertyContainer::OOClass(), acc.containerPath);
            if(containerPath.empty())
                throw Exception(tr("Time averaging: Data object '%1' does not exist in the current frame.").arg(acc.containerPath));
            PropertyContainer* container = static_object_cast<PropertyContainer>(containerPath.back());
            const Property* input = container->getProperty(acc.name);
            if(!input)
                throw Exception(tr("Time averaging: Property '%1' does not exist in the current frame.").arg(acc.name));

            // Bring the averages into the element order of the current frame.
            const size_t numComponents = acc.componentCount;
            std::vector<double> ordered;
            if(!acc.idToIndex.empty()) {
                std::vector<qlonglong> ids = readIdentifiers(container);
                ordered.resize(ids.size() * numComponents);
                for(size_t i = 0; i < ids.size(); i++) {
                    auto iter = acc.idToIndex.find(ids[i]);
                    if(iter == acc.idToIndex.end())
                        throw Exception(tr("Time averaging: Element with identifier %1 in the current frame was not present in the averaged frames.").arg(ids[i]));
                    std::copy_n(means.begin() + iter->second * numComponents, numComponents, ordered.begin() + i * numComponents);
                }
            }
            else {
                ordered = std::move(means);
            }
            if(ordered.size() != container->elementCount() * numComponents || input->componentCount() != numComponents)
                throw Exception(tr("Time averaging: The number of elements in '%1' in the current frame differs from the averaged frames.").arg(acc.containerPath));

            if(overwrite) {
                writeValues(container->makePropertyMutable(input, DataBuffer::Uninitialized), ordered);
            }
            else {
                Property* averaged = container->createProperty(DataBuffer::Uninitialized, acc.name + QStringLiteral(" Average"), Property::FloatDefault, numComponents, input->componentNames());
                writeValues(averaged, ordered);
            }
            break;
        }
        }
    }
}

/******************************************************************************
* Throws away the accumulated sums and interrupts any averaging in progress.
******************************************************************************/
void TimeAveragingModificationNode::invalidateAverages()
{
    _averagingComplete = false;
    _accumulators.clear();
    _averagingWeakFuture.reset();
    _generation++;
}

/******************************************************************************
* Sends an event to all dependents of this RefTarget.
******************************************************************************/
void TimeAveragingModificationNode::notifyDependentsImpl(const ReferenceEvent& event) noexcept
{
    if(event.type() == ReferenceEvent::TargetChanged) {
        // Do not discard the averages if the modifier is only turned off and on again by the user.
        if(static_cast<const TargetChangedEvent&>(event).field() != PROPERTY_FIELD(Modifier::isEnabled) || event.sender() != modifier()) {
            // Throw away the averages when the modifier or the upstream pipeline change.
            invalidateAverages();
        }
    }
    ModificationNode::notifyDependentsImpl(event);
}

/******************************************************************************
* Rescales the times of all animation keys from the old animation interval to the new interval.
******************************************************************************/
void TimeAveragingModificationNode::rescaleTime(const TimeInterval& oldAnimationInterval, const TimeInterval& newAnimationInterval)
{
    ModificationNode::rescaleTime(oldAnimationInterval, newAnimationInterval);
    invalidateAverages();
}

}   // End of namespace
