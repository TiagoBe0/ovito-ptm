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
 * \brief Computes the static structure factor S(k) of a particle system.
 *
 * Clean-room implementation written from the user manual, with details settled by comparing the output
 * of OVITO Pro 3.16.1 on test systems (black box):
 *
 * - Direct method: S(k) = |F(k)|^2 / N with F(k) = sum_j exp(i k.r_j), evaluated on the reciprocal lattice
 *   vectors k = h b1 + k b2 + l b3 of the simulation cell with h, k, l >= 0 (not all zero) and averaged within
 *   each |k| bin. Partial factors: S_ab(k) = Re(F_a* F_b) / N.
 * - Debye method: S(k) = 1 + 4 pi rho / k * integral_0^{L/2} r sin(kr) g(r) w(r) dr with w(r) = sin(2 pi r/L) /
 *   (2 pi r / L), L the longest cell vector, and g(r) computed over all pairs up to L/2 (bins of width 0.01, integrand
 *   at the lower bin edges). Partial factors from the
 *   partial RDFs: A_ab(k) = 1 + 4 pi rho / k * integral r sin(kr) g_ab(r) w(r) dr, S_ab = x_a x_b (A_ab - 1) + x_a delta_ab.
 * - Faber-Ziman factors: A_ab = (S_ab - x_a delta_ab) / (x_a x_b) + 1. The partials satisfy S = sum (2 - delta_ab) S_ab.
 * - X-ray factors: A^X_ab = f_a f_b / (sum_i x_i f_i)^2 A_ab with the Cromer-Mann form factors f(k) evaluated at
 *   sin(theta)/lambda = k / (4 pi); S^X is assembled from the corresponding partials.
 *
 * The k values are the bin centers k_min + (i + 1/2) dk. (OVITO Pro 3.16.1 in Debye mode evaluates S at
 * (i + 1/2) dk but labels the bins starting from k_min, i.e. its curve is shifted when k_min != 0.)
 *
 * A defined type without (selected) particles contributes S_ab = 0 and A_ab = NaN. (With form factors enabled,
 * OVITO Pro 3.16.1 returns NaN for all of S in that case.)
 *
 * Outputs the table "structure-factor" (column "S(k)", one component per type pair if partials are enabled)
 * and, for partials, "faber-ziman-structure-factor" (column "A(k)").
 */
class OVITO_PARTICLES_EXPORT StructureFactorModifier : public Modifier
{
    /// Give this modifier class its own metaclass.
    class StructureFactorModifierClass : public Modifier::OOMetaClass
    {
    public:

        /// Inherit constructor from base metaclass.
        using Modifier::OOMetaClass::OOMetaClass;

        /// Asks the metaclass whether the modifier can be applied to the given input data.
        virtual bool isApplicableTo(const DataCollection& input) const override;
    };

    OVITO_CLASS_META(StructureFactorModifier, StructureFactorModifierClass)

public:

    /// The calculation methods.
    enum Mode {
        Direct,
        Debye
    };
    Q_ENUM(Mode);

    /// Identifiers of the output tables.
    static constexpr const char* tableIdentifier = "structure-factor";
    static constexpr const char* faberZimanTableIdentifier = "faber-ziman-structure-factor";

    /// Returns the Cromer-Mann X-ray form factor of a species at sin(theta)/lambda = s, or NaN if the species is unknown.
    /// Accepts the species names of the International Tables (e.g. "Cu", "Cu1+", "Siv"); a plain element name not found
    /// in the table falls back to its neutral-atom entry with suffix "v" (e.g. "Si" -> "Siv").
    static double formFactor(const QString& species, double s);

    /// Is called by the pipeline system before a new modifier evaluation begins.
    virtual void preevaluateModifier(const ModifierEvaluationRequest& request, PipelineEvaluationResult::EvaluationTypes& evaluationTypes, TimeInterval& validityInterval) const override;

    /// Modifies the input data.
    virtual Future<PipelineFlowState> evaluateModifier(const ModifierEvaluationRequest& request, PipelineFlowState&& state) override;

private:

    /// The calculation method.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(Mode{Debye}, mode, setMode, PROPERTY_FIELD_MEMORIZE);

    /// Smallest k (inclusive).
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(FloatType{0}, kMin, setKMin, PROPERTY_FIELD_MEMORIZE);

    /// Largest k (exclusive).
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(FloatType{10}, kMax, setKMax, PROPERTY_FIELD_MEMORIZE);

    /// Number of k bins.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(int{100}, numberOfBins, setNumberOfBins, PROPERTY_FIELD_MEMORIZE);

    /// Compute partial and Faber-Ziman structure factors.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(bool{false}, computePartials, setComputePartials);

    /// Weight with the atomic X-ray form factors.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(bool{false}, useFormFactors, setUseFormFactors);

    /// Use only the selected particles.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(bool{false}, onlySelected, setOnlySelected);
};

}   // End of namespace
