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


#include <ovito/particles/Particles.h>
#include <ovito/core/dataset/pipeline/Modifier.h>

namespace Ovito {

/**
 * \brief Classifies the hcp-like atoms of an fcc crystal by the planar defect they belong to
 *        (intrinsic stacking fault, coherent twin boundary, multi-layer stacking fault).
 *
 * Clean-room implementation written from the user manual description only. Input is the
 * "Structure Type" property of a preceding PTM or CNA modifier (hcp = 2 in both). For every hcp atom
 * the 12 nearest neighbors are split into the 6 basal-plane neighbors (the ones that have an
 * antipodal partner, v_i + v_j = 0) and 3 + 3 out-of-plane neighbors on either side of the basal
 * plane. The number of adjacent hcp-like layers then follows from how many out-of-plane neighbors
 * are hcp-like on each side. Unlike Pro, no lattice orientations are needed.
 */
class OVITO_PARTICLES_EXPORT IdentifyFCCPlanarFaultsModifier : public Modifier
{
    /// Give this modifier class its own metaclass.
    class IdentifyFCCPlanarFaultsModifierClass : public Modifier::OOMetaClass
    {
    public:

        /// Inherit constructor from base metaclass.
        using Modifier::OOMetaClass::OOMetaClass;

        /// Asks the metaclass whether the modifier can be applied to the given input data.
        virtual bool isApplicableTo(const DataCollection& input) const override;
    };

    OVITO_CLASS_META(IdentifyFCCPlanarFaultsModifier, IdentifyFCCPlanarFaultsModifierClass)

public:

    /// The values written to the "Planar Fault Type" output property.
    enum PlanarFaultType {
        NonHCP = 0,                 ///< Not hcp-like (perfect fcc, disordered, ...)
        Indeterminate = 1,          ///< Hcp-like but not part of a planar defect
        IntrinsicStackingFault = 2, ///< Two adjacent hcp-like layers
        CoherentTwinBoundary = 3,   ///< A single hcp-like layer
        MultiLayerStackingFault = 4,///< Three or more adjacent hcp-like layers
        NUM_FAULT_TYPES
    };

    /// Is called by the pipeline system before a new modifier evaluation begins.
    virtual void preevaluateModifier(const ModifierEvaluationRequest& request, PipelineEvaluationResult::EvaluationTypes& evaluationTypes, TimeInterval& validityInterval) const override;

    /// Modifies the input data.
    virtual Future<PipelineFlowState> evaluateModifier(const ModifierEvaluationRequest& request, PipelineFlowState&& state) override;

    /// Indicates that a preliminary viewport update will be performed immediately after this modifier
    /// has computed new results.
    virtual bool shouldRefreshViewportsAfterEvaluation() override { return true; }

private:

    /// Controls whether the hcp-like atoms get colored according to their planar fault type.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(bool{true}, colorParticles, setColorParticles);
};

}   // End of namespace
