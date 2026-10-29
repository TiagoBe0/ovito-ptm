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
 * \brief Gradient-boosted tree ensemble for the learned vacancy-candidate classification.
 *
 * Loaded from the JSON file written by research/vacancy_analysis/scripts/export_ml_model.py.
 * Evaluation is dependency-free: prob = sigmoid(baseline + sum of leaf values), with the
 * decision rule x[feature] <= threshold -> left child, else right child.
 */
class OVITO_PARTICLES_EXPORT VacancyMLModel
{
public:

    struct Node {
        int32_t feature;
        FloatType threshold;
        FloatType value;
        int32_t left;
        int32_t right;
        bool leaf;
    };

    /// Number of input features the model expects (fixed descriptor layout).
    static constexpr int NUM_FEATURES = 17;

    /// Loads and validates a model file. Throws an Exception on error.
    static std::shared_ptr<const VacancyMLModel> load(const QString& path);

    /// Evaluates the ensemble, returning the vacancy probability.
    FloatType evaluate(const FloatType* features) const;

    /// Sentinel value (in nn units) substituted for "no such site" distances.
    FloatType farSentinel() const { return _farSentinel; }

private:

    std::vector<std::vector<Node>> _trees;
    FloatType _baseline = 0;
    FloatType _farSentinel = 5;
};

