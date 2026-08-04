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
#include <ovito/core/dataset/pipeline/DelegatingModifier.h>
#include <ovito/core/dataset/pipeline/PipelineNode.h>
#include "CombineDatasetsModifier.h"

namespace Ovito {

/**
 * \brief Merges an arbitrary number of separate datasets (input files) into one.
 *
 * This is a variant of the CombineDatasetsModifier that can hold a list of secondary
 * data sources instead of a single one. All sources are merged, one after another,
 * into the dataset flowing down the pipeline. It reuses the existing
 * CombineDatasetsModifierDelegate machinery to do the actual per-source merging.
 */
class OVITO_STDMOD_EXPORT CombineMultipleDatasetsModifier : public MultiDelegatingModifier
{
    /// Give this modifier class its own metaclass.
    class CombineMultipleDatasetsModifierClass : public MultiDelegatingModifier::OOMetaClass
    {
    public:

        /// Inherit constructor from base metaclass.
        using MultiDelegatingModifier::OOMetaClass::OOMetaClass;

        /// Return the metaclass of delegates for this modifier type.
        virtual const ModifierDelegate::OOMetaClass& delegateMetaclass() const override { return CombineDatasetsModifierDelegate::OOClass(); }
    };

    OVITO_CLASS_META(CombineMultipleDatasetsModifier, CombineMultipleDatasetsModifierClass)

public:

    /// Constructor.
    void initializeObject(ObjectInitializationFlags flags);

    /// Modifies the input data.
    virtual Future<PipelineFlowState> evaluateModifier(const ModifierEvaluationRequest& request, PipelineFlowState&& state) override;

    /// Returns the number of animation frames this modifier can provide.
    virtual int numberOfOutputFrames(ModificationNode* node) const override {
        int count = MultiDelegatingModifier::numberOfOutputFrames(node);
        for(PipelineNode* source : secondaryDataSources())
            if(source)
                count = std::max(count, source->numberOfSourceFrames());
        return count;
    }

    /// Returns the human-readable labels associated with the animation frames.
    virtual QMap<int, AnimationFrameLabel> animationFrameLabels(QMap<int, AnimationFrameLabel> inputLabels) const override {
        for(PipelineNode* source : secondaryDataSources())
            if(source)
                inputLabels.insert(source->animationFrameLabels());
        return std::move(inputLabels);
    }

    /// Asks the modifier to replace a visual element owned by this modifier with a new compatible object.
    virtual void replaceVisualElement(DataVis* visElement, const std::function<OORef<DataVis>(const QString&)>& getReplacement) override;

    /// Appends a new data source to the end of the list of secondary sources.
    void addDataSource(OORef<PipelineNode> source) {
        _secondaryDataSources.push_back(this, PROPERTY_FIELD(secondaryDataSources), std::move(source));
    }

    /// Removes the secondary data source at the given list index.
    void removeDataSourceByIndex(int index) {
        _secondaryDataSources.remove(this, PROPERTY_FIELD(secondaryDataSources), index);
    }

protected:

    /// Is called when a RefTarget referenced by this object generated an event.
    virtual bool referenceEvent(RefTarget* source, const ReferenceEvent& event) override;

    /// Is called when a reference target is added to a list reference field of this object.
    virtual void referenceInserted(const PropertyFieldDescriptor* field, RefTarget* newTarget, int listIndex) override;

    /// Is called when a reference target is removed from a list reference field of this object.
    virtual void referenceRemoved(const PropertyFieldDescriptor* field, RefTarget* oldTarget, int listIndex) override;

    /// Recursively evaluates and merges the secondary source at the given list index and all following ones.
    [[nodiscard]] Future<PipelineFlowState> combineFromSource(const ModifierEvaluationRequest& request, PipelineFlowState&& state, int index);

    /// Merges a single secondary pipeline state into the accumulating primary state.
    [[nodiscard]] Future<PipelineFlowState> combineOneDataset(const ModifierEvaluationRequest& request, PipelineFlowState&& state, const PipelineFlowState& secondaryState);

private:

    /// The list of sources providing the datasets to be merged into the pipeline.
    DECLARE_MODIFIABLE_VECTOR_REFERENCE_FIELD_FLAGS(OORef<PipelineNode>, secondaryDataSources, setSecondaryDataSources, PROPERTY_FIELD_NO_SUB_ANIM);
};

}   // End of namespace
