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
#include <ovito/stdobj/table/DataTable.h>
#include <ovito/core/dataset/pipeline/ModificationNode.h>
#include "DifferenceBetweenFramesModifier.h"
#include <numeric>

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(DifferenceBetweenFramesModifier);
OVITO_CLASSINFO(DifferenceBetweenFramesModifier, "DisplayName", "Difference between frames");
OVITO_CLASSINFO(DifferenceBetweenFramesModifier, "Description", "Compute the change of attributes, data tables or properties relative to a reference frame.");
OVITO_CLASSINFO(DifferenceBetweenFramesModifier, "ModifierCategory", "Analysis");
DEFINE_PROPERTY_FIELD(DifferenceBetweenFramesModifier, operateOn);
DEFINE_PROPERTY_FIELD(DifferenceBetweenFramesModifier, useReferenceFrameOffset);
DEFINE_PROPERTY_FIELD(DifferenceBetweenFramesModifier, referenceFrameNumber);
DEFINE_PROPERTY_FIELD(DifferenceBetweenFramesModifier, referenceFrameOffset);
DEFINE_PROPERTY_FIELD(DifferenceBetweenFramesModifier, tolerateNewElements);
DEFINE_PROPERTY_FIELD(DifferenceBetweenFramesModifier, selectNewElements);
SET_PROPERTY_FIELD_LABEL(DifferenceBetweenFramesModifier, operateOn, "Input quantities");
SET_PROPERTY_FIELD_LABEL(DifferenceBetweenFramesModifier, useReferenceFrameOffset, "Use reference frame offset");
SET_PROPERTY_FIELD_LABEL(DifferenceBetweenFramesModifier, referenceFrameNumber, "Reference frame number");
SET_PROPERTY_FIELD_LABEL(DifferenceBetweenFramesModifier, referenceFrameOffset, "Reference frame offset");
SET_PROPERTY_FIELD_LABEL(DifferenceBetweenFramesModifier, tolerateNewElements, "Tolerate newly appearing elements");
SET_PROPERTY_FIELD_LABEL(DifferenceBetweenFramesModifier, selectNewElements, "Select newly appearing elements");
SET_PROPERTY_FIELD_UNITS_AND_MINIMUM(DifferenceBetweenFramesModifier, referenceFrameNumber, IntegerParameterUnit, 0);

namespace {

const QString differenceSuffix = QStringLiteral(" (Difference)");
constexpr size_t noMatch = std::numeric_limits<size_t>::max();

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

/// Creates the output property "<name> (Difference)" holding current[i] - reference[mapping[i]],
/// with the data type of the current property. Unmatched elements get 0.
void outputDifference(PropertyContainer* container, const Property* current, const Property* reference, const std::vector<size_t>& mapping)
{
    const size_t numComponents = current->componentCount();
    if(reference->componentCount() != numComponents)
        throw Exception(DifferenceBetweenFramesModifier::tr("Difference between frames: The number of components of '%1' differs between the current and the reference frame.").arg(current->name()));
    OVITO_ASSERT(mapping.size() == current->size());

    current->forAnyType([&](auto _) {
        using T = decltype(_);
        std::vector<T> currentValues(current->size() * numComponents);
        std::vector<T> referenceValues(reference->size() * numComponents);
        current->copyTo(currentValues.begin());
        reference->copyTo(referenceValues.begin());

        Property* output = container->createProperty(DataBuffer::Uninitialized, current->name() + differenceSuffix,
            current->dataType(), numComponents, current->componentNames());
        BufferWriteAccess<T*, access_mode::discard_write> access(output);
        T* dst = access.begin();
        for(size_t i = 0; i < mapping.size(); i++) {
            for(size_t c = 0; c < numComponents; c++) {
                if(mapping[i] == noMatch)
                    *dst++ = T(0);
                else
                    *dst++ = static_cast<T>(currentValues[i * numComponents + c] - referenceValues[mapping[i] * numComponents + c]);
            }
        }
    });
}

}   // End of anonymous namespace

/******************************************************************************
* Is called by the pipeline system before a new modifier evaluation begins.
******************************************************************************/
void DifferenceBetweenFramesModifier::preevaluateModifier(const ModifierEvaluationRequest& request, PipelineEvaluationResult::EvaluationTypes& evaluationTypes, TimeInterval& validityInterval) const
{
    // The reference frame is only fetched in non-interactive evaluations.
    if(request.interactiveMode())
        evaluationTypes = PipelineEvaluationResult::EvaluationType::Interactive;
    else
        evaluationTypes = PipelineEvaluationResult::EvaluationType::Noninteractive;

    // With a sliding reference frame, results are only valid for the current frame.
    if(useReferenceFrameOffset())
        validityInterval.intersect(request.time());
}