/**
 * \brief Consensus point-defect analysis that refines the Wigner-Seitz result.
 *
 * Standard Wigner-Seitz (WS) occupancy analysis overcounts vacancies in heavily deformed,
 * compressed or radiation-damaged regions and at grain boundaries/surfaces, because non-affine
 * atomic displacements push atoms across reference cell boundaries and create spurious,
 * spatially-correlated vacancy-interstitial (Frenkel) pairs.
 *
 * This modifier generates WS candidates internally and then applies a consensus of physical
 * filters intended to separate likely point-defect candidates from deformation artifacts:
 *   1. Frenkel-pair recombination: a candidate vacancy adjacent to an over-occupied site is the
 *      signature of a displaced (not a missing) atom -> annihilate the pair.
 *   2. Free-volume confirmation: a likely vacancy leaves a measurable cavity (the 6 nearest current
 *      atoms stay, on average, about one nn distance away from the empty site).
 *   3. Structural masking: candidates surrounded by disordered atoms (grain boundary / surface /
 *      amorphous) are reported separately, not as point vacancies. The disorder signal is taken
 *      from an upstream PTM/CNA (Structure Type), Centrosymmetry, AtomicStrain (Shear Strain), or
 *      imported LAMMPS per-atom columns such as c_centro/c_coord.
 *   4. Identity ledger: with particle identifiers, each vacant site is followed along its chain of
 *      replacements to the over-occupied site that holds its displaced atom. An interstitial is kept
 *      only if the vacancy at the other end of its chain was confirmed, so the corrected counts obey
 *      the same balance as the raw ones (vacancies - interstitials = missing atoms).
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
        ShearStrainSignal,      ///< Atomic shear strain above an automatic cutoff.
        LammpsCentroSignal,     ///< Imported LAMMPS c_centro column above an automatic cutoff.
        LammpsCoordSignal       ///< Imported LAMMPS c_coord column below an automatic cutoff.
    };
    Q_ENUM(MaskSignalType)

    /// Per-reference-site classification stored in the "Defect Type" output property.
    enum DefectType {
        DefectOK = 0,           ///< Normally occupied site.
        DefectVacancy = 1,      ///< Classified vacancy candidate.
        DefectInterstitial = 2, ///< Over-occupied site (interstitial).
        DefectFrenkel = 3,      ///< Vacancy recombined with an adjacent interstitial (deformation artifact).
        DefectNoCavity = 4,     ///< Empty site without enough local free volume (collapsed/deformation artifact).
        DefectStructural = 5,   ///< Empty site inside a disordered region (grain boundary / amorphous).
        DefectSurfaceOrVoid = 6, ///< Empty site near a free surface, void, or low-density region.
        DefectMLRejected = 7    ///< Empty site the learned classifier rejected as a deformation artifact.
    };

    /// Which reference sites the modifier outputs.
    enum OutputSitesType {
        AllSites,               ///< Every reference site (required for per-site exports).
        WignerSeitzDefectSites, ///< Only the sites plain Wigner-Seitz flags (occupancy != 1).
        DetectedDefectSites     ///< Only the vacancies and interstitials this modifier keeps.
    };
    Q_ENUM(OutputSitesType)

    /// Per-reference-site agreement with plain Wigner-Seitz, stored in "Wigner-Seitz Comparison".
    /// The modifier only removes Wigner-Seitz defects, so a "this method only" class does not exist.
    enum WignerSeitzComparison {
        ComparisonNoDefect = 0,             ///< Singly occupied site: no defect for either method.
        ComparisonVacancyBoth = 1,          ///< Empty site confirmed as a vacancy.
        ComparisonVacancyWSOnly = 2,        ///< Empty site rejected by the filters.
        ComparisonInterstitialBoth = 3,     ///< Over-occupied site that keeps an interstitial.
        ComparisonInterstitialWSOnly = 4    ///< Over-occupied site whose surplus was cancelled.
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
                ConstPropertyPtr identifiers, ConstPropertyPtr refIdentifiers,
                AffineMappingType affineMapping,
                bool frenkelRecombination, FloatType recombinationCutoff,
                bool requireFreeVolume, FloatType cavityRadiusRatio, FloatType nearestAtomRatio,
                bool maskDisordered, MaskSignalType maskSignal, FloatType maskThreshold,
                ConstPropertyPtr structureProperty, ConstPropertyPtr cspProperty, ConstPropertyPtr shearProperty,
                ConstPropertyPtr lammpsCentroProperty, ConstPropertyPtr lammpsCoordProperty,
                ConstPropertyPtr volumetricStrainProperty, ConstPropertyPtr rmsdProperty, ConstPropertyPtr atomicVolumeProperty,
                std::shared_ptr<const VacancyMLModel> mlModel, FloatType mlThreshold,
                OutputSitesType outputSites,
                OOWeakRef<const PipelineNode> createdByNode) :
            Engine(std::move(positions), simCell, std::move(refPositions), simCellRef,
                std::move(identifiers), std::move(refIdentifiers), affineMapping, false),
            _frenkelRecombination(frenkelRecombination), _recombinationCutoff(recombinationCutoff),
            _requireFreeVolume(requireFreeVolume), _cavityRadiusRatio(cavityRadiusRatio), _nearestAtomRatio(nearestAtomRatio),
            _maskDisordered(maskDisordered), _maskSignal(maskSignal), _maskThreshold(maskThreshold),
            _structureProperty(std::move(structureProperty)), _cspProperty(std::move(cspProperty)), _shearProperty(std::move(shearProperty)),
            _lammpsCentroProperty(std::move(lammpsCentroProperty)), _lammpsCoordProperty(std::move(lammpsCoordProperty)),
            _volumetricStrainProperty(std::move(volumetricStrainProperty)), _rmsdProperty(std::move(rmsdProperty)),
            _atomicVolumeProperty(std::move(atomicVolumeProperty)),
            _mlModel(std::move(mlModel)), _mlThreshold(mlThreshold), _outputSites(outputSites),
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
        FloatType _nearestAtomRatio;
        bool _maskDisordered;
        MaskSignalType _maskSignal;
        FloatType _maskThreshold;

        // Upstream per-atom disorder signals (current configuration); may be null.
        ConstPropertyPtr _structureProperty;
        ConstPropertyPtr _cspProperty;
        ConstPropertyPtr _shearProperty;
        ConstPropertyPtr _lammpsCentroProperty;
        ConstPropertyPtr _lammpsCoordProperty;

        // Continuous per-atom deformation context (current configuration); may be null. Unlike the
        // mask signals above these are never thresholded: they are aggregated over the neighborhood
        // of each candidate site and emitted as descriptors, so a downstream classifier (the GBDT,
        // or an unsupervised SOM) sees how deformed the surroundings of a candidate really are.
        ConstPropertyPtr _volumetricStrainProperty;
        ConstPropertyPtr _rmsdProperty;
        ConstPropertyPtr _atomicVolumeProperty;

        // Optional learned classifier (ML mode); null = rule-based classification.
        std::shared_ptr<const VacancyMLModel> _mlModel;
        FloatType _mlThreshold;

        OutputSitesType _outputSites;

        const PipelineFlowState _referenceState;
        OOWeakRef<const PipelineNode> _createdByNode;
    };

    /// Recombine candidate vacancies with adjacent interstitials (removes deformation artifacts).
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(bool{true}, frenkelRecombination, setFrenkelRecombination, PROPERTY_FIELD_MEMORIZE)

    /// Distance cutoff for Frenkel pairing, in simulation units. <= 0 means auto (~1.2 * nn distance).
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(FloatType{0}, recombinationCutoff, setRecombinationCutoff, PROPERTY_FIELD_MEMORIZE)

    /// Require local free volume at a candidate vacancy site.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(bool{true}, requireFreeVolume, setRequireFreeVolume, PROPERTY_FIELD_MEMORIZE)

    /// Minimum cavity radius for a vacancy candidate, expressed as a fraction of the current nn distance.
    /// The cavity radius is the mean distance from the empty site to its 6 nearest current atoms; the
    /// nn distance is the same 6-neighbor mean measured around the atoms, so 1 means a full first shell.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(FloatType{0.85}, cavityRadiusRatio, setCavityRadiusRatio, PROPERTY_FIELD_MEMORIZE)

    /// Minimum distance from a vacancy candidate to its closest current atom, expressed as a fraction of
    /// the median closest-neighbor distance between current atoms (the same statistic, so thermal noise
    /// shrinks both alike). Rejects sites with an atom sitting halfway into them (lattice-mismatch artifacts).
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(FloatType{0.75}, nearestAtomRatio, setNearestAtomRatio, PROPERTY_FIELD_MEMORIZE)

    /// Pair every vacant site with the over-occupied site at the end of its replacement chain (followed
    /// through the particle identifiers) and keep an interstitial only if its vacancy was confirmed.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(bool{true}, identityPairing, setIdentityPairing, PROPERTY_FIELD_MEMORIZE)

    /// Exclude candidate vacancies that sit inside disordered (grain-boundary/surface) regions.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(bool{true}, maskDisordered, setMaskDisordered, PROPERTY_FIELD_MEMORIZE)

    /// Which upstream per-atom signal is used to detect disordered environments.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(MaskSignalType{StructureSignal}, maskSignal, setMaskSignal, PROPERTY_FIELD_MEMORIZE)

    /// Which reference sites are output: all of them, or only the Wigner-Seitz / detected defect sites
    /// (colored by their Wigner-Seitz Comparison class), for a direct visual comparison.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(OutputSitesType{AllSites}, outputSites, setOutputSites)

    /// Fraction of disordered neighbors above which a candidate vacancy is treated as structural.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(FloatType{0.5}, maskThreshold, setMaskThreshold, PROPERTY_FIELD_MEMORIZE)

    /// Classify candidates with the learned (GBDT) model instead of the fixed-threshold rules.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(bool{false}, mlClassification, setMlClassification, PROPERTY_FIELD_MEMORIZE)

    /// Path of the JSON model file written by export_ml_model.py.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(QString{}, mlModelPath, setMlModelPath, PROPERTY_FIELD_MEMORIZE)

    /// Probability above which the learned model classifies a candidate as a true vacancy.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(FloatType{0.5}, mlThreshold, setMlThreshold, PROPERTY_FIELD_MEMORIZE)
};

}   // End of namespace
