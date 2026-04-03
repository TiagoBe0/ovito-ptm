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
#include <ovito/core/utilities/concurrent/Launch.h>
#include <ovito/core/utilities/concurrent/ParallelFor.h>
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
    // --- 1. Extract particle data (on calling thread) -----------------------

    const SimulationCell* simCell = input.getObject<SimulationCell>();
    Particles* particlesObj       = input.expectMutableObject<Particles>();
    const Property* posProp       = particlesObj->expectProperty(Particles::PositionProperty);
    const size_t N                = posProp->size();

    // Capture modifier parameters by value so the background thread can use
    // them safely without touching 'this'.
    const InputMode   iMode       = inputMode();
    const OutputMode  oMode       = outputMode();
    const FloatType   cutoff      = cutoffRadius();
    const int         maxK        = numNeighbors();
    const QStringList propRefs    = inputProperties();
    const QString     modelFile   = modelPath();
    const QString     outPropName = outputPropertyName().isEmpty()
                                        ? QStringLiteral("ML_Structure")
                                        : outputPropertyName();

    // Validate early (on calling thread) to give fast feedback.
    if(iMode == InputMode::NeighborDistances) {
        if(cutoff <= 0)
            throw Exception(tr("MLStructureModifier: cutoff radius must be positive."));
        if(maxK <= 0)
            throw Exception(tr("MLStructureModifier: numNeighbors must be at least 1."));
    } else {
        if(propRefs.isEmpty())
            throw Exception(tr("MLStructureModifier: no input properties selected. "
                               "Please select at least one property column."));
    }
#ifdef OVITO_ML_HAS_LIBTORCH
    if(modelFile.isEmpty())
        throw Exception(tr("MLStructureModifier: no model path specified."));
