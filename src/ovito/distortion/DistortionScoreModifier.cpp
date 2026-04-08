////////////////////////////////////////////////////////////////////////////////////////
//
//  Copyright 2025 OVITO GmbH, Germany
//
//  This file is part of OVITO (Open Visualization Tool).
//
//  OVITO is free software; you can redistribute it and/or modify it either under the
//  terms of the GNU General Public License version 3 as published by the Free Software
//  Foundation (the "GPL") or, at your option, under the terms of the MIT License.
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
#include <ovito/particles/objects/Particles.h>
#include <ovito/particles/util/CutoffNeighborFinder.h>
#include "DistortionScoreModifier.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <string>
#include <vector>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace Ovito {

// ===========================================================================
// Model cache — holds the trained MCD parameters in memory.
// ===========================================================================

struct MCDModelCache
{
    std::mutex  mutex;
    std::string loadedPath; ///< Path currently in cache (empty = not loaded).
    int         numFeatures = 0;
    std::vector<double> mean;   ///< [numFeatures]
    std::vector<double> invCov; ///< [numFeatures × numFeatures], row-major
};

// ===========================================================================
// Inference helpers (used only in this translation unit)
// ===========================================================================

/// Compute squared Mahalanobis distance: d² = (x − μ)ᵀ Σ⁻¹ (x − μ).
/// x is float (descriptor), mu/invCov are double (model parameters).
static double mahalanobisSq(const float* x, const double* mu,
                             const double* invCov, int D)
{
    double d2 = 0.0;
    for(int a = 0; a < D; ++a) {
        const double da = static_cast<double>(x[a]) - mu[static_cast<size_t>(a)];
        for(int b = 0; b < D; ++b)
            d2 += da * invCov[static_cast<size_t>(a * D + b)]
                     * (static_cast<double>(x[b]) - mu[static_cast<size_t>(b)]);
    }
    return (d2 > 0.0) ? d2 : 0.0;
}

