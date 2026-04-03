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
 * Two input modes are available (see InputMode):
 *
 *  NeighborDistances (0) — original behaviour:
 *    For each atom, collects neighbour distances within cutoffRadius(), sorts them,
 *    pads to numNeighbors() slots, normalises by cutoffRadius().
 *    Produces a [N, numNeighbors] float tensor.
 *
 *  ParticleProperties (1) — column selection mode:
 *    Builds the feature vector from an arbitrary set of particle property columns
 *    previously computed by upstream modifiers (e.g. Voronoi volumes, PTM RMSD, …).
 *    The user selects which property/component pairs to include; each selected column
 *    contributes one float per particle. Produces a [N, numSelectedColumns] tensor.
 *    The model must have been trained with the same columns in the same order.
 *
 * In both modes the model must return class logits [N, C]; argmax gives the class index
 * that is written into a new "ML_Structure" integer particle property.
 */
class OVITO_MLPLUGIN_EXPORT MLStructureModifier : public Modifier
{
    /// Give this modifier class its own metaclass.
    class OVITO_MLPLUGIN_EXPORT OOMetaClass : public Modifier::OOMetaClass
    {
    public:
        using Modifier::OOMetaClass::OOMetaClass;

        /// Returns true when the modifier can be applied to the given data.
        virtual bool isApplicableTo(const DataCollection& input) const override;
    };

    OVITO_CLASS_META(MLStructureModifier, OOMetaClass)

public:

    /// How the per-atom input feature vector is constructed.
    enum class InputMode {
        NeighborDistances  = 0,  ///< Sorted normalised neighbour distances (original).
        ParticleProperties = 1   ///< User-selected particle property columns.
    };
    Q_ENUM(InputMode)

    /// How the model output is interpreted and written to a particle property.
    enum class OutputMode {
        Classification = 0,  ///< argmax over class logits → Int32 property (class index).
        Regression     = 1   ///< Raw model output → Float property (one or more components).
    };
    Q_ENUM(OutputMode)

    /// Returns a human-readable title shown in the pipeline editor.
    virtual QString objectTitle() const override { return tr("ML Structure Modifier"); }

    /// Main evaluation entry point called by the pipeline engine.
    virtual Future<PipelineFlowState> evaluateModifier(
        const ModifierEvaluationRequest& request,
        PipelineFlowState&& input) override;

private:

    /// Filesystem path to a TorchScript model file (.pt).
    DECLARE_MODIFIABLE_PROPERTY_FIELD(QString{}, modelPath, setModelPath);

    /// Selects how the per-atom feature vector is constructed.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(MLStructureModifier::InputMode{MLStructureModifier::InputMode::NeighborDistances}, inputMode, setInputMode);

    // --- NeighborDistances mode parameters ---

    /// Cutoff radius used to build the local environment descriptor (Angstrom).
    DECLARE_MODIFIABLE_PROPERTY_FIELD(FloatType{5.0}, cutoffRadius, setCutoffRadius);

    /// Maximum number of neighbors included in the descriptor vector.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(int{16}, numNeighbors, setNumNeighbors);

    // --- ParticleProperties mode parameters ---

    /// List of property columns to use as input features.
    /// Each entry is a PropertyReference name string (e.g. "Voronoi Volume",
    /// "Position.X", "Coordination").  Order matters — it must match the training data.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(QStringList{}, inputProperties, setInputProperties);

    // --- Output parameters ---

    /// Selects how the model output is interpreted.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(MLStructureModifier::OutputMode{MLStructureModifier::OutputMode::Classification}, outputMode, setOutputMode);

    /// Name of the particle property written by this modifier.
    /// Default "ML_Structure" for classification; the user should rename for regression.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(QString{"ML_Structure"}, outputPropertyName, setOutputPropertyName);
};

}  // namespace Ovito
