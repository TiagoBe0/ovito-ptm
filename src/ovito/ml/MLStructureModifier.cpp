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
#include <ovito/core/dataset/data/BufferAccess.h>
#include <ovito/core/utilities/units/UnitsManager.h>
#include <ovito/stdobj/simcell/SimulationCell.h>
#include <ovito/stdobj/properties/Property.h>
#include <ovito/stdobj/properties/PropertyReference.h>
#include <ovito/particles/objects/Particles.h>
#include <ovito/particles/util/CutoffNeighborFinder.h>
#include "MLStructureModifier.h"

// Include LibTorch headers only when the library is available.
#ifdef OVITO_ML_HAS_LIBTORCH
#  include <torch/script.h>
#  include <torch/csrc/autograd/grad_mode.h>
#endif

namespace Ovito {

// ---------------------------------------------------------------------------
// Class registration macros
// ---------------------------------------------------------------------------

IMPLEMENT_CREATABLE_OVITO_CLASS(MLStructureModifier);
OVITO_CLASSINFO(MLStructureModifier, "DisplayName",      "ML Structure Modifier");
OVITO_CLASSINFO(MLStructureModifier, "ModifierCategory", "Structure identification");

DEFINE_PROPERTY_FIELD(MLStructureModifier, modelPath);
DEFINE_PROPERTY_FIELD(MLStructureModifier, inputMode);
DEFINE_PROPERTY_FIELD(MLStructureModifier, cutoffRadius);
DEFINE_PROPERTY_FIELD(MLStructureModifier, numNeighbors);
DEFINE_PROPERTY_FIELD(MLStructureModifier, inputProperties);
DEFINE_PROPERTY_FIELD(MLStructureModifier, outputMode);
DEFINE_PROPERTY_FIELD(MLStructureModifier, outputPropertyName);

SET_PROPERTY_FIELD_LABEL(MLStructureModifier, modelPath,           "Model path (.pt)");
SET_PROPERTY_FIELD_LABEL(MLStructureModifier, inputMode,           "Input mode");
SET_PROPERTY_FIELD_LABEL(MLStructureModifier, cutoffRadius,        "Cutoff radius (Å)");
SET_PROPERTY_FIELD_LABEL(MLStructureModifier, numNeighbors,        "Max neighbors in descriptor");
SET_PROPERTY_FIELD_LABEL(MLStructureModifier, inputProperties,     "Input property columns");
SET_PROPERTY_FIELD_LABEL(MLStructureModifier, outputMode,          "Output mode");
SET_PROPERTY_FIELD_LABEL(MLStructureModifier, outputPropertyName,  "Output property name");

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
    // --- 1. Extract particle data -------------------------------------------

    const SimulationCell* simCell   = input.getObject<SimulationCell>();
    const Particles* particlesObj   = input.expectObject<Particles>();
    const Property*  posProp        =
        particlesObj->expectProperty(Particles::PositionProperty);

    const size_t N = posProp->size();

    // --- 2. Build the per-atom input feature matrix -------------------------

    std::vector<float> descriptorBuf;
    int numFeatures = 0;

