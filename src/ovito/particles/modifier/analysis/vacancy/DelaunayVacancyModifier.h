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


#include <ovito/particles/Particles.h>
#include <ovito/core/dataset/pipeline/Modifier.h>

namespace Ovito {

/**
 * \brief Reference-free vacancy detection based on Delaunay voids and a locally fitted lattice.
 *
 * Unlike Wigner-Seitz based analysis, this modifier needs no reference configuration, so it keeps
 * working where the original lattice sites are no longer meaningful (e.g. after a displacement
 * cascade has recrystallized with a shifted lattice). Port of detect_delaunay / detect_local_lattice
 * from ML-VAC/software/abtem_vac/compare_methods.py:
 *   1. Delaunay tessellation of the atoms (periodic ghost layer). Every tetrahedron whose
 *      circumsphere is larger than the midpoint between the largest interstitial hole of the perfect
 *      lattice and the nearest-neighbor distance marks a void; its circumcenter is the void center.
 *   2. Void centers closer than 0.7 a0 are merged greedily, largest circumsphere first.
 *   3. (optional) Connected groups of void centers are refined: a cubic lattice (BCC or FCC) is fitted
 *      to the atoms around the group, and every lattice site close to the group that has no atom
 *      assigned is reported as one vacancy. This separates divacancies and vacancy clusters, which
 *      the plain void merge counts as a single void.
 *
 * All length thresholds scale with the lattice constant a0, which is estimated from the median
 * nearest-neighbor distance when not given. Free surfaces (non-periodic directions) produce spurious
 * voids, so the modifier is meant for periodic bulk crystals.
 */
class OVITO_PARTICLES_EXPORT DelaunayVacancyModifier : public Modifier
{
    /// Give this modifier class its own metaclass.
    class DelaunayVacancyModifierClass : public Modifier::OOMetaClass
    {
    public:

        /// Inherit constructor from base metaclass.
        using Modifier::OOMetaClass::OOMetaClass;

        /// Asks the metaclass whether the modifier can be applied to the given input data.
        virtual bool isApplicableTo(const DataCollection& input) const override;
    };

    OVITO_CLASS_META(DelaunayVacancyModifier, DelaunayVacancyModifierClass)

public:

    /// The crystal lattice that the local lattice fit assumes.
    enum LatticeType {
        BCC,
        FCC
    };
    Q_ENUM(LatticeType);

    /// Is called by the pipeline system before a new modifier evaluation begins.
    virtual void preevaluateModifier(const ModifierEvaluationRequest& request, PipelineEvaluationResult::EvaluationTypes& evaluationTypes, TimeInterval& validityInterval) const override;

    /// Modifies the input data.
    virtual Future<PipelineFlowState> evaluateModifier(const ModifierEvaluationRequest& request, PipelineFlowState&& state) override;

    /// Indicates that a preliminary viewport update will be performed immediately after this modifier
    /// has computed new results.
    virtual bool shouldRefreshViewportsAfterEvaluation() override { return true; }

private:

    /// The crystal lattice of the material.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(LatticeType{BCC}, lattice, setLattice, PROPERTY_FIELD_MEMORIZE);

    /// The lattice constant a0 in simulation length units. 0 = estimate from the median nearest-neighbor distance.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(FloatType{0}, latticeConstant, setLatticeConstant, PROPERTY_FIELD_MEMORIZE);

    /// Separate divacancies and vacancy clusters by fitting a local lattice around each group of voids.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(bool{true}, refineWithLocalLattice, setRefineWithLocalLattice, PROPERTY_FIELD_MEMORIZE);

    /// Replace the particles by the detected vacancy sites. If off, the atoms are kept and only the counts are output.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(bool{true}, outputVacancySites, setOutputVacancySites, PROPERTY_FIELD_MEMORIZE);
};

}   // End of namespace
