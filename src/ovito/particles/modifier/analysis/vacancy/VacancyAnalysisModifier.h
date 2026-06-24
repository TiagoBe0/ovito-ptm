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
#include <ovito/stdobj/properties/Property.h>
#include <ovito/stdobj/simcell/SimulationCell.h>
#include <ovito/particles/modifier/analysis/ReferenceConfigurationModifier.h>

namespace Ovito {

/**
 * \brief Robust point-defect analysis that refines the Wigner-Seitz result.
 *
 * Standard Wigner-Seitz (WS) occupancy analysis overcounts vacancies in heavily deformed,
 * compressed or radiation-damaged regions and at grain boundaries/surfaces, because non-affine
 * atomic displacements push atoms across reference cell boundaries and create spurious,
 * spatially-correlated vacancy-interstitial (Frenkel) pairs.
 *
 * This modifier generates WS candidates internally and then applies a consensus of physical
 * filters that a *true* missing atom must satisfy but a deformation artifact does not:
 *   1. Frenkel-pair recombination: a candidate vacancy adjacent to an over-occupied site is the
 *      signature of a displaced (not a missing) atom -> annihilate the pair.
 *   2. Free-volume confirmation: a real vacancy leaves a measurable cavity (no current atom sits
 *      close to the empty site).
 *   3. Structural masking: candidates surrounded by disordered atoms (grain boundary / surface /
 *      amorphous) are reported separately, not as point vacancies. The disorder signal is taken
 *      from an upstream PTM/CNA (Structure Type), Centrosymmetry, or AtomicStrain (Shear Strain).
 *
 * Both the raw WS counts and the corrected counts are emitted as global attributes so the
 * overcount can be quantified directly.
 */
class OVITO_PARTICLES_EXPORT VacancyAnalysisModifier : public ReferenceConfigurationModifier
{
    OVITO_CLASS(VacancyAnalysisModifier)

public:

    /// The per-atom signal used to detect disordered (grain-boundary / surface) environments.
    enum MaskSignalType {
        StructureSignal,        ///< Structure Type == OTHER (from PTM / CNA).
        CentrosymmetrySignal,   ///< Centrosymmetry parameter above an automatic cutoff.
        ShearStrainSignal       ///< Atomic shear strain above an automatic cutoff.
    };
    Q_ENUM(MaskSignalType)

    /// Per-reference-site classification stored in the "Defect Type" output property.
    enum DefectType {
        DefectOK = 0,           ///< Normally occupied site.
        DefectVacancy = 1,      ///< Confirmed true vacancy.
        DefectInterstitial = 2, ///< Over-occupied site (interstitial).
        DefectFrenkel = 3,      ///< Vacancy recombined with an adjacent interstitial (deformation artifact).
        DefectNoCavity = 4,     ///< Empty site without real free volume (collapsed/deformation artifact).
        DefectStructural = 5,   ///< Empty site inside a disordered region (grain boundary / amorphous).
        DefectSurface = 6       ///< Empty site at/near a free surface.
    };

protected:

    /// Creates a computation engine that will compute the modifier's results.
    virtual std::unique_ptr<Engine> createEngine(const ModifierEvaluationRequest& request, const PipelineFlowState& input, const PipelineFlowState& referenceState) override;

    /// Adopts existing computation results for an interactive pipeline evaluation.
    virtual Future<PipelineFlowState> reuseCachedState(const ModifierEvaluationRequest& request, Particles* particles, PipelineFlowState&& output, const PipelineFlowState& cachedState) override;

private:

    /// Computes the modifier's results.
    class VacancyAnalysisEngine : public Engine
    {
    public:

        /// Constructor.
        VacancyAnalysisEngine(ConstPropertyPtr positions, const SimulationCell* simCell,
                PipelineFlowState referenceState, ConstPropertyPtr refPositions, const SimulationCell* simCellRef,
                AffineMappingType affineMapping,
                bool frenkelRecombination, FloatType recombinationCutoff,
                bool requireFreeVolume, FloatType cavityRadiusRatio,
                bool maskDisordered, MaskSignalType maskSignal, FloatType maskThreshold,
                ConstPropertyPtr structureProperty, ConstPropertyPtr cspProperty, ConstPropertyPtr shearProperty,
                OOWeakRef<const PipelineNode> createdByNode) :
            Engine(std::move(positions), simCell, std::move(refPositions), simCellRef,
                nullptr, nullptr, affineMapping, false),
            _frenkelRecombination(frenkelRecombination), _recombinationCutoff(recombinationCutoff),
            _requireFreeVolume(requireFreeVolume), _cavityRadiusRatio(cavityRadiusRatio),
            _maskDisordered(maskDisordered), _maskSignal(maskSignal), _maskThreshold(maskThreshold),
            _structureProperty(std::move(structureProperty)), _cspProperty(std::move(cspProperty)), _shearProperty(std::move(shearProperty)),
            _referenceState(std::move(referenceState)),
            _createdByNode(std::move(createdByNode)) {}

        /// Performs the actual computation of the modifier's results.
        virtual void perform(PipelineFlowState& state) override;

        /// Returns the reference state.
        const PipelineFlowState& referenceState() const { return _referenceState; }

    private:

        // Filter parameters:
        bool _frenkelRecombination;
        FloatType _recombinationCutoff;
        bool _requireFreeVolume;
        FloatType _cavityRadiusRatio;
        bool _maskDisordered;
        MaskSignalType _maskSignal;
        FloatType _maskThreshold;

        // Upstream per-atom disorder signals (current configuration); may be null.
        ConstPropertyPtr _structureProperty;
        ConstPropertyPtr _cspProperty;
        ConstPropertyPtr _shearProperty;

        const PipelineFlowState _referenceState;
        OOWeakRef<const PipelineNode> _createdByNode;
    };

    /// Recombine candidate vacancies with adjacent interstitials (removes deformation artifacts).
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(bool{true}, frenkelRecombination, setFrenkelRecombination, PROPERTY_FIELD_MEMORIZE)

    /// Distance cutoff for Frenkel pairing, in simulation units. <= 0 means auto (~1.2 * nn distance).
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(FloatType{0}, recombinationCutoff, setRecombinationCutoff, PROPERTY_FIELD_MEMORIZE)

    /// Require a real cavity (free volume) at a candidate vacancy site.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(bool{true}, requireFreeVolume, setRequireFreeVolume, PROPERTY_FIELD_MEMORIZE)

    /// Minimum cavity radius for a true vacancy, expressed as a fraction of the nn distance.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(FloatType{0.5}, cavityRadiusRatio, setCavityRadiusRatio, PROPERTY_FIELD_MEMORIZE)

    /// Exclude candidate vacancies that sit inside disordered (grain-boundary/surface) regions.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(bool{true}, maskDisordered, setMaskDisordered, PROPERTY_FIELD_MEMORIZE)

    /// Which upstream per-atom signal is used to detect disordered environments.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(MaskSignalType{StructureSignal}, maskSignal, setMaskSignal, PROPERTY_FIELD_MEMORIZE)

    /// Fraction of disordered neighbors above which a candidate vacancy is treated as structural.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(FloatType{0.5}, maskThreshold, setMaskThreshold, PROPERTY_FIELD_MEMORIZE)
};

}   // End of namespace