    if(inputMode() == InputMode::NeighborDistances) {

        // ----- 2a. Sorted normalised neighbour distances --------------------

        const FloatType cutoff = cutoffRadius();
        const int       maxK   = numNeighbors();

        if(cutoff <= 0)
            throw Exception(tr("MLStructureModifier: cutoff radius must be positive."));
        if(maxK <= 0)
            throw Exception(tr("MLStructureModifier: numNeighbors must be at least 1."));

        numFeatures = maxK;
        descriptorBuf.resize(N * maxK);

        CutoffNeighborFinder neighborFinder(cutoff, posProp, simCell, nullptr);
        const float invCutoff = 1.0f / static_cast<float>(cutoff);

        for(size_t i = 0; i < N; ++i) {
            std::vector<FloatType> dists;
            dists.reserve(32);
            for(CutoffNeighborFinder::Query q(neighborFinder, i); !q.atEnd(); q.next())
                dists.push_back(q.distance());

            std::sort(dists.begin(), dists.end());

            float* row = descriptorBuf.data() + i * maxK;
            const int k = static_cast<int>(std::min(dists.size(),
                                                     static_cast<size_t>(maxK)));
            for(int j = 0; j < k; ++j)
                row[j] = static_cast<float>(dists[j]) * invCutoff;
            for(int j = k; j < maxK; ++j)
                row[j] = 1.0f;
        }

    } else {

        // ----- 2b. User-selected particle property columns ------------------

        const QStringList& propRefs = inputProperties();
        if(propRefs.isEmpty())
            throw Exception(tr("MLStructureModifier: no input properties selected. "
                               "Please select at least one property column."));

        // Resolve each "Name.Component" string to (Property*, componentIndex).
        struct Column {
            const Property* prop;
            int             comp;   // resolved component index (>= 0)
        };
        std::vector<Column> columns;
        columns.reserve(propRefs.size());

        for(const QString& refStr : propRefs) {
            PropertyReference ref(refStr);
            QString errMsg;
            auto [prop, comp] = ref.findInContainerWithComponent(
                particlesObj, errMsg, /*requireComponent=*/false);
            if(!prop)
                throw Exception(tr("MLStructureModifier: %1").arg(errMsg));
            // If no component was specified (scalar property), default to 0.
            columns.push_back({prop, comp < 0 ? 0 : comp});
        }

        numFeatures = static_cast<int>(columns.size());
        descriptorBuf.resize(N * numFeatures);

        // Fill descriptor buffer: one row per atom, one column per selected feature.
        // BufferAccessConvertedTo<FloatType> handles int/float/double properties
        // transparently, converting to FloatType on the fly.
        for(int j = 0; j < numFeatures; ++j) {
            const Property* prop  = columns[j].prop;
            const int       comp  = columns[j].comp;
            const size_t    nComp = prop->componentCount();

            BufferAccessConvertedTo<FloatType> acc(prop);
            for(size_t i = 0; i < N; ++i)
                descriptorBuf[i * numFeatures + j] =
                    static_cast<float>(acc[i * nComp + comp]);
        }
    }

    // --- 3. Determine output property name ----------------------------------

    const QString outPropName = outputPropertyName().isEmpty()
        ? QStringLiteral("ML_Structure") : outputPropertyName();

    // --- 4. Run ML inference ------------------------------------------------

#ifdef OVITO_ML_HAS_LIBTORCH

    if(modelPath().isEmpty())
        throw Exception(tr("MLStructureModifier: no model path specified."));

    // Load TorchScript model.
    torch::jit::script::Module model;
    try {
        model = torch::jit::load(modelPath().toStdString());
        model.eval();
    }
    catch(const c10::Error& e) {
        throw Exception(tr("MLStructureModifier: failed to load model '%1': %2")
            .arg(modelPath()).arg(QString::fromStdString(e.what())));
    }
    catch(const std::exception& e) {
        throw Exception(tr("MLStructureModifier: failed to load model '%1': %2")
            .arg(modelPath()).arg(QString::fromStdString(e.what())));
    }
    catch(...) {
        throw Exception(tr("MLStructureModifier: failed to load model '%1' (unknown exception).")
            .arg(modelPath()));
    }

    // Wrap descriptor in a LibTorch tensor (zero-copy via from_blob).
    auto inputTensor = torch::from_blob(
        descriptorBuf.data(),
        {static_cast<int64_t>(N), static_cast<int64_t>(numFeatures)},
        torch::kFloat32);

