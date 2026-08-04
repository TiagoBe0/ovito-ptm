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
#include <ovito/stdobj/properties/Property.h>
#include <ovito/stdobj/simcell/SimulationCell.h>
#include <ovito/mesh/surface/SurfaceMesh.h>
#include <ovito/core/dataset/data/mesh/TriangleMesh.h>
#include <ovito/core/dataset/data/AttributeDataObject.h>
#include <ovito/core/dataset/io/FileSource.h>
#include <ovito/core/dataset/pipeline/ModificationNode.h>
#include <ovito/core/dataset/animation/AnimationSettings.h>
#include "CombineMultipleDatasetsModifier.h"

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(CombineMultipleDatasetsModifier);
OVITO_CLASSINFO(CombineMultipleDatasetsModifier, "DisplayName", "Combine multiple datasets");
OVITO_CLASSINFO(CombineMultipleDatasetsModifier, "Description", "Merge particles and bonds from several separate input files into one dataset.");
OVITO_CLASSINFO(CombineMultipleDatasetsModifier, "ModifierCategory", "Modification");
DEFINE_VECTOR_REFERENCE_FIELD(CombineMultipleDatasetsModifier, secondaryDataSources);
SET_PROPERTY_FIELD_LABEL(CombineMultipleDatasetsModifier, secondaryDataSources, "Secondary sources");

/******************************************************************************
* Constructor.
******************************************************************************/
void CombineMultipleDatasetsModifier::initializeObject(ObjectInitializationFlags flags)
{
    MultiDelegatingModifier::initializeObject(flags);

    if(!flags.testFlag(ObjectInitializationFlag::DontInitializeObject)) {
        // Generate the list of delegate objects.
        createModifierDelegates(CombineDatasetsModifierDelegate::OOClass());

        // Start with one empty file source, so the user can immediately pick a file.
        // Further sources can be added through the modifier's editor.
        addDataSource(OORef<FileSource>::create(flags));
    }
}

/******************************************************************************
* Replaces any references the modifier has to the given visual element with a new compatible object.
******************************************************************************/
void CombineMultipleDatasetsModifier::replaceVisualElement(DataVis* visElement, const std::function<OORef<DataVis>(const QString&)>& getReplacement)
{
    for(PipelineNode* source : secondaryDataSources()) {
        if(source)
            source->replaceVisualElement(visElement, getReplacement);
    }
}

/******************************************************************************
* Modifies the input data.
******************************************************************************/
Future<PipelineFlowState> CombineMultipleDatasetsModifier::evaluateModifier(const ModifierEvaluationRequest& request, PipelineFlowState&& state)
{
    // Make sure at least one data source has been provided.
    bool hasSource = false;
    for(PipelineNode* source : secondaryDataSources()) {
        if(source) { hasSource = true; break; }
    }
    if(!hasSource)
        throw Exception(tr("No datasets to be merged have been provided. Please add at least one input file."));

    // Merge the sources one after another into the pipeline state.
    return combineFromSource(request, std::move(state), 0);
}

/******************************************************************************
* Recursively evaluates and merges the secondary source at the given list index
* and all following ones.
******************************************************************************/
Future<PipelineFlowState> CombineMultipleDatasetsModifier::combineFromSource(const ModifierEvaluationRequest& request, PipelineFlowState&& state, int index)
{
    // Skip empty entries and find the next valid source in the list.
    while(index < secondaryDataSources().size() && !secondaryDataSources()[index])
        index++;

    // No more sources: return the accumulated result.
    if(index >= secondaryDataSources().size())
        return std::move(state);

    // Kick off evaluation of the current source.
    PipelineEvaluationResult secondaryStateFuture = secondaryDataSources()[index]->evaluate(request);

    // Wait for the data to become available, merge it, then continue with the next source.
    return secondaryStateFuture.then(ObjectExecutor(this), [this, state = std::move(state), request, index](const PipelineFlowState& secondaryState) mutable -> Future<PipelineFlowState> {

        // Make sure the obtained dataset is valid and ready to use.
        if(secondaryState.status().type() == PipelineStatus::Error) {
            PipelineNode* currentSource = secondaryDataSources()[index];
            if(FileSource* fileSource = dynamic_object_cast<FileSource>(currentSource)) {
                if(fileSource->sourceUrls().empty())
                    throw Exception(tr("Please pick an input file for every source to be merged."));
            }
            state.setStatus(secondaryState.status());
            return std::move(state);
        }

        if(!secondaryState)
            throw Exception(tr("One of the secondary data sources is empty. Please pick a valid input file to be merged."));

        // Merge validity intervals of primary and secondary datasets.
        state.intersectStateValidity(secondaryState.stateValidity());

        // Merge this dataset into the accumulating state, then process the remaining sources.
        return combineOneDataset(request, std::move(state), secondaryState)
            .then(ObjectExecutor(this), [this, request, index](PipelineFlowState&& merged) mutable -> Future<PipelineFlowState> {
                return combineFromSource(request, std::move(merged), index + 1);
            });
    });
}

