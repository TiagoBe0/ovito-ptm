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

// Cache slot shared between the calling thread (initialises/reads) and
// background evaluation tasks (reads on hit, writes on miss).
// The internal mutex makes concurrent access safe.
struct ModelCacheSlot {
    std::mutex                 mutex;
    std::string                loadedPath; // empty = no model cached yet
    torch::jit::script::Module model;
};
#endif

namespace Ovito {

// ---------------------------------------------------------------------------
// Class registration macros
// ---------------------------------------------------------------------------

IMPLEMENT_CREATABLE_OVITO_CLASS(MLStructureModifier);
OVITO_CLASSINFO(MLStructureModifier, "DisplayName",      "NN Modifier");
OVITO_CLASSINFO(MLStructureModifier, "ModifierCategory", "Structure identification");

DEFINE_PROPERTY_FIELD(MLStructureModifier, modelPath);
DEFINE_PROPERTY_FIELD(MLStructureModifier, inputMode);
DEFINE_PROPERTY_FIELD(MLStructureModifier, cutoffRadius);
DEFINE_PROPERTY_FIELD(MLStructureModifier, numNeighbors);
DEFINE_PROPERTY_FIELD(MLStructureModifier, inputProperties);
DEFINE_PROPERTY_FIELD(MLStructureModifier, outputMode);
DEFINE_PROPERTY_FIELD(MLStructureModifier, outputPropertyName);
DEFINE_PROPERTY_FIELD(MLStructureModifier, onlySelectedParticles);
DEFINE_PROPERTY_FIELD(MLStructureModifier, classLabels);
DEFINE_PROPERTY_FIELD(MLStructureModifier, outputProbabilities);

SET_PROPERTY_FIELD_LABEL(MLStructureModifier, modelPath,              "Model path (.pt)");
SET_PROPERTY_FIELD_LABEL(MLStructureModifier, inputMode,              "Input mode");
SET_PROPERTY_FIELD_LABEL(MLStructureModifier, cutoffRadius,           "Cutoff radius (Å)");
SET_PROPERTY_FIELD_LABEL(MLStructureModifier, numNeighbors,           "Max neighbors in descriptor");
SET_PROPERTY_FIELD_LABEL(MLStructureModifier, inputProperties,        "Input property columns");
SET_PROPERTY_FIELD_LABEL(MLStructureModifier, outputMode,             "Output mode");
SET_PROPERTY_FIELD_LABEL(MLStructureModifier, outputPropertyName,     "Output property name");
SET_PROPERTY_FIELD_LABEL(MLStructureModifier, onlySelectedParticles,  "Use only selected particles");
SET_PROPERTY_FIELD_LABEL(MLStructureModifier, classLabels,            "Class labels");
SET_PROPERTY_FIELD_LABEL(MLStructureModifier, outputProbabilities,    "Output class probabilities");

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
    const InputMode   iMode         = inputMode();
    const OutputMode  oMode         = outputMode();
    const FloatType   cutoff        = cutoffRadius();
    const int         maxK          = numNeighbors();
    const QStringList propRefs      = inputProperties();
    const QString     modelFile     = modelPath();
    const QString     outPropName   = outputPropertyName().isEmpty()
                                          ? QStringLiteral("ML_Structure")
                                          : outputPropertyName();
    const QStringList lbls          = classLabels();          // classification only
    const bool        outProbs      = outputProbabilities();  // classification only

    // Selection support: collect indices of selected particles on the calling
    // thread (where the PropertyContainer is accessible).
    const Property* selection = onlySelectedParticles()
        ? particlesObj->expectProperty(Particles::SelectionProperty)
        : nullptr;
    std::vector<size_t> selectedIndices;
    if(selection) {
        BufferReadAccess<SelectionIntType> selData(selection);
        selectedIndices.reserve(N);
        for(size_t i = 0; i < N; ++i)
            if(selData[i]) selectedIndices.push_back(i);
    }
    // M = number of particles that will actually be processed.
    const size_t M = selection ? selectedIndices.size() : N;

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

    // Lazily create the model cache slot (once per modifier instance) and
    // capture a shared reference for the background task.
    // The slot is type-erased here; the background task casts it back inside
    // the #ifdef OVITO_ML_HAS_LIBTORCH block where the full type is visible.
#ifdef OVITO_ML_HAS_LIBTORCH
    if(!_modelCacheSlot)
        _modelCacheSlot = std::make_shared<ModelCacheSlot>();
#endif
    std::shared_ptr<void> cacheSlotVoid = _modelCacheSlot;

