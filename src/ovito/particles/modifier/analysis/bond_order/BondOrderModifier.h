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
 * \brief Assigns bond orders to an existing bond topology and perceives aromatic rings and functional groups.
 *
 * Clean-room implementation written from the user manual, the atomic valence penalty scheme of J. Wang et al.,
 * J. Mol. Graph. Model. 25, 247 (2006) and S. Artemova et al., J. Comput. Chem. 37, 1191 (2016). The penalty
 * table and the perception rules were measured on OVITO Pro 3.16.1 as a black box (outputs only):
 *
 * - Each atom has a penalty that depends on its element, its number of bonds (connectivity) and its valence (sum
 *   of bond orders); amide carbons (C(=O)N) may in addition reach valence 5, and their C=O bonds are fixed double.
 *   Entries missing from the table cost ForbiddenPenalty. Elements without a table (H, halogens,
 *   metals, ...) cost nothing. Atoms whose connectivity reaches the element's maximum have all bonds single.
 *   Bonds between two elements without a table stay single.
 * - Bond orders 1..3 minimize the total penalty. Each connected component of optimizable bonds is solved exactly
 *   by branch and bound. Bonds are visited in order of their particle indices, higher orders first, and the first
 *   optimum is kept; with that order ties are broken like OVITO Pro in ~98% of random test molecules. Components
 *   whose search exceeds a node budget (large conjugated networks without a zero-penalty assignment) fall back to
 *   a local search: alternating +1/-1 order changes along paths of any length (as augmenting paths in matching)
 *   and exact re-optimization of molecular fragments of growing size, from 3 up to the fragment size limit.
 * - Aromatic 5- and 6-rings: chordless rings of C, N, O, S atoms whose pi electrons satisfy Hueckel's rule.
 *   An atom already in an aromatic ring contributes one electron; rings with 4-fold coordinated atoms are never
 *   aromatic.
 * - Functional groups (Amide, Aromatic, Carboxylate, Nitro, Amine), with optional fractional bond orders.
 *
 * Deliberate differences from OVITO Pro: integer bond orders are the optimum found (Pro moves the double bond of
 * carboxylate and nitro groups to the bond to the substituent, which gives that atom five bonds); with fractional
 * orders only the C-O / N-O bonds of these groups become 1.5 (4/3 for nitrate), while Pro also changes the bond
 * to the substituent; their bonds get the group's type (Pro: none). Pro also fails with "Invalid aromatic ring"
 * on some small fused cages; this implementation does not.
 *
 * Outputs the bond properties "Bond Order" and "Functional Group", the particle property "Functional Group" and
 * the attribute "BondOrder.PenaltyScore".
 */
class OVITO_PARTICLES_EXPORT BondOrderModifier : public Modifier
{
    /// Give this modifier class its own metaclass.
    class BondOrderModifierClass : public Modifier::OOMetaClass
    {
    public:

        /// Inherit constructor from base metaclass.
        using Modifier::OOMetaClass::OOMetaClass;

        /// Asks the metaclass whether the modifier can be applied to the given input data.
        virtual bool isApplicableTo(const DataCollection& input) const override;
    };

    OVITO_CLASS_META(BondOrderModifier, BondOrderModifierClass)

public:

    /// Functional group type IDs (same numbering as OVITO Pro).
    enum FunctionalGroup : int32_t {
        NoGroup = 0,
        Amide = 1,
        Aromatic = 2,
        Carboxylate = 3,
        Nitro = 4,
        Amine = 5,
        NumGroups
    };

    /// Penalty of an atom whose connectivity or valence is not listed in the table.
    static constexpr int64_t ForbiddenPenalty = 1000;

    /// Result of the bond order assignment.
    struct Result {
        std::vector<int> integerOrders;             ///< Per bond, 1..3.
        std::vector<FloatType> bondOrders;          ///< Per bond; fractional if requested.
        std::vector<int32_t> particleGroups;        ///< Per particle.
        std::vector<int32_t> bondGroups;            ///< Per bond.
        int64_t penaltyScore = 0;
        size_t heuristicComponents = 0;             ///< Components not solved to proven optimality.
    };

    /// Returns the atomic number of a chemical symbol, or 0 if it is not an element.
    static int atomicNumber(const QString& symbol);

    /// Returns the name of a functional group type.
    static QString groupName(int group);

    /// Returns the color of a functional group type (as in OVITO Pro).
    static Color groupColor(int group);

    /// Returns true if the element has a penalty table (C, N, O, Si, P, S).
    static bool hasPenaltyTable(int z);

    /// Connectivity from which all bonds of an atom of the given element are single.
    static int maximumConnectivity(int z);

    /// Penalty of an atom with the given atomic number, connectivity and valence.
    static int64_t atomPenalty(int z, int connectivity, int valence, bool amideCarbon = false);

    /// Flags the carbons with three bonds, a terminal oxygen and a 3-fold coordinated nitrogen (own penalty entry).
    static std::vector<bool> amideCarbons(const std::vector<int>& atomicNumbers, const std::vector<std::pair<size_t, size_t>>& bonds);

    /// Assigns bond orders and functional groups. 'nodeBudget' limits the exact search per component.
    static Result compute(const std::vector<int>& atomicNumbers, const std::vector<std::pair<size_t, size_t>>& bonds,
                          int fragmentSizeLimit, bool redistributeBondOrders, size_t nodeBudget = 2000000);

    /// Is called by the pipeline system before a new modifier evaluation begins.
    virtual void preevaluateModifier(const ModifierEvaluationRequest& request, PipelineEvaluationResult::EvaluationTypes& evaluationTypes, TimeInterval& validityInterval) const override;

    /// Modifies the input data.
    virtual Future<PipelineFlowState> evaluateModifier(const ModifierEvaluationRequest& request, PipelineFlowState&& state) override;

private:

    /// Largest molecular fragment re-optimized by the local search (used only where the exact search gives up).
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(int{5}, fragmentSizeLimit, setFragmentSizeLimit, PROPERTY_FIELD_MEMORIZE);

    /// Replaces the integer orders of aromatic rings and functional groups with fractional ones.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(bool{false}, redistributeBondOrders, setRedistributeBondOrders, PROPERTY_FIELD_MEMORIZE);
};

}   // End of namespace
