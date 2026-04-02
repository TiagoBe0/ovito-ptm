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
#include <ovito/stdobj/properties/Property.h>
#include <ovito/particles/objects/Particles.h>

namespace Ovito {

/**
 * \brief Modifier that runs inference with a pre-trained ML model (LibTorch .pt)
 *        and outputs per-particle structure predictions as a new particle property.
 *
 * Pipeline:
 *   1. Reads particle positions and simulation cell from PipelineFlowState.
 *   2. For each atom, collects neighbor distances within cutoffRadius() using
 *      CutoffNeighborFinder, sorts them, pads to numNeighbors() slots, and
 *      normalises by cutoffRadius().  Produces a [N, numNeighbors] float tensor.
 *   3. Forwards the tensor through a TorchScript model loaded from modelPath().
 *      The model must return class logits [N, C]; argmax gives the class index.
 *   4. Writes integer class indices into a new "ML_Structure" particle property.
 *
 * Descriptor: sorted normalised neighbor distances.
 *   - Rotation & translation invariant.
 *   - Must match the descriptor used during Python training
 *     (see scripts/train_structure_classifier.py).
 *
 * When OVITO_ML_HAS_LIBTORCH is not defined, step 3 is skipped and the output
 * property is filled with zeros (placeholder mode, useful for integration testing
 * without the LibTorch dependency).
 */
class OVITO_MLPLUGIN_EXPORT MLStructureModifier : public Modifier
{
    /// Give this modifier class its own metaclass.
    class OOMetaClass : public Modifier::OOMetaClass
    {
    public:
        using Modifier::OOMetaClass::OOMetaClass;

        /// Returns true when the modifier can be applied to the given data.
        virtual bool isApplicableTo(const DataCollection& input) const override;
    };

    OVITO_CLASS_META(MLStructureModifier, OOMetaClass)

public:

    /// Returns a human-readable title shown in the pipeline editor.
    virtual QString objectTitle() const override { return tr("ML Structure Modifier"); }

    /// Main evaluation entry point called by the pipeline engine.
    virtual Future<PipelineFlowState> evaluateModifier(
        const ModifierEvaluationRequest& request,
        PipelineFlowState&& input) override;

private:

    /// Filesystem path to a TorchScript model file (.pt).
    /// Set via the property field system so OVITO can serialize/deserialize it.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(QString, modelPath, setModelPath);

    /// Cutoff radius used to build the local environment descriptor (Angstrom).
    /// Must match the value used when training the model (default: 5.0 Å).
    DECLARE_MODIFIABLE_PROPERTY_FIELD(FloatType, cutoffRadius, setCutoffRadius);

    /// Maximum number of neighbors included in the descriptor vector.
    /// Distances are sorted ascending; shorter vectors are padded with cutoffRadius.
    /// Must match MAX_NEIGH in the Python training script (default: 16).
    DECLARE_MODIFIABLE_PROPERTY_FIELD(int, numNeighbors, setNumNeighbors);
};

}  // namespace Ovito