    // Forward pass.  NoGradGuard disables autograd (best practice since 1.9+).
    at::Tensor rawOutput;
    try {
        torch::NoGradGuard no_grad;
        rawOutput = model.forward({inputTensor}).toTensor();
    }
    catch(const c10::Error& e) {
        throw Exception(tr("MLStructureModifier: model forward() failed "
                           "(input [%1 atoms × %2 features]): %3")
            .arg(N).arg(numFeatures).arg(QString::fromStdString(e.what())));
    }
    catch(const std::exception& e) {
        throw Exception(tr("MLStructureModifier: model forward() failed "
                           "(input [%1 atoms × %2 features]): %3")
            .arg(N).arg(numFeatures).arg(QString::fromStdString(e.what())));
    }
    catch(...) {
        throw Exception(tr("MLStructureModifier: model forward() failed (unknown exception). "
                           "Input tensor: [%1 atoms × %2 features]. "
                           "Verify that the model's first layer accepts %2 features.")
            .arg(N).arg(numFeatures));
    }

    // Normalise output shape to 2-D: [N, K].
    // Models may return [N] for single-output regression — reshape to [N, 1].
    if(rawOutput.dim() == 1)
        rawOutput = rawOutput.unsqueeze(1);

    if(rawOutput.dim() != 2 || static_cast<size_t>(rawOutput.size(0)) != N)
        throw Exception(tr("MLStructureModifier: unexpected output shape from model "
                           "(expected [%1, K], got tensor with %2 dimensions / %3 rows).")
            .arg(N).arg(rawOutput.dim()).arg(rawOutput.size(0)));

    const int64_t K = rawOutput.size(1);   // number of output values per atom

    Particles* outputParticles = input.expectMutableObject<Particles>();

    if(outputMode() == OutputMode::Classification) {

        // ---- 4a. Classification: argmax → Int32 property ------------------

        if(K < 2)
            throw Exception(tr("MLStructureModifier (classification): model output has only "
                               "%1 column(s); need at least 2 class logits. "
                               "For single-value output use Regression mode.").arg(K));

        Property* outProp = outputParticles->createProperty(
            DataBuffer::Initialized, outPropName, Property::Int32, 1);
        BufferWriteAccess<int32_t, access_mode::read_write> outAccess{outProp};

        at::Tensor predictions = rawOutput.argmax(/*dim=*/1).to(torch::kInt32).contiguous();
        const int32_t* predData = predictions.data_ptr<int32_t>();
        for(size_t i = 0; i < N; ++i)
            outAccess[i] = predData[i];

    } else {

        // ---- 4b. Regression: raw float values → Float property ------------
        //
        // Output shape [N, 1]  → scalar Float property  (1 component).
        // Output shape [N, K>1] → vector Float property (K components).
        // Convert tensor to float32 then cast to OVITO's FloatType (float or double).

        Property* outProp = outputParticles->createProperty(
            DataBuffer::Initialized, outPropName, Property::FloatDefault,
            static_cast<size_t>(K));
        BufferWriteAccess<FloatType, access_mode::read_write> outAccess{outProp};

        at::Tensor vals = rawOutput.to(torch::kFloat32).contiguous();
        const float* valData = vals.data_ptr<float>();
        const size_t total = N * static_cast<size_t>(K);
        for(size_t idx = 0; idx < total; ++idx)
            outAccess[idx] = static_cast<FloatType>(valData[idx]);
    }

#else

    // LibTorch not available — create zero-filled placeholder property.
    Particles* outputParticles = input.expectMutableObject<Particles>();
    if(outputMode() == OutputMode::Classification) {
        outputParticles->createProperty(
            DataBuffer::Initialized, outPropName, Property::Int32, 1);
    } else {
        outputParticles->createProperty(
            DataBuffer::Initialized, outPropName, Property::FloatDefault, 1);
    }

#endif  // OVITO_ML_HAS_LIBTORCH

    // --- 5. Return modified state -------------------------------------------

    return Future<PipelineFlowState>::createImmediate(std::move(input));
}

}  // namespace Ovito