/******************************************************************************
* Asks the modifier for the set of animation time intervals that should be
* cached by the upstream pipeline.
******************************************************************************/
void DifferenceBetweenFramesModifier::inputCachingHints(ModifierEvaluationRequest& request)
{
    if(useReferenceFrameOffset()) {
        // Shift the requested intervals by the frame offset.
        for(const TimeInterval& iv : TimeIntervalUnion(request.cachingIntervals())) {
            int startFrame = request.modificationNode()->animationTimeToSourceFrame(iv.start());
            int endFrame = request.modificationNode()->animationTimeToSourceFrame(iv.end());
            AnimationTime shiftedStartTime = request.modificationNode()->sourceFrameToAnimationTime(startFrame + referenceFrameOffset());
            AnimationTime shiftedEndTime = request.modificationNode()->sourceFrameToAnimationTime(endFrame + referenceFrameOffset());
            request.mutableCachingIntervals().add(TimeInterval(shiftedStartTime, shiftedEndTime));
        }
    }
    else {
        request.mutableCachingIntervals().add(request.modificationNode()->sourceFrameToAnimationTime(referenceFrameNumber()));
    }

    Modifier::inputCachingHints(request);
}

/******************************************************************************
* Lets the modifier adjust the time interval of a TargetChanged event received
* from the upstream pipeline.
******************************************************************************/
void DifferenceBetweenFramesModifier::restrictInputValidityInterval(TimeInterval& iv) const
{
    Modifier::restrictInputValidityInterval(iv);

    // Any change of the upstream pipeline may affect the reference frame, i.e. all output frames.
    iv.setEmpty();
}

/******************************************************************************
* Modifies the input data.
******************************************************************************/
Future<PipelineFlowState> DifferenceBetweenFramesModifier::evaluateModifier(const ModifierEvaluationRequest& request, PipelineFlowState&& state)
{
    if(!state)
        return std::move(state);

    if(operateOn().empty()) {
        state.setStatus(PipelineStatus(PipelineStatus::Warning, tr("No input quantities selected.")));
        return std::move(state);
    }

    // In interactive mode, do not fetch the reference frame. Reuse a previously computed result if available.
    if(request.interactiveMode()) {
        if(PipelineFlowState cachedState = request.modificationNode()->getCachedPipelineNodeOutput(request.time(), true)) {
            if(cachedState.stateValidity().contains(request.time()))
                return cachedState;
        }
        state.setStatus(PipelineStatus(PipelineStatus::Warning, tr("Waiting for the reference frame...")));
        return std::move(state);
    }

    // Determine the reference frame.
    int referenceFrame;
    if(useReferenceFrameOffset()) {
        int currentFrame = state.data() ? state.data()->sourceFrame() : -1;
        if(currentFrame < 0)
            currentFrame = request.modificationNode()->animationTimeToSourceFrame(request.time());
        referenceFrame = currentFrame + referenceFrameOffset();
    }
    else {
        referenceFrame = referenceFrameNumber();
    }
    const int numFrames = request.modificationNode()->numberOfSourceFrames();
    if(referenceFrame < 0 || (numFrames > 0 && referenceFrame >= numFrames))
        throw Exception(tr("Requested reference frame %1 is out of range. Cannot compute differences at the current animation time.").arg(referenceFrame));

    // Request the reference frame from the upstream pipeline.
    ModifierEvaluationRequest referenceRequest = request;
    referenceRequest.setTime(request.modificationNode()->sourceFrameToAnimationTime(referenceFrame));
    inputCachingHints(referenceRequest);

    return request.modificationNode()->evaluateInput(referenceRequest)
        .then(ObjectExecutor(this), [this, request, referenceFrame, state = std::move(state)](const PipelineFlowState& referenceState) mutable {
            if(referenceState.status().type() == PipelineStatus::Error)
                throw Exception(tr("Reference frame %1 is not available: %2").arg(referenceFrame).arg(referenceState.status().text()));
            if(!referenceState)
                throw Exception(tr("Reference frame %1 is empty.").arg(referenceFrame));
            if(int sourceFrame = referenceState.data()->sourceFrame(); sourceFrame != -1 && sourceFrame != referenceFrame)
                throw Exception(tr("Requested reference frame %1 is out of range.").arg(referenceFrame));

            computeDifferences(request, state, referenceState);
            return std::move(state);
        });
}

