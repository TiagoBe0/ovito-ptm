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

// Fix 1: Include ModificationNode.h (not ModifierEvaluationRequest.h directly).
// The inline modificationNodeWeak() function in ModifierEvaluationRequest.h performs
// an OORef<ModificationNode> -> OOWeakRef<const PipelineNode> upcast, which requires
// ModificationNode to be fully declared. Including ModificationNode.h ensures this.
#include <ovito/core/dataset/pipeline/ModificationNode.h>
#include <ovito/core/utilities/units/UnitsManager.h>
#include <ovito/stdobj/simcell/SimulationCell.h>
#include <ovito/stdobj/properties/Property.h>
#include <ovito/particles/objects/Particles.h>
#include <ovito/particles/util/CutoffNeighborFinder.h>
#include "MLPerAtomModifier.h"

#ifdef OVITO_ML_HAS_LIBTORCH
#  include <torch/script.h>
#endif

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(MLPerAtomModifier);
OVITO_CLASSINFO(MLPerAtomModifier, "DisplayName",      "ML Per-Atom Modifier");
OVITO_CLASSINFO(MLPerAtomModifier, "ModifierCategory", "Analysis");

DEFINE_PROPERTY_FIELD(MLPerAtomModifier, modelPath);
DEFINE_PROPERTY_FIELD(MLPerAtomModifier, cutoffRadius);
DEFINE_PROPERTY_FIELD(MLPerAtomModifier, numNeighbors);
DEFINE_PROPERTY_FIELD(MLPerAtomModifier, outputPropertyName);
DEFINE_PROPERTY_FIELD(MLPerAtomModifier, taskType);

SET_PROPERTY_FIELD_LABEL(MLPerAtomModifier, modelPath,         "Model path (.pt)");
SET_PROPERTY_FIELD_LABEL(MLPerAtomModifier, cutoffRadius,      "Cutoff radius (\u00c5)");
SET_PROPERTY_FIELD_LABEL(MLPerAtomModifier, numNeighbors,      "Max neighbors in descriptor");
SET_PROPERTY_FIELD_LABEL(MLPerAtomModifier, outputPropertyName,"Output property name");
SET_PROPERTY_FIELD_LABEL(MLPerAtomModifier, taskType,          "Task type (0=class, 1=reg)");

SET_PROPERTY_FIELD_UNITS_AND_MINIMUM(MLPerAtomModifier, cutoffRadius, WorldParameterUnit, 0);
SET_PROPERTY_FIELD_RANGE(MLPerAtomModifier, numNeighbors, 1, 64);
SET_PROPERTY_FIELD_RANGE(MLPerAtomModifier, taskType, 0, 1);

bool MLPerAtomModifier::OOMetaClass::isApplicableTo(const DataCollection& input) const
{
    if(const Particles* particles = input.getObject<Particles>())
        return particles->getProperty(Particles::PositionProperty) != nullptr;
    return false;
}