#endif

    // Pre-resolve property columns on the calling thread (where the
    // PropertyContainer is accessible); capture the resolved data.
    struct Column { const Property* prop; int comp; size_t nComp; };
    std::vector<Column> columns;
    if(iMode == InputMode::ParticleProperties) {
        columns.reserve(propRefs.size());
        for(const QString& refStr : propRefs) {
            PropertyReference ref(refStr);
            QString errMsg;
            auto [prop, comp] = ref.findInContainerWithComponent(
                particlesObj, errMsg, /*requireComponent=*/false);
            if(!prop)
                throw Exception(tr("MLStructureModifier: %1").arg(errMsg));
            columns.push_back({prop, comp < 0 ? 0 : comp, prop->componentCount()});
        }
    }

    // --- 2. Launch background task ------------------------------------------
    // All heavy computation runs in a worker thread so the UI stays responsive.

    return asyncLaunch([
            state       = std::move(input),
            simCell, particlesObj, posProp,
            N, iMode, oMode,
            cutoff, maxK,
            columns     = std::move(columns),
            propRefs,           // kept for error messages
            modelFile,
            outPropName
            ]() mutable -> PipelineFlowState
    {
        TaskProgress progress(this_task::ui());

        // --- 3. Build per-atom feature matrix (parallel) --------------------

        std::vector<float> descriptorBuf;
        int numFeatures = 0;

        if(iMode == InputMode::NeighborDistances) {

            // ----- 3a. Sorted normalised neighbour distances ----------------

            numFeatures = maxK;
            descriptorBuf.resize(N * maxK);

            progress.setText(tr("ML: computing neighbor descriptors"));

            CutoffNeighborFinder neighborFinder(cutoff, posProp, simCell, nullptr);
            const float invCutoff = 1.0f / static_cast<float>(cutoff);

            // parallelForInnerOuter: the outer lambda runs once per thread
            // chunk; it allocates 'dists' once and reuses it for every atom in
            // that chunk, avoiding per-atom heap allocations.
            parallelForInnerOuter(N, /*chunkSize=*/256, progress,
                [&](auto&& iterate)
            {
                std::vector<FloatType> dists;
                dists.reserve(64);
                iterate([&](size_t i) {
                    dists.clear();
                    for(CutoffNeighborFinder::Query q(neighborFinder, i);
                            !q.atEnd(); q.next())
                        dists.push_back(q.distance());
                    std::sort(dists.begin(), dists.end());

                    float* row = descriptorBuf.data() + i * maxK;
                    const int k = static_cast<int>(
                        std::min(dists.size(), static_cast<size_t>(maxK)));
                    for(int j = 0; j < k; ++j)
                        row[j] = static_cast<float>(dists[j]) * invCutoff;
                    for(int j = k; j < maxK; ++j)
                        row[j] = 1.0f;
                });
            });

        } else {

            // ----- 3b. User-selected particle property columns --------------

            numFeatures = static_cast<int>(columns.size());
            descriptorBuf.resize(N * numFeatures);

            // Build read accessors once (before parallel section).
            // BufferAccessConvertedTo converts int/float/double → FloatType.
            std::vector<BufferAccessConvertedTo<FloatType>> accs;
            accs.reserve(columns.size());
            for(const auto& col : columns)
                accs.emplace_back(col.prop);

            progress.setText(tr("ML: building feature matrix"));

            // Each atom writes to its own disjoint row; no data races.
            parallelFor(N, /*chunkSize=*/512, progress, [&](size_t i) {
                for(int j = 0; j < numFeatures; ++j)
                    descriptorBuf[i * numFeatures + j] = static_cast<float>(
                        accs[j][i * columns[j].nComp + columns[j].comp]);
            });
        }

        // --- 4. Run ML inference (single-threaded) --------------------------

#ifdef OVITO_ML_HAS_LIBTORCH

        // Load TorchScript model.
        progress.setText(tr("ML: loading model"));
        torch::jit::script::Module model;
        try {
            model = torch::jit::load(modelFile.toStdString());
            model.eval();
        }
        catch(const c10::Error& e) {
            throw Exception(tr("MLStructureModifier: failed to load model '%1': %2")
                .arg(modelFile).arg(QString::fromStdString(e.what())));
        }
        catch(const std::exception& e) {
            throw Exception(tr("MLStructureModifier: failed to load model '%1': %2")
                .arg(modelFile).arg(QString::fromStdString(e.what())));
        }
        catch(...) {
            throw Exception(
                tr("MLStructureModifier: failed to load model '%1' (unknown exception).")
                .arg(modelFile));
        }

        // Wrap descriptor buffer in a LibTorch tensor (zero-copy via from_blob).
        auto inputTensor = torch::from_blob(
            descriptorBuf.data(),
            {static_cast<int64_t>(N), static_cast<int64_t>(numFeatures)},
            torch::kFloat32);

        // Forward pass.  NoGradGuard disables autograd (best practice since 1.9+).
        progress.setText(tr("ML: running model"));
        at::Tensor rawOutput;
        try {
            torch::NoGradGuard no_grad;
            rawOutput = model.forward({inputTensor}).toTensor();
        }
        catch(const c10::Error& e) {
            throw Exception(
                tr("MLStructureModifier: model forward() failed "
                   "(input [%1 atoms × %2 features]): %3")
                .arg(N).arg(numFeatures).arg(QString::fromStdString(e.what())));
        }
        catch(const std::exception& e) {
            throw Exception(
                tr("MLStructureModifier: model forward() failed "
                   "(input [%1 atoms × %2 features]): %3")
                .arg(N).arg(numFeatures).arg(QString::fromStdString(e.what())));
        }
        catch(...) {
            throw Exception(
                tr("MLStructureModifier: model forward() failed (unknown exception). "
                   "Input tensor: [%1 atoms × %2 features]. "
                   "Verify that the model's first layer accepts %2 features.")
                .arg(N).arg(numFeatures));
        }

        // Normalise output shape to 2-D: [N, K].
        // Models may return [N] for single-output regression; reshape to [N,1].
        if(rawOutput.dim() == 1)
            rawOutput = rawOutput.unsqueeze(1);

        if(rawOutput.dim() != 2 || static_cast<size_t>(rawOutput.size(0)) != N)
            throw Exception(
                tr("MLStructureModifier: unexpected output shape from model "
                   "(expected [%1, K], got tensor with %2 dimensions / %3 rows).")
                .arg(N).arg(rawOutput.dim()).arg(rawOutput.size(0)));

        const int64_t K = rawOutput.size(1);   // output values per atom

        if(oMode == OutputMode::Classification) {

            // ---- 4a. Classification: argmax → Int32 property ---------------

            if(K < 2)
                throw Exception(
                    tr("MLStructureModifier (classification): model output has only "
                       "%1 column(s); need at least 2 class logits. "
                       "For single-value output use Regression mode.").arg(K));

            Property* outProp = particlesObj->createProperty(
                DataBuffer::Initialized, outPropName, Property::Int32, 1);
            BufferWriteAccess<int32_t, access_mode::read_write> outAccess{outProp};

            at::Tensor preds = rawOutput.argmax(/*dim=*/1).to(torch::kInt32).contiguous();
            const int32_t* predData = preds.data_ptr<int32_t>();
            for(size_t i = 0; i < N; ++i)
                outAccess[i] = predData[i];

        } else {

            // ---- 4b. Regression: raw floats → FloatDefault property --------
            //
            // Output [N,1] → scalar property (1 component).
            // Output [N,K>1] → vector property (K components).
            // Always convert to float32 before writing (avoids kDouble mismatch).

            Property* outProp = particlesObj->createProperty(
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
        if(oMode == OutputMode::Classification) {
            particlesObj->createProperty(
                DataBuffer::Initialized, outPropName, Property::Int32, 1);
        } else {
            particlesObj->createProperty(
                DataBuffer::Initialized, outPropName, Property::FloatDefault, 1);
        }

#endif  // OVITO_ML_HAS_LIBTORCH

        return std::move(state);
    });
}

}  // namespace Ovito