/******************************************************************************
* Computes the differences between the current state and the reference state.
******************************************************************************/
void DifferenceBetweenFramesModifier::computeDifferences(const ModifierEvaluationRequest& request, PipelineFlowState& state, const PipelineFlowState& referenceState) const
{
    std::map<QString, size_t> numNewElements;  // Per container path.

    for(const QString& key : operateOn()) {

        if(key.startsWith(QStringLiteral("attribute:"))) {
            const QString name = key.mid(10);
            QVariant current = state.getAttributeValue(name);
            QVariant reference = referenceState.getAttributeValue(name);
            if(!current.isValid())
                throw Exception(tr("Difference between frames: Attribute '%1' does not exist in the current frame.").arg(name));
            if(!reference.isValid())
                throw Exception(tr("Difference between frames: Attribute '%1' does not exist in the reference frame.").arg(name));
            // Like OVITO Pro, the difference of two attributes is always a floating-point value.
            bool ok1 = false, ok2 = false;
            double difference = current.toDouble(&ok1) - reference.toDouble(&ok2);
            if(!ok1 || !ok2)
                throw Exception(tr("Difference between frames: Attribute '%1' is not numeric.").arg(name));
            state.setAttribute(name + differenceSuffix, QVariant::fromValue(difference), request.modificationNode());
        }

        else if(key.startsWith(QStringLiteral("table:"))) {
            const QString identifier = key.mid(6);
            ConstDataObjectPath referencePath = referenceState.data()->getObject(DataTable::OOClass(), identifier);
            if(referencePath.empty())
                throw Exception(tr("Difference between frames: Data table '%1' does not exist in the reference frame.").arg(identifier));
            const DataTable* referenceTable = static_object_cast<DataTable>(referencePath.back());
            ConstDataObjectPath currentPath = state.data()->getObject(DataTable::OOClass(), identifier);
            if(currentPath.empty())
                throw Exception(tr("Difference between frames: Data table '%1' does not exist in the current frame.").arg(identifier));
            DataOORef<const DataTable> table = static_object_cast<DataTable>(currentPath.back());

            if(table->elementCount() != referenceTable->elementCount())
                throw Exception(tr("Difference between frames: The number of rows of data table '%1' differs between the current and the reference frame.").arg(identifier));
            // The x values (or categories) must not change.
            if(table->x()) {
                std::vector<double> xCurrent(table->x()->size() * table->x()->componentCount());
                table->x()->copyTo(xCurrent.begin());
                const Property* xReference = referenceTable->getProperty(table->x()->name());
                std::vector<double> xRef(xReference ? xReference->size() * xReference->componentCount() : 0);
                if(xReference)
                    xReference->copyTo(xRef.begin());
                if(xCurrent != xRef)
                    throw Exception(tr("Difference between frames: The x-axis of data table '%1' differs between the current and the reference frame.").arg(identifier));
            }
            else if(table->intervalStart() != referenceTable->intervalStart() || table->intervalEnd() != referenceTable->intervalEnd()) {
                // Without an x column, the rows are bins of the interval, e.g. a histogram with an automatic range.
                throw Exception(tr("Difference between frames: The x-axis range of data table '%1' differs between the current and the reference frame. "
                    "Use a fixed range in the modifier that generates the table.").arg(identifier));
            }

            // Like OVITO Pro, the differences go into a new table "<identifier>[diff]" that keeps the x column and the plot settings.
            DataTable* output = state.createObject<DataTable>(identifier + QStringLiteral("[diff]"), request.modificationNode(), table->plotMode(), table->title());
            output->setAxisLabelX(table->axisLabelX());
            output->setAxisLabelY(table->axisLabelY());
            output->setIntervalStart(table->intervalStart());
            output->setIntervalEnd(table->intervalEnd());
            output->setElementCount(table->elementCount());
            if(table->x()) {
                output->addProperty(table->x());
                output->setX(table->x());
            }

            std::vector<size_t> identity(table->elementCount());
            std::iota(identity.begin(), identity.end(), size_t(0));
            for(const Property* column : table->properties()) {
                if(column == table->x())
                    continue;
                const Property* referenceColumn = referenceTable->getProperty(column->name());
                if(!referenceColumn)
                    throw Exception(tr("Difference between frames: Column '%1' of data table '%2' does not exist in the reference frame.").arg(column->name()).arg(identifier));
                outputDifference(output, column, referenceColumn, identity);
                if(column == table->y())
                    output->setY(output->getProperty(column->name() + differenceSuffix));
            }
        }

        else if(key.startsWith(QStringLiteral("property:")) && key.lastIndexOf(QChar('/')) > 9) {
            qsizetype slash = key.lastIndexOf(QChar('/'));
            const QString containerPath = key.mid(9, slash - 9);
            const QString name = key.mid(slash + 1);

            ConstDataObjectPath referencePath = referenceState.data()->getObject(PropertyContainer::OOClass(), containerPath);
            if(referencePath.empty())
                throw Exception(tr("Difference between frames: Data object '%1' does not exist in the reference frame.").arg(containerPath));
            const PropertyContainer* referenceContainer = static_object_cast<PropertyContainer>(referencePath.back());
            const Property* referenceProperty = referenceContainer->getProperty(name);
            if(!referenceProperty)
                throw Exception(tr("Difference between frames: Property '%1' of '%2' does not exist in the reference frame.").arg(name).arg(containerPath));

            DataObjectPath currentPath = state.getMutableObject(PropertyContainer::OOClass(), containerPath);
            if(currentPath.empty())
                throw Exception(tr("Difference between frames: Data object '%1' does not exist in the current frame.").arg(containerPath));
            PropertyContainer* container = static_object_cast<PropertyContainer>(currentPath.back());
            const Property* property = container->getProperty(name);
            if(!property)
                throw Exception(tr("Difference between frames: Property '%1' of '%2' does not exist in the current frame.").arg(name).arg(containerPath));

            // Map each current element to its counterpart in the reference frame.
            std::vector<size_t> mapping(container->elementCount(), noMatch);
            std::vector<qlonglong> ids = readIdentifiers(container);
            std::vector<qlonglong> referenceIds = readIdentifiers(referenceContainer);
            if(ids.empty() != referenceIds.empty())
                throw Exception(tr("Difference between frames: '%1' has unique identifiers in only one of the current and the reference frame.").arg(containerPath));
            // Elements that cannot have identifiers (e.g. voxel grid cells, bonds) can only be matched if their number stays the same.
            if(!container->getOOMetaClass().isValidStandardPropertyId(Property::GenericIdentifierProperty) && container->elementCount() != referenceContainer->elementCount())
                throw Exception(tr("Difference between frames: The number of elements of '%1' differs between the current frame (%2) and the reference frame (%3).")
                    .arg(containerPath).arg(container->elementCount()).arg(referenceContainer->elementCount()));
            if(!ids.empty()) {
                std::unordered_map<qlonglong, size_t> idToIndex;
                for(size_t j = 0; j < referenceIds.size(); j++) {
                    if(!idToIndex.emplace(referenceIds[j], j).second)
                        throw Exception(tr("Difference between frames: Detected non-unique identifier %1 in '%2' of the reference frame.").arg(referenceIds[j]).arg(containerPath));
                }
                for(size_t i = 0; i < ids.size(); i++) {
                    if(auto iter = idToIndex.find(ids[i]); iter != idToIndex.end())
                        mapping[i] = iter->second;
                }
            }
            else {
                for(size_t i = 0; i < mapping.size() && i < referenceContainer->elementCount(); i++)
                    mapping[i] = i;
            }

            size_t numUnmatched = std::count(mapping.begin(), mapping.end(), noMatch);
            if(numUnmatched != 0 && !tolerateNewElements()) {
                auto first = std::find(mapping.begin(), mapping.end(), noMatch) - mapping.begin();
                if(!ids.empty())
                    throw Exception(tr("Difference between frames: Element with identifier %1 of '%2' does not exist in the reference frame. Enable the option 'Tolerate newly appearing elements' to ignore it.").arg(ids[first]).arg(containerPath));
                else
                    throw Exception(tr("Difference between frames: '%1' has %2 elements in the current frame but only %3 in the reference frame. Enable the option 'Tolerate newly appearing elements' to ignore the additional ones.")
                        .arg(containerPath).arg(container->elementCount()).arg(referenceContainer->elementCount()));
            }

            outputDifference(container, property, referenceProperty, mapping);

            if(selectNewElements() && container->getOOMetaClass().isValidStandardPropertyId(Property::GenericSelectionProperty)) {
                BufferWriteAccess<SelectionIntType, access_mode::discard_write> selection(
                    container->createProperty(DataBuffer::Uninitialized, Property::GenericSelectionProperty));
                for(size_t i = 0; i < mapping.size(); i++)
                    selection[i] = (mapping[i] == noMatch) ? 1 : 0;
            }
            // Several properties of the same container share the same new elements; count them once.
            numNewElements[containerPath] = numUnmatched;
        }

        else {
            throw Exception(tr("Invalid input quantity '%1'. Expected 'attribute:<name>', 'table:<identifier>' or 'property:<container>/<name>'.").arg(key));
        }
    }

    size_t totalNewElements = 0;
    for(const auto& entry : numNewElements)
        totalNewElements += entry.second;
    if(totalNewElements != 0)
        state.setStatus(PipelineStatus(PipelineStatus::Warning, tr("%1 element(s) do not exist in the reference frame; their difference is set to zero.").arg(totalNewElements)));
}

}   // End of namespace