Future<PipelineFlowState> MLPerAtomModifier::evaluateModifier(
    const ModifierEvaluationRequest& request,
    PipelineFlowState&& input)
{
    Q_UNUSED(request);

    const FloatType cutoff = cutoffRadius();
    const int maxK = numNeighbors();
    const int mode = taskType();
    const QString outName = outputPropertyName().trimmed();

    if(cutoff <= 0)
        throw Exception(tr("MLPerAtomModifier: cutoff radius must be positive."));
    if(maxK <= 0)
        throw Exception(tr("MLPerAtomModifier: numNeighbors must be at least 1."));
    if(mode < 0 || mode > 1)
        throw Exception(tr("MLPerAtomModifier: taskType must be 0 (classification) or 1 (regression)."));
    if(outName.isEmpty())
        throw Exception(tr("MLPerAtomModifier: outputPropertyName must not be empty."));

    const SimulationCell* simCell = input.getObject<SimulationCell>();
    const Particles* particlesObj = input.expectObject<Particles>();
    const Property* posProp = particlesObj->expectProperty(Particles::PositionProperty);
    const size_t N = posProp->size();

    std::vector<float> descriptorBuf(N * maxK);

    {
        CutoffNeighborFinder neighborFinder(cutoff, posProp, simCell, nullptr);
        const float invCutoff = 1.0f / static_cast<float>(cutoff);

        for(size_t i = 0; i < N; ++i) {
            std::vector<FloatType> dists;
            dists.reserve(32);
            for(CutoffNeighborFinder::Query q(neighborFinder, i); !q.atEnd(); q.next())
                dists.push_back(q.distance());

            std::sort(dists.begin(), dists.end());

            float* row = descriptorBuf.data() + i * maxK;
            const int k = static_cast<int>(std::min(dists.size(), static_cast<size_t>(maxK)));
            for(int j = 0; j < k; ++j)
                row[j] = static_cast<float>(dists[j]) * invCutoff;
            for(int j = k; j < maxK; ++j)
                row[j] = 1.0f;
        }
    }

    Particles* outputParticles = input.expectMutableObject<Particles>();

    Property* outputProp = outputParticles->createProperty(
        DataBuffer::Initialized,
        outName,
        mode == 0 ? Property::Int32 : Property::Float32,
        1);

#ifdef OVITO_ML_HAS_LIBTORCH

    if(modelPath().isEmpty())
        throw Exception(tr("MLPerAtomModifier: no model path specified."));

    torch::jit::script::Module model;
    try {
        model = torch::jit::load(modelPath().toStdString());
        model.eval();
    }
    catch(const c10::Error& e) {
        throw Exception(tr("MLPerAtomModifier: failed to load model '%1': %2")
            .arg(modelPath())
            .arg(QString::fromStdString(e.what())));
    }

    auto inputTensor = torch::from_blob(
        descriptorBuf.data(),
        {static_cast<long>(N), static_cast<long>(maxK)},
        torch::kFloat32);

    at::Tensor outputTensor;
    try {
        outputTensor = model.forward({inputTensor}).toTensor().contiguous();
    }
    catch(const c10::Error& e) {
        throw Exception(tr("MLPerAtomModifier: model forward() failed: %1")
            .arg(QString::fromStdString(e.what())));
    }

    if(mode == 0) {
        BufferWriteAccess<int32_t, access_mode::read_write> labels{outputProp};

        at::Tensor pred;
        if(outputTensor.dim() == 2) {
            pred = outputTensor.argmax(/*dim=*/1).to(torch::kInt32).contiguous();
        }
        else if(outputTensor.dim() == 1) {
            pred = outputTensor.to(torch::kInt32).contiguous();
        }
        else {
            throw Exception(tr("MLPerAtomModifier: classification expects output shape [N,C] or [N]."));
        }

        if(static_cast<size_t>(pred.size(0)) != N)
            throw Exception(tr("MLPerAtomModifier: output first dimension (%1) does not match atom count (%2).")
                .arg(pred.size(0)).arg(N));

        // Fix 2: Use static_cast instead of template data_ptr<int>() — GCC cannot
        // parse LibTorch template member calls of the form tensor.data_ptr<T>().
        const int* predData = static_cast<const int*>(pred.data_ptr());
        for(size_t i = 0; i < N; ++i)
            labels[i] = static_cast<int32_t>(predData[i]);
    }
    else {
        BufferWriteAccess<float, access_mode::read_write> values{outputProp};

        at::Tensor scalar;
        if(outputTensor.dim() == 2 && outputTensor.size(1) == 1)
            scalar = outputTensor.squeeze(1).to(torch::kFloat32).contiguous();
        else if(outputTensor.dim() == 1)
            scalar = outputTensor.to(torch::kFloat32).contiguous();
        else
            throw Exception(tr("MLPerAtomModifier: regression expects output shape [N] or [N,1]."));

        if(static_cast<size_t>(scalar.size(0)) != N)
            throw Exception(tr("MLPerAtomModifier: output first dimension (%1) does not match atom count (%2).")
                .arg(scalar.size(0)).arg(N));

        // Fix 3: Use static_cast instead of template data_ptr<float>() — same
        // GCC parsing issue as above.
        const float* outData = static_cast<const float*>(scalar.data_ptr());
        for(size_t i = 0; i < N; ++i)
            values[i] = outData[i];
    }

#else

    // LibTorch disabled: property stays initialized to zeros.
    Q_UNUSED(outputProp);

#endif

    return Future<PipelineFlowState>::createImmediate(std::move(input));
}

} // namespace Ovito