/// Load a trained MCD model from a JSON file into the given cache slot.
/// Must be called with cache.mutex held.
/// Returns an error message on failure (empty = success).
static QString loadMCDModel(MCDModelCache& cache, const std::string& path)
{
    QFile f(QString::fromStdString(path));
    if(!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QStringLiteral("Cannot open model file '%1': %2")
               .arg(QString::fromStdString(path)).arg(f.errorString());

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if(doc.isNull())
        return QStringLiteral("JSON parse error in '%1': %2")
               .arg(QString::fromStdString(path)).arg(err.errorString());

    const QJsonObject root = doc.object();
    const int D = root["numFeatures"].toInt(0);
    if(D <= 0)
        return QStringLiteral("Invalid 'numFeatures' in model file '%1'.")
               .arg(QString::fromStdString(path));

    const QJsonArray jMean   = root["mean"].toArray();
    const QJsonArray jInvCov = root["invCov"].toArray();
    if(jMean.size() != D || jInvCov.size() != D * D)
        return QStringLiteral("Dimension mismatch in model file '%1': "
               "expected mean[%2] and invCov[%3].")
               .arg(QString::fromStdString(path)).arg(D).arg(D * D);

    cache.numFeatures = D;
    cache.mean.resize(static_cast<size_t>(D));
    cache.invCov.resize(static_cast<size_t>(D * D));
    for(int j = 0; j < D; ++j)
        cache.mean[static_cast<size_t>(j)] = jMean[j].toDouble();
    for(int j = 0; j < D * D; ++j)
        cache.invCov[static_cast<size_t>(j)] = jInvCov[j].toDouble();

    cache.loadedPath = path;
    return {};
}

// ===========================================================================
// Class registration
// ===========================================================================

IMPLEMENT_CREATABLE_OVITO_CLASS(DistortionScoreModifier);
OVITO_CLASSINFO(DistortionScoreModifier, "DisplayName",      "Distortion Score Modifier");
OVITO_CLASSINFO(DistortionScoreModifier, "ModifierCategory", "Structure identification");

DEFINE_PROPERTY_FIELD(DistortionScoreModifier, cutoffRadius);
DEFINE_PROPERTY_FIELD(DistortionScoreModifier, numNeighbors);
DEFINE_PROPERTY_FIELD(DistortionScoreModifier, contaminationFactor);
DEFINE_PROPERTY_FIELD(DistortionScoreModifier, regularization);
DEFINE_PROPERTY_FIELD(DistortionScoreModifier, numCSteps);
DEFINE_PROPERTY_FIELD(DistortionScoreModifier, threshold);
DEFINE_PROPERTY_FIELD(DistortionScoreModifier, modelFilePath);

SET_PROPERTY_FIELD_LABEL(DistortionScoreModifier, cutoffRadius,        "Cutoff radius (Å)");
SET_PROPERTY_FIELD_LABEL(DistortionScoreModifier, numNeighbors,        "Max neighbours in descriptor");
SET_PROPERTY_FIELD_LABEL(DistortionScoreModifier, contaminationFactor, "Contamination factor ν");
SET_PROPERTY_FIELD_LABEL(DistortionScoreModifier, regularization,      "Covariance regularisation");
SET_PROPERTY_FIELD_LABEL(DistortionScoreModifier, numCSteps,           "MCD C-step iterations");
SET_PROPERTY_FIELD_LABEL(DistortionScoreModifier, threshold,           "Defect threshold d_RB");
SET_PROPERTY_FIELD_LABEL(DistortionScoreModifier, modelFilePath,       "Model file (.json)");

SET_PROPERTY_FIELD_UNITS_AND_MINIMUM(DistortionScoreModifier, cutoffRadius,        WorldParameterUnit, 0);
SET_PROPERTY_FIELD_UNITS_AND_RANGE  (DistortionScoreModifier, numNeighbors,        IntegerParameterUnit, 1, 128);
SET_PROPERTY_FIELD_UNITS_AND_RANGE  (DistortionScoreModifier, contaminationFactor, FloatParameterUnit,   0, 0.499);
SET_PROPERTY_FIELD_UNITS_AND_MINIMUM(DistortionScoreModifier, regularization,      FloatParameterUnit,   0);
SET_PROPERTY_FIELD_UNITS_AND_RANGE  (DistortionScoreModifier, numCSteps,           IntegerParameterUnit, 1, 100);
SET_PROPERTY_FIELD_UNITS_AND_MINIMUM(DistortionScoreModifier, threshold,           FloatParameterUnit,   0);

// ===========================================================================
// isApplicableTo
// ===========================================================================

bool DistortionScoreModifier::OOMetaClass::isApplicableTo(const DataCollection& input) const
{
    if(const Particles* p = input.getObject<Particles>())
        return p->getProperty(Particles::PositionProperty) != nullptr;
    return false;
}

// ===========================================================================
// evaluateModifier
// ===========================================================================

Future<PipelineFlowState> DistortionScoreModifier::evaluateModifier(
    const ModifierEvaluationRequest& request,
    PipelineFlowState&& input)
{
    // --- Validate early (calling thread) ------------------------------------

    if(modelFilePath().isEmpty())
        throw Exception(tr("DistortionScoreModifier: no model file specified. "
                           "Please train the model first and set the model file path."));

    const FloatType cutoff  = cutoffRadius();
    const int       maxK    = numNeighbors();
    const FloatType thresh  = threshold();
    const QString   mPath   = modelFilePath();

    if(cutoff <= 0)
        throw Exception(tr("DistortionScoreModifier: cutoff radius must be positive."));
    if(maxK <= 0)
        throw Exception(tr("DistortionScoreModifier: numNeighbors must be at least 1."));

    const SimulationCell* simCell   = input.getObject<SimulationCell>();
    Particles*            particles = input.expectMutableObject<Particles>();
    const Property*       posProp   = particles->expectProperty(Particles::PositionProperty);
    const size_t          N         = posProp->size();

    // Lazily create the model cache slot.
    if(!_modelCacheSlot)
        _modelCacheSlot = std::make_shared<MCDModelCache>();
    std::shared_ptr<void> cacheVoid = _modelCacheSlot;

    // --- Background task ----------------------------------------------------

    return asyncLaunch([
            state = std::move(input),
            simCell, particles, posProp,
            N, maxK, cutoff, thresh,
            mPath = mPath.toStdString(),
            cacheVoid
        ]() mutable -> PipelineFlowState
    {
        TaskProgress progress(this_task::ui());

        // -- 1. Load model from file (if needed) -----------------------------

        MCDModelCache& cache = *static_cast<MCDModelCache*>(cacheVoid.get());
        {
            std::lock_guard<std::mutex> lock(cache.mutex);
            if(cache.loadedPath != mPath) {
                const QString err = loadMCDModel(cache, mPath);
                if(!err.isEmpty())
                    throw Exception(QStringLiteral(
                        "DistortionScoreModifier: %1").arg(err));
            }
        }

        int numFeatures;
        std::vector<double> mu, invCov;
        {
            std::lock_guard<std::mutex> lock(cache.mutex);
            numFeatures = cache.numFeatures;
            mu          = cache.mean;
            invCov      = cache.invCov;
        }

        if(numFeatures != maxK)
            throw Exception(QStringLiteral(
                "DistortionScoreModifier: model was trained with D=%1 neighbours "
                "but the modifier is set to %2. "
                "Please retrain or change 'Max neighbours' to %1.")
                .arg(numFeatures).arg(maxK));

        // -- 2. Build sorted-normalised-neighbour-distance descriptors --------

        progress.setText(tr("Distortion Score: computing descriptors…"));

        std::vector<float> descriptors(N * static_cast<size_t>(maxK));

        CutoffNeighborFinder finder(cutoff, posProp, simCell, nullptr);
        const float invCutoff = 1.0f / static_cast<float>(cutoff);

        parallelForInnerOuter(N, /*chunkSize=*/256, progress,
            [&](auto&& iterate)
        {
            std::vector<FloatType> dists;
            dists.reserve(64);
            iterate([&](size_t i) {
                dists.clear();
                for(CutoffNeighborFinder::Query q(finder, i); !q.atEnd(); q.next())
                    dists.push_back(q.distance());
                std::sort(dists.begin(), dists.end());

                float* row = descriptors.data() + i * static_cast<size_t>(maxK);
                const int k = static_cast<int>(
                    std::min(dists.size(), static_cast<size_t>(maxK)));
                for(int j = 0; j < k; ++j)
                    row[j] = static_cast<float>(dists[j]) * invCutoff;
                for(int j = k; j < maxK; ++j)
                    row[j] = 1.0f;
            });
        });

        // -- 3. Compute robust Mahalanobis distance for every atom ------------

        progress.setText(tr("Distortion Score: computing distortion scores…"));

        Property* scoreProp  = particles->createProperty(
            DataBuffer::Uninitialized, QStringLiteral("DistortionScore_MCD"),
            Property::FloatDefault, 1);
        Property* defectProp = particles->createProperty(
            DataBuffer::Uninitialized, QStringLiteral("IsDefect_MCD"),
            Property::Int32, 1);
        defectProp->addNumericType(Particles::OOClass(), 0, QStringLiteral("Normal"));
        defectProp->addNumericType(Particles::OOClass(), 1, QStringLiteral("Defect"));

        {
            BufferWriteAccess<FloatType, access_mode::discard_write> scoreAcc(scoreProp);
            BufferWriteAccess<int32_t,  access_mode::discard_write> defectAcc(defectProp);

            parallelFor(N, /*chunkSize=*/512, progress, [&](size_t i) {
                const float* xi = descriptors.data() + i * static_cast<size_t>(maxK);
                const double d  = std::sqrt(
                    mahalanobisSq(xi, mu.data(), invCov.data(), maxK));
                scoreAcc[i]  = static_cast<FloatType>(d);
                defectAcc[i] = (d > static_cast<double>(thresh)) ? 1 : 0;
            });
        }

        return std::move(state);
    });
}

}  // namespace Ovito
