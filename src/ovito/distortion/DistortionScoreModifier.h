////////////////////////////////////////////////////////////////////////////////////////
//
//  Copyright 2025 OVITO GmbH, Germany
//
//  This file is part of OVITO (Open Visualization Tool).
//
//  OVITO is free software; you can redistribute it and/or modify it either under the
//  terms of the GNU General Public License version 3 as published by the Free Software
//  Foundation (the "GPL") or, at your option, under the terms of the MIT License.
//
////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <memory>
#include <mutex>
#include <vector>

#include <ovito/core/dataset/pipeline/Modifier.h>
#include <ovito/core/dataset/pipeline/PipelineFlowState.h>
#include <ovito/particles/Particles.h>
#include <ovito/stdobj/simcell/SimulationCell.h>
#include <ovito/stdobj/properties/Property.h>

namespace Ovito {

/**
 * \brief Modifier that computes a per-atom distortion score using the
 *        Minimum Covariance Determinant (MCD) estimator and the robust
 *        Mahalanobis distance, as described in:
 *
 *  Goryaeva et al., "Reinforcing materials modelling by encoding the structures
 *  of defects in crystalline solids into distortion scores",
 *  Nature Communications 11, 4129 (2020).
 *
 * Workflow:
 *   1. Train the reference model on a defect-free (or nearly defect-free)
 *      configuration by clicking "Train on current frame" in the GUI panel.
 *      The model (robust mean + inverse covariance) is saved to a JSON file.
 *   2. Apply the modifier to any downstream configuration.  For each atom the
 *      modifier computes:
 *        - "DistortionScore_MCD" (float): robust Mahalanobis distance d_RB.
 *        - "IsDefect_MCD"        (int32): 1 if d_RB > threshold, 0 otherwise.
 *
 * Descriptor:
 *   Sorted, normalised neighbour distances within cutoffRadius(), padded to
 *   numNeighbors() elements (same representation as the existing NN modifiers).
 *
 * MCD algorithm:
 *   C-step iterations starting from the full-sample mean and covariance.
 *   Each C-step retains h = floor((1 - contaminationFactor) * N) atoms
 *   with the smallest Mahalanobis distance, then recomputes mean and
 *   covariance from the subset.  A small regularisation term (regularization *
 *   I) is added to the covariance before inversion to ensure invertibility.
 */
class OVITO_DISTORTIONSCOREPLUGIN_EXPORT DistortionScoreModifier : public Modifier
{
    /// Give this modifier class its own metaclass.
    class OVITO_DISTORTIONSCOREPLUGIN_EXPORT OOMetaClass : public Modifier::OOMetaClass
    {
    public:
        using Modifier::OOMetaClass::OOMetaClass;

        /// Returns true when the modifier can be applied to the given data.
        virtual bool isApplicableTo(const DataCollection& input) const override;
    };

    OVITO_CLASS_META(DistortionScoreModifier, OOMetaClass)

public:

    /// Human-readable title shown in the pipeline panel.
    virtual QString objectTitle() const override { return tr("Distortion Score Modifier"); }

    /// Main pipeline evaluation: computes distortion scores for every atom.
    virtual Future<PipelineFlowState> evaluateModifier(
        const ModifierEvaluationRequest& request,
        PipelineFlowState&& input) override;

    // -----------------------------------------------------------------------
    // Descriptor parameters
    // -----------------------------------------------------------------------

    /// Cutoff radius used to build the sorted neighbour-distance descriptor.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(FloatType{5.0}, cutoffRadius, setCutoffRadius);

    /// Number of nearest neighbours included in the descriptor vector.
    /// Must match the value used during training.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(int{16}, numNeighbors, setNumNeighbors);

    // -----------------------------------------------------------------------
    // MCD training parameters (stored for reference / re-training)
    // -----------------------------------------------------------------------

    /// Fraction of atoms assumed to be outliers in the training set.
    /// h = floor((1 - contaminationFactor) * N) atoms are retained per C-step.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(FloatType{0.07}, contaminationFactor, setContaminationFactor);

    /// Tikhonov regularisation added to the diagonal of the covariance matrix
    /// before inversion (prevents singularity for collinear descriptors).
    DECLARE_MODIFIABLE_PROPERTY_FIELD(FloatType{1e-6}, regularization, setRegularization);

    /// Number of C-step iterations in the simplified FAST-MCD algorithm.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(int{10}, numCSteps, setNumCSteps);

    // -----------------------------------------------------------------------
    // Inference / output parameters
    // -----------------------------------------------------------------------

    /// Distortion scores above this value are labelled as defects.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(FloatType{3.0}, threshold, setThreshold);

    /// Path to the JSON file that stores the trained MCD model
    /// (mean vector + flattened inverse covariance matrix).
    DECLARE_MODIFIABLE_PROPERTY_FIELD(QString{}, modelFilePath, setModelFilePath);

    // -----------------------------------------------------------------------
    // In-memory model cache (not serialised — loaded from modelFilePath)
    // -----------------------------------------------------------------------

    /// Type-erased pointer to the cached MCDModelCache struct.
    /// Shared with background evaluation tasks; the pointee carries its own mutex.
    std::shared_ptr<void> _modelCacheSlot;
};

}  // namespace Ovito
