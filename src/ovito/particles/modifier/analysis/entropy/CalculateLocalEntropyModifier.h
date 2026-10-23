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
 * \brief Computes the local pair entropy fingerprint of each particle (Piaggi & Parrinello,
 *        J. Chem. Phys. 147, 114112 (2017)).
 *
 * Uses the same discretization as LAMMPS' compute entropy/atom (bins of width sigma, Gaussian kernel
 * truncated to +/-3 bins, trapezoidal integration), so the parameters have the same meaning. The one
 * deliberate difference: the local density is the neighbor count within the cutoff divided by the
 * cutoff sphere volume, instead of LAMMPS' neighbor-list radius (force cutoff + skin).
 * Particle types are ignored; the model is treated as a single-component system.
 */
class OVITO_PARTICLES_EXPORT CalculateLocalEntropyModifier : public Modifier
{
    /// Give this modifier class its own metaclass.
    class CalculateLocalEntropyModifierClass : public Modifier::OOMetaClass
    {
    public:

        /// Inherit constructor from base metaclass.
        using Modifier::OOMetaClass::OOMetaClass;

        /// Asks the metaclass whether the modifier can be applied to the given input data.
        virtual bool isApplicableTo(const DataCollection& input) const override;
    };

    OVITO_CLASS_META(CalculateLocalEntropyModifier, CalculateLocalEntropyModifierClass)

public:

    /// Is called by the pipeline system before a new modifier evaluation begins.
    virtual void preevaluateModifier(const ModifierEvaluationRequest& request, PipelineEvaluationResult::EvaluationTypes& evaluationTypes, TimeInterval& validityInterval) const override;

    /// Modifies the input data.
    virtual Future<PipelineFlowState> evaluateModifier(const ModifierEvaluationRequest& request, PipelineFlowState&& state) override;

    /// Indicates that a preliminary viewport update will be performed immediately after this modifier
    /// has computed new results.
    virtual bool shouldRefreshViewportsAfterEvaluation() override { return true; }

private:

    /// Cutoff distance for the g(r) calculation.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(FloatType{5.0}, cutoff, setCutoff, PROPERTY_FIELD_MEMORIZE);

    /// Width of the Gaussians used to smooth g(r).
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(FloatType{0.2}, sigma, setSigma, PROPERTY_FIELD_MEMORIZE);

    /// Normalizes g(r) with the local density around each particle instead of the global density.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(bool{false}, useLocalDensity, setUseLocalDensity, PROPERTY_FIELD_MEMORIZE);

    /// Averages the pair entropy over the neighbors within averageCutoff (including the particle itself).
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(bool{false}, computeAverage, setComputeAverage, PROPERTY_FIELD_MEMORIZE);

    /// Cutoff distance for the averaging over neighbors.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(FloatType{5.0}, averageCutoff, setAverageCutoff, PROPERTY_FIELD_MEMORIZE);
};

}   // End of namespace
