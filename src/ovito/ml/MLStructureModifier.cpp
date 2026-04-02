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

#include <ovito/core/dataset/DataSet.h>
#include <ovito/core/dataset/pipeline/ModificationNode.h>
#include <ovito/core/dataset/pipeline/ModifierEvaluationRequest.h>
#include <ovito/stdobj/simcell/SimulationCell.h>
#include <ovito/stdobj/properties/Property.h>
#include <ovito/particles/objects/Particles.h>
#include "MLStructureModifier.h"

// Include LibTorch headers only when the library is available.
#ifdef OVITO_ML_HAS_LIBTORCH
#  include <torch/script.h>
#endif

namespace Ovito {

// ---------------------------------------------------------------------------
// Class registration macros
// ---------------------------------------------------------------------------

IMPLEMENT_CREATABLE_OVITO_CLASS(MLStructureModifier);
OVITO_CLASSINFO(MLStructureModifier, "DisplayName",      "ML Structure Modifier");
OVITO_CLASSINFO(MLStructureModifier, "ModifierCategory", "Analysis");

DEFINE_PROPERTY_FIELD(MLStructureModifier, modelPath);
DEFINE_PROPERTY_FIELD(MLStructureModifier, cutoffRadius);

SET_PROPERTY_FIELD_LABEL(MLStructureModifier, modelPath,    "Model path (.pt)");
SET_PROPERTY_FIELD_LABEL(MLStructureModifier, cutoffRadius, "Cutoff radius (Å)");
SET_PROPERTY_FIELD_UNITS_AND_MINIMUM(MLStructureModifier, cutoffRadius, WorldParameterUnit, 0);

// ---------------------------------------------------------------------------
// OOMetaClass::isApplicableTo
// ---------------------------------------------------------------------------

bool MLStructureModifier::OOMetaClass::isApplicableTo(const DataCollection& input) const
{
    // The modifier requires at least one Particles object with a Position property.
    if(const Particles* particles = input.getObject<Particles>())
        return particles->getProperty(ParticlesObject::PositionProperty) != nullptr;
    return false;
}

// ---------------------------------------------------------------------------
// evaluateModifier
// ---------------------------------------------------------------------------

Future<PipelineFlowState> MLStructureModifier::evaluateModifier(
    const ModifierEvaluationRequest& request,
    PipelineFlowState&& input)
{
    // --- 1. Extract particle data -----------------------------------------

    const Particles* particlesObj = input.expectObject<Particles>();
    const PropertyObject* posProperty =
        particlesObj->expectProperty(ParticlesObject::PositionProperty);

    const size_t particleCount = posProperty->size();

    // --- 2. Allocate output property "ML_Structure" (int32, 1 component) ---

    // getMutableObject() makes a copy if the object is shared (copy-on-write).
    Particles* outputParticles = input.expectMutableObject<Particles>();

    // Create (or replace) the output property.
    PropertyObject* structProp = outputParticles->createProperty(
        QStringLiteral("ML_Structure"),
        PropertyObject::Int,
        1,
        /*initializeMemory=*/true);

    int* outputData = structProp->dataInt();

    // --- 3. Run ML inference -----------------------------------------------

#ifdef OVITO_ML_HAS_LIBTORCH

    if(modelPath().isEmpty())
        throwException(tr("MLStructureModifier: no model path specified."));

    // Load (or re-use cached) TorchScript model.
    // NOTE: For production use, cache the loaded module as a member variable
    // and only reload when modelPath changes.
    torch::jit::script::Module model;
    try {
        model = torch::jit::load(modelPath().toStdString());
        model.eval();
    }
    catch(const c10::Error& e) {
        throwException(tr("MLStructureModifier: failed to load model '%1': %2")
            .arg(modelPath())
            .arg(QString::fromStdString(e.what())));
    }

    // Build a [N, 3] float tensor from particle positions.
    const Point3* positions =
        reinterpret_cast<const Point3*>(posProperty->constDataPoint3());

    auto inputTensor = torch::zeros({static_cast<long>(particleCount), 3},
                                     torch::kFloat32);
    float* tensorData = inputTensor.data_ptr<float>();
    for(size_t i = 0; i < particleCount; ++i) {
        tensorData[i * 3 + 0] = static_cast<float>(positions[i].x());
        tensorData[i * 3 + 1] = static_cast<float>(positions[i].y());
        tensorData[i * 3 + 2] = static_cast<float>(positions[i].z());
    }

    // Forward pass.
    std::vector<torch::jit::IValue> inputs = { inputTensor };
    at::Tensor outputTensor;
    try {
        outputTensor = model.forward(inputs).toTensor();
    }
    catch(const c10::Error& e) {
        throwException(tr("MLStructureModifier: model forward() failed: %1")
            .arg(QString::fromStdString(e.what())));
    }

    // Expect output shape [N] or [N,1] of integer class indices.
    outputTensor = outputTensor.flatten().to(torch::kInt32).contiguous();
    if(static_cast<size_t>(outputTensor.size(0)) != particleCount)
        throwException(tr("MLStructureModifier: model output size (%1) "
                          "does not match particle count (%2).")
            .arg(outputTensor.size(0))
            .arg(particleCount));

    const int* predData = outputTensor.data_ptr<int>();
    std::copy(predData, predData + particleCount, outputData);

#else

    // LibTorch not available — fill with zeros (placeholder / integration test).
    Q_UNUSED(outputData);   // already zero-initialised above

#endif  // OVITO_ML_HAS_LIBTORCH

    // --- 4. Return the modified pipeline state ----------------------------

    return Future<PipelineFlowState>::createImmediate(std::move(input));
}

}  // namespace Ovito