    return asyncLaunch([
            state       = std::move(input),
            simCell, particlesObj, posProp,
            N, M, iMode, oMode,
            cutoff, maxK,
            columns     = std::move(columns),
            propRefs,           // kept for error messages
            modelFile,
            outPropName,
            selection,
            selectedIndices = std::move(selectedIndices),
            cacheSlotVoid,
            lbls, outProbs
            ]() mutable -> PipelineFlowState
    {
        TaskProgress progress(this_task::ui());

        // --- 3. Build per-atom feature matrix (parallel) --------------------

        std::vector<float> descriptorBuf;
        int numFeatures = 0;

        if(iMode == InputMode::NeighborDistances) {

            // ----- 3a. Sorted normalised neighbour distances ----------------
            // When onlySelectedParticles is active, M <= N and selectedIndices
            // maps row mi → global atom index i.

            numFeatures = maxK;
            descriptorBuf.resize(M * maxK);

            progress.setText(tr("ML: computing neighbor descriptors"));

            CutoffNeighborFinder neighborFinder(cutoff, posProp, simCell, nullptr);
            const float invCutoff = 1.0f / static_cast<float>(cutoff);

            // parallelForInnerOuter: the outer lambda runs once per thread
            // chunk; it allocates 'dists' once and reuses it for every atom in
            // that chunk, avoiding per-atom heap allocations.
            parallelForInnerOuter(M, /*chunkSize=*/256, progress,
                [&](auto&& iterate)
            {
                std::vector<FloatType> dists;
                dists.reserve(64);
                iterate([&](size_t mi) {
                    const size_t i = selection ? selectedIndices[mi] : mi;
                    dists.clear();
                    for(CutoffNeighborFinder::Query q(neighborFinder, i);
                            !q.atEnd(); q.next())
                        dists.push_back(q.distance());
                    std::sort(dists.begin(), dists.end());

                    float* row = descriptorBuf.data() + mi * maxK;
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
            descriptorBuf.resize(M * numFeatures);

            // Build read accessors once (before parallel section).
            // BufferAccessConvertedTo converts int/float/double → FloatType.
            std::vector<BufferAccessConvertedTo<FloatType>> accs;
            accs.reserve(columns.size());
            for(const auto& col : columns)
                accs.emplace_back(col.prop);

            progress.setText(tr("ML: building feature matrix"));

            // Each atom writes to its own disjoint row; no data races.
            parallelFor(M, /*chunkSize=*/512, progress, [&](size_t mi) {
                const size_t i = selection ? selectedIndices[mi] : mi;
                for(int j = 0; j < numFeatures; ++j)
                    descriptorBuf[mi * numFeatures + j] = static_cast<float>(
                        accs[j][i * columns[j].nComp + columns[j].comp]);
            });
        }

        // --- 4. Run ML inference (single-threaded) --------------------------

#ifdef OVITO_ML_HAS_LIBTORCH

        // Acquire model — reuse the cached module when the path has not
        // changed, otherwise load from disk and populate the cache for
        // subsequent evaluations (e.g. scrubbing through animation frames).
        torch::jit::script::Module model;
        {
            auto* slot = static_cast<ModelCacheSlot*>(cacheSlotVoid.get());
            const std::string pathStr = modelFile.toStdString();

            // --- cache lookup (fast path) ---
            bool needLoad = false;
            {
                std::lock_guard<std::mutex> lk(slot->mutex);
                if(slot->loadedPath == pathStr && !slot->loadedPath.empty()) {
                    model = slot->model;   // Module is ref-counted: cheap copy
                } else {
                    needLoad = true;
                }
            }

            // --- cache miss: load outside the lock so the mutex is not held
            //     during a potentially slow disk read ---
            if(needLoad) {
                progress.setText(tr("ML: loading model"));
                torch::jit::script::Module loaded;
                try {
                    loaded = torch::jit::load(pathStr);
                    loaded.eval();
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

                // Store in cache for future evaluations.
                {
                    std::lock_guard<std::mutex> lk(slot->mutex);
                    slot->model      = loaded;
                    slot->loadedPath = pathStr;
                }
                model = std::move(loaded);
            }
        }

        // Wrap descriptor buffer in a LibTorch tensor (zero-copy via from_blob).
        // Only M rows: selected particles (or all N when onlySelectedParticles is off).
        auto inputTensor = torch::from_blob(
            descriptorBuf.data(),
            {static_cast<int64_t>(M), static_cast<int64_t>(numFeatures)},
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
                .arg(M).arg(numFeatures).arg(QString::fromStdString(e.what())));
        }
        catch(const std::exception& e) {
            throw Exception(
                tr("MLStructureModifier: model forward() failed "
                   "(input [%1 atoms × %2 features]): %3")
                .arg(M).arg(numFeatures).arg(QString::fromStdString(e.what())));
        }
        catch(...) {
            throw Exception(
                tr("MLStructureModifier: model forward() failed (unknown exception). "
                   "Input tensor: [%1 atoms × %2 features]. "
                   "Verify that the model's first layer accepts %2 features.")
                .arg(M).arg(numFeatures));
        }

        // Normalise output shape to 2-D: [M, K].
        // Models may return [M] for single-output regression; reshape to [M,1].
        if(rawOutput.dim() == 1)
            rawOutput = rawOutput.unsqueeze(1);

        if(rawOutput.dim() != 2 || static_cast<size_t>(rawOutput.size(0)) != M)
            throw Exception(
                tr("MLStructureModifier: unexpected output shape from model "
                   "(expected [%1, K], got tensor with %2 dimensions / %3 rows).")
                .arg(M).arg(rawOutput.dim()).arg(rawOutput.size(0)));

        const int64_t K = rawOutput.size(1);   // output values per atom

        if(oMode == OutputMode::Classification) {

            // ---- 4a. Classification: argmax → typed Int32 property ---------
            //
            // The output property is a "typed" Int32 property: each integer
            // value maps to a named ParticleType (visible in OVITO's particle
            // type table with colors).  Class names come from `lbls`; any class
            // index beyond that list is auto-named "Class N".
            //
            // Output is initialized to 0 so unselected (or skipped) particles
            // get class 0 by default.

            if(K < 2)
                throw Exception(
                    tr("MLStructureModifier (classification): model output has only "
                       "%1 column(s); need at least 2 class logits. "
                       "For single-value output use Regression mode.").arg(K));

            // Helper: resolve a label for class index k.
            auto labelFor = [&](int64_t k) -> QString {
                return (k < (int64_t)lbls.size() && !lbls[k].trimmed().isEmpty())
                    ? lbls[k].trimmed()
                    : QStringLiteral("Class %1").arg(k);
            };

            // Create the typed Int32 property and register one ParticleType
            // per class so OVITO shows named, colored categories.
            Property* outProp = particlesObj->createProperty(
                DataBuffer::Initialized, outPropName, Property::Int32, 1);
            for(int64_t k = 0; k < K; ++k)
                outProp->addNumericType(Particles::OOClass(),
                                       static_cast<int>(k), labelFor(k));

            // Write argmax class indices.
            {
                BufferWriteAccess<int32_t, access_mode::read_write> outAccess{outProp};
                at::Tensor preds = rawOutput.argmax(/*dim=*/1).to(torch::kInt32).contiguous();
                const int32_t* predData = preds.data_ptr<int32_t>();
                for(size_t mi = 0; mi < M; ++mi) {
                    const size_t i = selection ? selectedIndices[mi] : mi;
                    outAccess[i] = predData[mi];
                }
            }

            // ---- 4a-2. Optional: per-class softmax probabilities -----------
            //
            // Creates a second Float property "<name> Probabilities" with K
            // components (one per class).  Component names match the class labels
            // so OVITO shows e.g. "ML_Structure Probabilities.FCC".
            if(outProbs) {
                QStringList compNames;
                compNames.reserve(static_cast<int>(K));
                for(int64_t k = 0; k < K; ++k)
                    compNames << labelFor(k);

                const QString probPropName =
                    outPropName + QStringLiteral(" Probabilities");
                Property* probProp = particlesObj->createProperty(
                    DataBuffer::Initialized, probPropName,
                    Property::FloatDefault, static_cast<size_t>(K), compNames);

                at::Tensor probs =
                    torch::softmax(rawOutput, /*dim=*/1).to(torch::kFloat32).contiguous();
                const float* probData = probs.data_ptr<float>();

                BufferWriteAccess<FloatType, access_mode::read_write> probAccess{probProp};
                for(size_t mi = 0; mi < M; ++mi) {
                    const size_t i = selection ? selectedIndices[mi] : mi;
                    for(int64_t k = 0; k < K; ++k)
                        probAccess[i * static_cast<size_t>(K) + k] =
                            static_cast<FloatType>(probData[mi * static_cast<size_t>(K) + k]);
                }
            }

        } else {

            // ---- 4b. Regression: raw floats → FloatDefault property --------
            //
            // Output [M,1] → scalar property (1 component).
            // Output [M,K>1] → vector property (K components).
            // Always convert to float32 before writing (avoids kDouble mismatch).
            // Non-selected particles stay at their initialized value (0.0).

            Property* outProp = particlesObj->createProperty(
                DataBuffer::Initialized, outPropName, Property::FloatDefault,
                static_cast<size_t>(K));
            BufferWriteAccess<FloatType, access_mode::read_write> outAccess{outProp};

            at::Tensor vals = rawOutput.to(torch::kFloat32).contiguous();
            const float* valData = vals.data_ptr<float>();
            for(size_t mi = 0; mi < M; ++mi) {
                const size_t i = selection ? selectedIndices[mi] : mi;
                for(int64_t k = 0; k < K; ++k)
                    outAccess[i * static_cast<size_t>(K) + k] =
                        static_cast<FloatType>(valData[mi * static_cast<size_t>(K) + k]);
            }
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
