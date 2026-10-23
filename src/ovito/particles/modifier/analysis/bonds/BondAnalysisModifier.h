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
 * \brief Computes the bond length and bond angle distributions of a particle system with bonds.
 *
 * Outputs two histogram tables: "bond-length-distr" over [0, lengthCutoff] and "bond-angle-distr"
 * over [0, 180] degrees, or over [-1, 1] if useCosine() is set. Bonds longer than the cutoff are
 * left out of the length histogram but still count for the angles. Periodic images of bonds are
 * taken into account.
 *
 * The distributions can be partitioned into one histogram component per combination of:
 *   - bond types: lengths per type, angles per unordered pair of types;
 *   - bond selection: as bond types, with the two kinds "Unselected" and "Selected";
 *   - particle types: lengths per unordered pair of types, angles per central type and unordered
 *     pair of end types (N^2 (N+1)/2 components, named "<end>-<center>-<end>");
 *   - particle selection: as particle types, with the two species "Unselected" and "Selected".
 */
class OVITO_PARTICLES_EXPORT BondAnalysisModifier : public Modifier
{
    /// Give this modifier class its own metaclass.
    class BondAnalysisModifierClass : public Modifier::OOMetaClass
    {
    public:

        /// Inherit constructor from base metaclass.
        using Modifier::OOMetaClass::OOMetaClass;

        /// Asks the metaclass whether the modifier can be applied to the given input data.
        virtual bool isApplicableTo(const DataCollection& input) const override;
    };

    OVITO_CLASS_META(BondAnalysisModifier, BondAnalysisModifierClass)

public:

    /// How the distributions are broken down into partial histograms.
    enum PartitioningMode {
        Off,
        ByBondType,
        ByBondSelection,
        ByParticleType,
        ByParticleSelection
    };
    Q_ENUM(PartitioningMode);

    /// Identifiers of the output tables.
    static constexpr const char* lengthTableIdentifier = "bond-length-distr";
    static constexpr const char* angleTableIdentifier = "bond-angle-distr";

    /// Is called by the pipeline system before a new modifier evaluation begins.
    virtual void preevaluateModifier(const ModifierEvaluationRequest& request, PipelineEvaluationResult::EvaluationTypes& evaluationTypes, TimeInterval& validityInterval) const override;

    /// Modifies the input data.
    virtual Future<PipelineFlowState> evaluateModifier(const ModifierEvaluationRequest& request, PipelineFlowState&& state) override;

private:

    /// Number of histogram bins (both histograms).
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(int{180}, bins, setBins, PROPERTY_FIELD_MEMORIZE);

    /// Histogram the cosines of the bond angles instead of the angles.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(bool{false}, useCosine, setUseCosine, PROPERTY_FIELD_MEMORIZE);

    /// Upper end of the bond length histogram.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(FloatType{3.2}, lengthCutoff, setLengthCutoff, PROPERTY_FIELD_MEMORIZE);

    /// How the distributions are broken down into partial histograms.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(PartitioningMode{Off}, partition, setPartition);
};

}   // End of namespace