/******************************************************************************
* Merges a single secondary pipeline state into the accumulating primary state.
******************************************************************************/
Future<PipelineFlowState> CombineMultipleDatasetsModifier::combineOneDataset(const ModifierEvaluationRequest& request, PipelineFlowState&& state, const PipelineFlowState& secondaryState)
{
    if(!state || !secondaryState)
        return std::move(state);

    // Merge validity intervals of primary and secondary datasets.
    state.intersectStateValidity(secondaryState.stateValidity());

    // Merge global attributes of primary and secondary datasets.
    for(const DataObject* obj : secondaryState.data()->objects()) {
        if(const AttributeDataObject* attribute = dynamic_object_cast<AttributeDataObject>(obj)) {
            if(state.getAttributeValue(attribute->identifier()).isNull())
                state.addObject(attribute);
        }
    }

    // Combine surface meshes from primary and secondary datasets.
    for(const DataObject* obj : secondaryState.data()->objects()) {
        if(const SurfaceMesh* surfaceMesh = dynamic_object_cast<SurfaceMesh>(obj)) {
            if(!state.data()->contains(surfaceMesh))
                state.addObject(surfaceMesh);
        }
        else if(const TriangleMesh* triMesh = dynamic_object_cast<TriangleMesh>(obj)) {
            if(!state.data()->contains(triMesh))
                state.addObject(triMesh);
        }
    }

    // Special handling for the simulation cell. If the secondary dataset contains a simulation cell but
    // the primary doesn't, then copy it over to the primary dataset.
    if(const SimulationCell* secondaryCell = secondaryState.getObject<SimulationCell>()) {
        if(!state.getObject<SimulationCell>())
            state.addObject(secondaryCell);
    }

    // Let the delegates do their job and merge the data objects of the two datasets.
    return applyDelegates(request, std::move(state), { std::reference_wrapper<const PipelineFlowState>(secondaryState) });
}

/******************************************************************************
* Is called when a RefTarget referenced by this object generated an event.
******************************************************************************/
bool CombineMultipleDatasetsModifier::referenceEvent(RefTarget* source, const ReferenceEvent& event)
{
    if(event.type() == ReferenceEvent::AnimationFramesChanged) {
        // Propagate animation interval events from any of the secondary sources.
        for(PipelineNode* s : secondaryDataSources()) {
            if(s == source)
                return true;
        }
    }
    return MultiDelegatingModifier::referenceEvent(source, event);
}

/******************************************************************************
* Is called when a reference target is added to a list reference field of this object.
******************************************************************************/
void CombineMultipleDatasetsModifier::referenceInserted(const PropertyFieldDescriptor* field, RefTarget* newTarget, int listIndex)
{
    if(field == PROPERTY_FIELD(secondaryDataSources) && !isBeingLoaded() && !isBeingDeleted()) {
        // The animation length might have changed when a secondary source has been added.
        notifyDependents(ReferenceEvent::AnimationFramesChanged);
    }
    MultiDelegatingModifier::referenceInserted(field, newTarget, listIndex);
}

/******************************************************************************
* Is called when a reference target is removed from a list reference field of this object.
******************************************************************************/
void CombineMultipleDatasetsModifier::referenceRemoved(const PropertyFieldDescriptor* field, RefTarget* oldTarget, int listIndex)
{
    if(field == PROPERTY_FIELD(secondaryDataSources) && !isBeingLoaded() && !isBeingDeleted()) {
        // The animation length might have changed when a secondary source has been removed.
        notifyDependents(ReferenceEvent::AnimationFramesChanged);
    }
    MultiDelegatingModifier::referenceRemoved(field, oldTarget, listIndex);
}

}   // End of namespace
