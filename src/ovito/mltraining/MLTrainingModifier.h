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

#include <ovito/core/dataset/pipeline/Modifier.h>
#include <ovito/core/dataset/pipeline/PipelineFlowState.h>
#include <ovito/particles/Particles.h>
#include <ovito/stdobj/simcell/SimulationCell.h>

namespace Ovito {

/**
 * \brief A modifier that trains a neural network (MLP) on per-atom data collected
 *        from N animation frames loaded in OVITO (e.g. a series of .dump files).
 *
 * This modifier is intentionally a **pass-through**: it does not modify the
 * particle data that flows through the pipeline.  Its purpose is:
 *
 *  1. Store the training hyper-parameters as part of the OVITO scene file so
 *     they are preserved across sessions.
 *  2. Provide a GUI panel with a "Collect & Train" button that:
 *       a. Evaluates the upstream pipeline at every animation frame to gather
 *          per-atom neighbor-distance descriptors and integer class labels.
 *       b. Trains a fully-connected MLP using LibTorch.
 *       c. Exports the trained model as a TorchScript .pt file that can
 *          immediately be loaded by the NN Modifier (MLStructureModifier).
 *
 * Expected pipeline setup:
 *
 *   [File Source: N .dump frames]
 *       ↓
 *   [PTM Modifier  (or any modifier producing an integer "Structure Type")]
 *       ↓
 *   [MLTrainingModifier]   ← training is triggered here
 *       ↓
 *   [NN Modifier]          ← loads the saved .pt for inference
 *
 * The label property read by default is "Structure Type" (written by OVITO's
 * Polyhedral Template Matching modifier).  Any Int/Int32 particle property can
 * be used — change labelProperty() to match.
 */
class OVITO_MLTRAININGPLUGIN_EXPORT MLTrainingModifier : public Modifier
{
    /// Give this modifier class its own metaclass.
    class OVITO_MLTRAININGPLUGIN_EXPORT OOMetaClass : public Modifier::OOMetaClass
    {
    public:
        using Modifier::OOMetaClass::OOMetaClass;

        /// Returns true when the modifier can be applied to the given data.
        virtual bool isApplicableTo(const DataCollection& input) const override;
    };

    OVITO_CLASS_META(MLTrainingModifier, OOMetaClass)

public:

    /// Human-readable title shown in the pipeline panel.
    virtual QString objectTitle() const override { return tr("NN Training Modifier"); }

    /// Pass-through: returns the input unchanged.  All training happens via
    /// the GUI "Collect & Train" button, not during pipeline evaluation.
    virtual Future<PipelineFlowState> evaluateModifier(
        const ModifierEvaluationRequest& request,
        PipelineFlowState&& input) override;

    // -----------------------------------------------------------------------
    // Descriptor / input parameters
    // -----------------------------------------------------------------------

    /// Cutoff radius used to build sorted normalised neighbour-distance descriptors.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(FloatType{5.0}, cutoffRadius, setCutoffRadius);

    /// Maximum number of neighbours included in the descriptor (= input size of the MLP).
    DECLARE_MODIFIABLE_PROPERTY_FIELD(int{16}, numNeighbors, setNumNeighbors);

    /// Name of the upstream particle property used as class labels.
    /// Must be an integer property (e.g. "Structure Type" from PTM).
    DECLARE_MODIFIABLE_PROPERTY_FIELD(QString{"Structure Type"}, labelProperty, setLabelProperty);

    // -----------------------------------------------------------------------
    // MLP architecture parameters
    // -----------------------------------------------------------------------

    /// Number of neurons in the first hidden layer.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(int{64}, hiddenSize1, setHiddenSize1);

    /// Number of neurons in the second hidden layer.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(int{32}, hiddenSize2, setHiddenSize2);

    // -----------------------------------------------------------------------
    // Training hyper-parameters
    // -----------------------------------------------------------------------

    /// Number of training epochs.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(int{100}, numEpochs, setNumEpochs);

    /// Adam learning rate.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(FloatType{1e-3}, learningRate, setLearningRate);

    /// Mini-batch size used during training (0 = full-batch).
    DECLARE_MODIFIABLE_PROPERTY_FIELD(int{256}, batchSize, setBatchSize);

    // -----------------------------------------------------------------------
    // Output
    // -----------------------------------------------------------------------

    /// Filesystem path where the trained TorchScript model (.pt) is saved.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(QString{"trained_model.pt"}, outputModelPath, setOutputModelPath);
};

}  // namespace Ovito
