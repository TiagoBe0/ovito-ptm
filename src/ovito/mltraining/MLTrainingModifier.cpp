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

#include <ovito/core/dataset/pipeline/ModifierEvaluationRequest.h>
#include <ovito/core/utilities/units/UnitsManager.h>
#include <ovito/particles/objects/Particles.h>
#include "MLTrainingModifier.h"

namespace Ovito {

// ---------------------------------------------------------------------------
// Class registration
// ---------------------------------------------------------------------------

IMPLEMENT_CREATABLE_OVITO_CLASS(MLTrainingModifier);
OVITO_CLASSINFO(MLTrainingModifier, "DisplayName",      "NN Training Modifier");
OVITO_CLASSINFO(MLTrainingModifier, "ModifierCategory", "Structure identification");

// Descriptor / input
DEFINE_PROPERTY_FIELD(MLTrainingModifier, cutoffRadius);
DEFINE_PROPERTY_FIELD(MLTrainingModifier, numNeighbors);
DEFINE_PROPERTY_FIELD(MLTrainingModifier, labelProperty);

// Architecture
DEFINE_PROPERTY_FIELD(MLTrainingModifier, hiddenSize1);
DEFINE_PROPERTY_FIELD(MLTrainingModifier, hiddenSize2);

// Training hyper-parameters
DEFINE_PROPERTY_FIELD(MLTrainingModifier, numEpochs);
DEFINE_PROPERTY_FIELD(MLTrainingModifier, learningRate);
DEFINE_PROPERTY_FIELD(MLTrainingModifier, batchSize);

// Output
DEFINE_PROPERTY_FIELD(MLTrainingModifier, outputModelPath);

// UI labels
SET_PROPERTY_FIELD_LABEL(MLTrainingModifier, cutoffRadius,     "Cutoff radius (A)");
SET_PROPERTY_FIELD_LABEL(MLTrainingModifier, numNeighbors,     "Max neighbors in descriptor");
SET_PROPERTY_FIELD_LABEL(MLTrainingModifier, labelProperty,    "Label property");
SET_PROPERTY_FIELD_LABEL(MLTrainingModifier, hiddenSize1,      "Hidden layer 1 size");
SET_PROPERTY_FIELD_LABEL(MLTrainingModifier, hiddenSize2,      "Hidden layer 2 size");
SET_PROPERTY_FIELD_LABEL(MLTrainingModifier, numEpochs,        "Training epochs");
SET_PROPERTY_FIELD_LABEL(MLTrainingModifier, learningRate,     "Learning rate");
SET_PROPERTY_FIELD_LABEL(MLTrainingModifier, batchSize,        "Batch size (0 = full batch)");
SET_PROPERTY_FIELD_LABEL(MLTrainingModifier, outputModelPath,  "Output model path (.pt)");

// Numeric constraints
SET_PROPERTY_FIELD_UNITS_AND_MINIMUM(MLTrainingModifier, cutoffRadius, WorldParameterUnit, 0);
SET_PROPERTY_FIELD_UNITS_AND_RANGE(MLTrainingModifier, numNeighbors, IntegerParameterUnit, 1, 64);
SET_PROPERTY_FIELD_UNITS_AND_MINIMUM(MLTrainingModifier, hiddenSize1, IntegerParameterUnit, 1);
SET_PROPERTY_FIELD_UNITS_AND_MINIMUM(MLTrainingModifier, hiddenSize2, IntegerParameterUnit, 1);
SET_PROPERTY_FIELD_UNITS_AND_MINIMUM(MLTrainingModifier, numEpochs,   IntegerParameterUnit, 1);
SET_PROPERTY_FIELD_UNITS_AND_MINIMUM(MLTrainingModifier, batchSize,   IntegerParameterUnit, 0);

// ---------------------------------------------------------------------------
// OOMetaClass::isApplicableTo
// ---------------------------------------------------------------------------

bool MLTrainingModifier::OOMetaClass::isApplicableTo(const DataCollection& input) const
{
    // Applicable to any dataset that contains particles with positions.
    if(const Particles* p = input.getObject<Particles>())
        return p->getProperty(Particles::PositionProperty) != nullptr;
    return false;
}

// ---------------------------------------------------------------------------
// evaluateModifier  —  pure pass-through
// ---------------------------------------------------------------------------

Future<PipelineFlowState> MLTrainingModifier::evaluateModifier(
    const ModifierEvaluationRequest& /*request*/,
    PipelineFlowState&& input)
{
    // This modifier does not alter particle data at all.
    // Its only purpose is to hold hyper-parameters and to provide the GUI
    // "Collect & Train" button (implemented in MLTrainingModifierEditor).
    return Future<PipelineFlowState>::createImmediate(std::move(input));
}

}  // namespace Ovito
