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
#include <ovito/core/utilities/units/UnitsManager.h>
#include <ovito/stdobj/simcell/SimulationCell.h>
#include <ovito/stdobj/properties/Property.h>
#include <ovito/particles/objects/Particles.h>
#include <ovito/particles/util/CutoffNeighborFinder.h>
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
DEFINE_PROPERTY_FIELD(MLStructureModifier, numNeighbors);

SET_PROPERTY_FIELD_LABEL(MLStructureModifier, modelPath,    "Model path (.pt)");
SET_PROPERTY_FIELD_LABEL(MLStructureModifier, cutoffRadius, "Cutoff radius (Å)");
SET_PROPERTY_FIELD_LABEL(MLStructureModifier, numNeighbors, "Max neighbors in descriptor");

SET_PROPERTY_FIELD_UNITS_AND_MINIMUM(MLStructureModifier, cutoffRadius, WorldParameterUnit, 0);
SET_PROPERTY_FIELD_UNITS_AND_RANGE(MLStructureModifier, numNeighbors, IntegerParameterUnit, 1, 64);

// ---------------------------------------------------------------------------
// OOMetaClass::isApplicableTo
// ---------------------------------------------------------------------------

bool MLStructureModifier::OOMetaClass::isApplicableTo(const DataCollection& input) const
{
    if(const Particles* particles = input.getObject<Particles>())
        return particles->getProperty(Particles::PositionProperty) != nullptr;
    return false;
}

// ---------------------------------------------------------------------------
// evaluateModifier
// ---------------------------------------------------------------------------

Future<PipelineFlowState> MLStructureModifier::evaluateModifier(
    const ModifierEvaluationRequest& request,
    PipelineFlowState&& input)
{
    // --- 1. Validate parameters -------------------------------------------

    const FloatType cutoff = cutoffRadius();
    const int       maxK   = numNeighbors();

    if(cutoff <= 0)
        throw Exception(tr("MLStructureModifier: cutoff radius must be positive."));
    if(maxK <= 0)
        throw Exception(tr("MLStructureModifier: numNeighbors must be at least 1."));

    // --- 2. Extract particle data -----------------------------------------

    const SimulationCell* simCell  = input.getObject<SimulationCell>();
    const Particles*  particlesObj = input.expectObject<Particles>();
    const Property* posProp  =
        particlesObj->expectProperty(Particles::PositionProperty);

    const size_t N = posProp->size();

    // --- 3. Build rotation-invariant descriptor [N, maxK] -----------------
    //
    // For each atom i:
    //   • Collect all neighbour distances within cutoff using CutoffNeighborFinder.
    //   • Sort ascending, pad with cutoff if fewer than maxK neighbours.
    //   • Normalise by cutoff → values in [0, 1].
    //
    // This matches the Python descriptor in scripts/train_structure_classifier.py.

    // Preallocate the descriptor as a flat float32 buffer [N * maxK].
    std::vector<float> descriptorBuf(N * maxK);

    {
        CutoffNeighborFinder neighborFinder(cutoff,
            posProp,          // positions property
            simCell,          // SimulationCellData (implicit conversion)
            nullptr);         // no selection filter

        const float invCutoff = 1.0f / static_cast<float>(cutoff);

        for(size_t i = 0; i < N; ++i) {
            // Collect neighbour distances for atom i.
            std::vector<FloatType> dists;
            dists.reserve(32);
            for(CutoffNeighborFinder::Query q(neighborFinder, i); !q.atEnd(); q.next())
                dists.push_back(q.distance());

            // Sort ascending.
            std::sort(dists.begin(), dists.end());

            // Fill descriptor row: first min(|dists|, maxK) values, rest = 1.0
            float* row = descriptorBuf.data() + i * maxK;
            const int k = static_cast<int>(std::min(dists.size(),
                                                     static_cast<size_t>(maxK)));
            for(int j = 0; j < k; ++j)
                row[j] = static_cast<float>(dists[j]) * invCutoff;
            for(int j = k; j < maxK; ++j)
                row[j] = 1.0f;   // pad with normalised cutoff distance
        }
    }

    // --- 4. Allocate output property "ML_Structure" -----------------------

    Particles* outputParticles = input.expectMutableObject<Particles>();
    Property* structProp = outputParticles->createProperty(
        DataBuffer::Initialized,
        QStringLiteral("ML_Structure"),
        Property::Int32,
        1);
    BufferWriteAccess<int32_t, access_mode::discard_write> outputData(structProp);

    // --- 5. Run ML inference ----------------------------------------------

#ifdef OVITO_ML_HAS_LIBTORCH

    if(modelPath().isEmpty())
        throw Exception(tr("MLStructureModifier: no model path specified."));

    // Load TorchScript model.
    // TODO: cache the loaded module as a member variable and reload only when
    //       modelPath() or a model version stamp changes.
    torch::jit::script::Module model;
    try {
        model = torch::jit::load(modelPath().toStdString());
        model.eval();
    }
    catch(const c10::Error& e) {
        throw Exception(tr("MLStructureModifier: failed to load model '%1': %2")
            .arg(modelPath())
            .arg(QString::fromStdString(e.what())));
    }

    // Wrap the descriptor buffer in a LibTorch tensor (zero-copy via from_blob).
    // descriptorBuf must outlive the tensor – it is alive for the rest of this scope.
    auto inputTensor = torch::from_blob(
        descriptorBuf.data(),
        {static_cast<long>(N), static_cast<long>(maxK)},
        torch::kFloat32);

    // Forward pass → logits [N, num_classes].
    at::Tensor logits;
    try {
        logits = model.forward({inputTensor}).toTensor();
    }
    catch(const c10::Error& e) {
        throw Exception(tr("MLStructureModifier: model forward() failed: %1")
            .arg(QString::fromStdString(e.what())));
    }

    if(logits.dim() != 2 || static_cast<size_t>(logits.size(0)) != N)
        throw Exception(tr("MLStructureModifier: expected model output shape [%1, C], "
                          "got [%2, ...].").arg(N).arg(logits.size(0)));

    // Argmax over class dimension → [N] int32 predictions.
    at::Tensor predictions = logits.argmax(/*dim=*/1).to(torch::kInt32).contiguous();
    const int* predData = predictions.data_ptr<int>();
    for(size_t i = 0; i < N; ++i)
        outputData[i] = predData[i];

#else

    // LibTorch not available — zero-fill (placeholder / integration test).
    // The descriptor was already computed; it's just not fed to the model.
    Q_UNUSED(outputData);

#endif  // OVITO_ML_HAS_LIBTORCH

    // --- 6. Return modified state -----------------------------------------

    return Future<PipelineFlowState>::createImmediate(std::move(input));
}

}  // namespace Ovito
