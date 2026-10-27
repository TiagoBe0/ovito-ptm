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

namespace Ovito {

/**
 * \brief Computes the difference (current minus reference) of one or more input quantities between
 *        the current trajectory frame and a reference frame.
 *
 * Each entry of operateOn() selects one quantity, using one of these forms:
 *   - "attribute:<name>"                 a global attribute
 *   - "table:<identifier>"               all y columns of a data table (the x column must not change)
 *   - "property:<container path>/<name>" an element-wise property, e.g. "property:particles/Velocity"
 *                                        or a voxel grid property, e.g. "property:binning/Count"
 *
 * The reference frame is either fixed (referenceFrameNumber()) or relative to the current frame
 * (referenceFrameOffset(), e.g. -1 for incremental differences). As in OVITO Pro, properties are output
 * next to the inputs with the suffix " (Difference)" and the data type of the input, attribute differences
 * are floating-point, and table columns go into a new table "<identifier>[diff]" (with the x column copied).
 *
 * Element-wise properties are matched across frames by their unique identifiers when the container
 * has them (e.g. particle IDs), otherwise by index. The reference frame may contain more elements
 * than the current one; elements that do not exist in the reference frame raise an error unless
 * tolerateNewElements() is set (their difference is then 0), and can optionally be selected.
 * Elements of containers that cannot have identifiers (voxel grid cells, bonds) must keep their
 * number, and tables without an x column must keep their x range (e.g. a histogram's range).
 *
 * Plain subtraction: periodic boundary conditions are not taken into account.
 */
class OVITO_STDMOD_EXPORT DifferenceBetweenFramesModifier : public Modifier
{
    OVITO_CLASS(DifferenceBetweenFramesModifier)

public:

    /// Is called by the pipeline system before a new modifier evaluation begins.
    virtual void preevaluateModifier(const ModifierEvaluationRequest& request, PipelineEvaluationResult::EvaluationTypes& evaluationTypes, TimeInterval& validityInterval) const override;

    /// Asks the modifier for the set of animation time intervals that should be cached by the upstream pipeline.
    virtual void inputCachingHints(ModifierEvaluationRequest& request) override;

    /// Lets the modifier adjust the time interval of a TargetChanged event received from the upstream pipeline.
    virtual void restrictInputValidityInterval(TimeInterval& iv) const override;

    /// Modifies the input data.
    virtual Future<PipelineFlowState> evaluateModifier(const ModifierEvaluationRequest& request, PipelineFlowState&& state) override;

    /// Returns a short piece of information to be displayed next to the modifier's title in the pipeline editor list.
    virtual QVariant getPipelineEditorShortInfo(Scene* scene, ModificationNode* node) const override { return operateOn().join(QStringLiteral(", ")); }

    /// Computes the differences between the current state and the reference state and writes them into the current state.
    void computeDifferences(const ModifierEvaluationRequest& request, PipelineFlowState& state, const PipelineFlowState& referenceState) const;

private:

    /// The input quantities (see class documentation for the syntax).
    DECLARE_MODIFIABLE_PROPERTY_FIELD(QStringList{}, operateOn, setOperateOn);

    /// Use a reference frame relative to the current frame instead of a fixed one.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(bool{false}, useReferenceFrameOffset, setUseReferenceFrameOffset);

    /// The fixed reference frame.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(int{0}, referenceFrameNumber, setReferenceFrameNumber);

    /// The offset of the reference frame relative to the current frame.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(int{-1}, referenceFrameOffset, setReferenceFrameOffset);

    /// Accept elements that do not exist in the reference frame (their difference is set to 0).
    DECLARE_MODIFIABLE_PROPERTY_FIELD(bool{false}, tolerateNewElements, setTolerateNewElements);

    /// Select the elements that do not exist in the reference frame.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(bool{false}, selectNewElements, setSelectNewElements);
};

}   // End of namespace
