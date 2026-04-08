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

#include <ovito/particles/gui/ParticlesGui.h>
#include <ovito/distortion/DistortionScoreModifier.h>
#include <ovito/particles/objects/Particles.h>
#include <ovito/stdobj/simcell/SimulationCell.h>
#include <ovito/core/dataset/data/BufferAccess.h>
#include <ovito/core/utilities/concurrent/Launch.h>
#include <ovito/core/utilities/concurrent/TaskProgress.h>
#include <ovito/gui/desktop/properties/FilenameParameterUI.h>
#include <ovito/gui/desktop/properties/FloatParameterUI.h>
#include <ovito/gui/desktop/properties/IntegerParameterUI.h>
#include <ovito/gui/desktop/properties/ObjectStatusDisplay.h>
#include <ovito/particles/util/CutoffNeighborFinder.h>

#include <QGroupBox>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QLabel>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

#include "DistortionScoreModifierEditor.h"

namespace Ovito {

// ===========================================================================
// Linear-algebra helpers (all static — local to this translation unit)
// ===========================================================================

/// Add reg * I to the diagonal of D×D row-major matrix A.
static void dss_regulariseDiag(std::vector<double>& A, int D, double reg)
{
    for(int j = 0; j < D; ++j)
        A[static_cast<size_t>(j * D + j)] += reg;
}

/// Compute mean of N D-dimensional points (row-major in data).
static void dss_computeMean(const std::vector<double>& data, int N, int D,
                            std::vector<double>& mean)
{
    mean.assign(static_cast<size_t>(D), 0.0);
    for(int i = 0; i < N; ++i)
        for(int j = 0; j < D; ++j)
            mean[static_cast<size_t>(j)] += data[static_cast<size_t>(i * D + j)];
    for(int j = 0; j < D; ++j)
        mean[static_cast<size_t>(j)] /= static_cast<double>(N);
}

/// Compute sample covariance (given precomputed mean) of N D-dimensional points.
static void dss_computeCov(const std::vector<double>& data, int N, int D,
                           const std::vector<double>& mean,
                           std::vector<double>& cov)
{
    cov.assign(static_cast<size_t>(D * D), 0.0);
    for(int i = 0; i < N; ++i) {
        for(int a = 0; a < D; ++a) {
            const double da = data[static_cast<size_t>(i * D + a)]
                            - mean[static_cast<size_t>(a)];
            for(int b = a; b < D; ++b) {
                const double db = data[static_cast<size_t>(i * D + b)]
                                - mean[static_cast<size_t>(b)];
                cov[static_cast<size_t>(a * D + b)] += da * db;
            }
        }
    }
    const double inv = (N > 1) ? 1.0 / static_cast<double>(N - 1) : 1.0;
    for(int a = 0; a < D; ++a) {
        for(int b = a; b < D; ++b) {
            cov[static_cast<size_t>(a * D + b)] *= inv;
            cov[static_cast<size_t>(b * D + a)]  = cov[static_cast<size_t>(a * D + b)];
        }
    }
}

/// Compute mean of h rows selected by idx from data.
static void dss_computeMeanSubset(const std::vector<double>& data,
                                  const std::vector<int>& idx, int h, int D,
                                  std::vector<double>& mean)
{
    mean.assign(static_cast<size_t>(D), 0.0);
    for(int ii = 0; ii < h; ++ii) {
        const int i = idx[static_cast<size_t>(ii)];
        for(int j = 0; j < D; ++j)
            mean[static_cast<size_t>(j)] += data[static_cast<size_t>(i * D + j)];
    }
    for(int j = 0; j < D; ++j)
        mean[static_cast<size_t>(j)] /= static_cast<double>(h);
}

/// Compute sample covariance of h rows selected by idx from data.
static void dss_computeCovSubset(const std::vector<double>& data,
                                 const std::vector<int>& idx, int h, int D,
                                 const std::vector<double>& mean,
                                 std::vector<double>& cov)
{
    cov.assign(static_cast<size_t>(D * D), 0.0);
    for(int ii = 0; ii < h; ++ii) {
        const int i = idx[static_cast<size_t>(ii)];
        for(int a = 0; a < D; ++a) {
            const double da = data[static_cast<size_t>(i * D + a)]
                            - mean[static_cast<size_t>(a)];
            for(int b = a; b < D; ++b) {
                const double db = data[static_cast<size_t>(i * D + b)]
                                - mean[static_cast<size_t>(b)];
                cov[static_cast<size_t>(a * D + b)] += da * db;
            }
        }
    }
    const double inv = (h > 1) ? 1.0 / static_cast<double>(h - 1) : 1.0;
    for(int a = 0; a < D; ++a) {
        for(int b = a; b < D; ++b) {
            cov[static_cast<size_t>(a * D + b)] *= inv;
            cov[static_cast<size_t>(b * D + a)]  = cov[static_cast<size_t>(a * D + b)];
        }
    }
}

/// In-place Gauss–Jordan inversion of D×D row-major matrix A.
/// Returns false if the matrix is (near-)singular.
static bool dss_invertMatrix(std::vector<double>& A, int D)
{
    const int W = 2 * D;
    std::vector<double> aug(static_cast<size_t>(D * W), 0.0);
    for(int i = 0; i < D; ++i) {
        for(int j = 0; j < D; ++j)
            aug[static_cast<size_t>(i * W + j)] = A[static_cast<size_t>(i * D + j)];
        aug[static_cast<size_t>(i * W + D + i)] = 1.0;
    }

    for(int col = 0; col < D; ++col) {
        // Partial pivot.
        int    maxRow = col;
        double maxVal = std::abs(aug[static_cast<size_t>(col * W + col)]);
        for(int row = col + 1; row < D; ++row) {
            const double val = std::abs(aug[static_cast<size_t>(row * W + col)]);
            if(val > maxVal) { maxVal = val; maxRow = row; }
        }
        if(maxVal < 1e-15) return false;

        if(maxRow != col)
            for(int j = 0; j < W; ++j)
                std::swap(aug[static_cast<size_t>(col * W + j)],
                          aug[static_cast<size_t>(maxRow * W + j)]);

        const double pivot = aug[static_cast<size_t>(col * W + col)];
        for(int j = 0; j < W; ++j)
            aug[static_cast<size_t>(col * W + j)] /= pivot;

        for(int row = 0; row < D; ++row) {
            if(row == col) continue;
            const double factor = aug[static_cast<size_t>(row * W + col)];
            if(factor == 0.0) continue;
            for(int j = 0; j < W; ++j)
                aug[static_cast<size_t>(row * W + j)]
                    -= factor * aug[static_cast<size_t>(col * W + j)];
        }
    }

    for(int i = 0; i < D; ++i)
        for(int j = 0; j < D; ++j)
            A[static_cast<size_t>(i * D + j)] = aug[static_cast<size_t>(i * W + D + j)];
    return true;
}

// ===========================================================================
// Simplified FAST-MCD training (runs in a background thread)
// ===========================================================================

struct DistortionTrainingResult
{
    int     numSamples   = 0;
    int     numFeatures  = 0;
    QString savedPath;
};

static DistortionTrainingResult runTraining(
    PipelineFlowState   state,
    float               cutoff,
    int                 maxNeighbors,
    double              nu,
    double              reg,
    int                 nCSteps,
    const std::string&  outPath)
{
    TaskProgress progress(this_task::ui());

    // -----------------------------------------------------------------------
    // 1. Extract particle data
    // -----------------------------------------------------------------------

    const Particles*      particles = state.getObject<Particles>();
    const SimulationCell* simCell   = state.getObject<SimulationCell>();

    if(!particles || !simCell)
        throw Exception(QStringLiteral(
            "DistortionScore: the current frame has no particles or no "
            "simulation cell. A periodic simulation cell is required."));

    const Property* posProp = particles->getProperty(Particles::PositionProperty);
    if(!posProp)
        throw Exception(QStringLiteral("DistortionScore: no Position property found."));

    const size_t N_all = posProp->size();
    if(N_all == 0)
        throw Exception(QStringLiteral("DistortionScore: frame contains no atoms."));

    // Use selection (if present) to restrict training to 'bulk' atoms.
    const Property* selProp = particles->getProperty(Particles::SelectionProperty);
    std::vector<size_t> trainIdx;
    if(selProp) {
        BufferReadAccess<SelectionIntType> selAcc(selProp);
        trainIdx.reserve(N_all);
        for(size_t i = 0; i < N_all; ++i)
            if(selAcc[i]) trainIdx.push_back(i);
    }
    const bool   useSelection = (selProp && !trainIdx.empty());
    const size_t N = useSelection ? trainIdx.size() : N_all;

    if(N == 0)
        throw Exception(QStringLiteral(
            "DistortionScore: no atoms selected for training.\n"
            "Clear the selection (train on all atoms) or select reference "
            "'bulk' atoms before clicking Train."));

    // -----------------------------------------------------------------------
    // 2. Build descriptor matrix [N × D]  (double precision)
    // -----------------------------------------------------------------------

    progress.setText(QStringLiteral(
        "DistortionScore: building descriptors (%1 atoms, D=%2)…")
        .arg(N).arg(maxNeighbors));

    const int D = maxNeighbors;
    std::vector<double> data(N * static_cast<size_t>(D), 1.0);

    {
        CutoffNeighborFinder finder(static_cast<FloatType>(cutoff),
                                   posProp, simCell, nullptr);
        const double invCutoff = 1.0 / static_cast<double>(cutoff);
        std::vector<FloatType> dists;
        dists.reserve(64);

        for(size_t ii = 0; ii < N; ++ii) {
            if(this_task::get()) this_task::throwIfCanceled();

            const size_t i = useSelection ? trainIdx[ii] : ii;
            dists.clear();
            for(CutoffNeighborFinder::Query q(finder, i); !q.atEnd(); q.next())
                dists.push_back(q.distance());
            std::sort(dists.begin(), dists.end());

            double* row = data.data() + ii * static_cast<size_t>(D);
            const int k = static_cast<int>(
                std::min(dists.size(), static_cast<size_t>(D)));
            for(int j = 0; j < k; ++j)
                row[j] = static_cast<double>(dists[j]) * invCutoff;
            // Remaining slots keep 1.0 (set by constructor).
        }
    }

    // -----------------------------------------------------------------------
    // 3. Simplified FAST-MCD: C-step iterations
    // -----------------------------------------------------------------------

    progress.setText(QStringLiteral(
        "DistortionScore: running FAST-MCD (%1 C-steps, ν=%2)…")
        .arg(nCSteps).arg(nu));

    const int nInt = static_cast<int>(N);
    if(nInt < D + 2)
        throw Exception(QStringLiteral(
            "DistortionScore: too few training atoms (%1) for descriptor "
            "dimensionality D=%2.  Need at least D+2 = %3 atoms.")
            .arg(N).arg(D).arg(D + 2));

    const int h = static_cast<int>(
        std::floor((1.0 - nu) * static_cast<double>(nInt)));
    if(h < D + 2)
        throw Exception(QStringLiteral(
            "DistortionScore: the retained subset h=%1 is too small (D=%2). "
            "Reduce ν or provide more training atoms.").arg(h).arg(D));

    // Initialise from full-sample estimates.
    std::vector<double> mean, cov;
    dss_computeMean(data, nInt, D, mean);
    dss_computeCov (data, nInt, D, mean, cov);

    std::vector<int>    idx(static_cast<size_t>(nInt));
    std::vector<double> dist2(static_cast<size_t>(nInt));
    std::vector<double> meanTmp, covTmp, invCovTry;

    for(int step = 0; step < nCSteps; ++step) {
        if(this_task::get()) this_task::throwIfCanceled();

        // Regularise and invert current covariance.
        invCovTry = cov;
        dss_regulariseDiag(invCovTry, D, reg);
        if(!dss_invertMatrix(invCovTry, D))
            break; // singular — keep last good mean/cov

        // Mahalanobis distances for all N atoms.
        for(int i = 0; i < nInt; ++i) {
            double d2 = 0.0;
            for(int a = 0; a < D; ++a) {
                const double da = data[static_cast<size_t>(i * D + a)]
                                - mean[static_cast<size_t>(a)];
                for(int b = 0; b < D; ++b)
                    d2 += da * invCovTry[static_cast<size_t>(a * D + b)]
                             * (data[static_cast<size_t>(i * D + b)]
                                - mean[static_cast<size_t>(b)]);
            }
            dist2[static_cast<size_t>(i)] = (d2 > 0.0) ? d2 : 0.0;
            idx  [static_cast<size_t>(i)] = i;
        }

        // Retain h atoms with smallest distances.
        std::partial_sort(
            idx.begin(), idx.begin() + h, idx.end(),
            [&dist2](int a, int b){
                return dist2[static_cast<size_t>(a)] < dist2[static_cast<size_t>(b)];
            });

        // Recompute mean and covariance from the subset.
        dss_computeMeanSubset(data, idx, h, D, meanTmp);
        dss_computeCovSubset (data, idx, h, D, meanTmp, covTmp);
        mean = meanTmp;
        cov  = covTmp;
    }

    // Final covariance inversion.
    std::vector<double> invCov = cov;
    dss_regulariseDiag(invCov, D, reg);
    if(!dss_invertMatrix(invCov, D))
        throw Exception(QStringLiteral(
            "DistortionScore: the covariance matrix is singular after training. "
            "Try increasing the regularisation, reducing D, or adding more training atoms."));

    // -----------------------------------------------------------------------
    // 4. Save model to JSON
    // -----------------------------------------------------------------------

    progress.setText(QStringLiteral("DistortionScore: saving model to %1…")
        .arg(QString::fromStdString(outPath)));

    {
        QJsonArray jMean, jInvCov;
        for(int j = 0; j < D; ++j)
            jMean.append(mean[static_cast<size_t>(j)]);
        for(int j = 0; j < D * D; ++j)
            jInvCov.append(invCov[static_cast<size_t>(j)]);

        QJsonObject root;
        root["numFeatures"] = D;
        root["mean"]        = jMean;
        root["invCov"]      = jInvCov;

        QFile f(QString::fromStdString(outPath));
        if(!f.open(QIODevice::WriteOnly | QIODevice::Text))
            throw Exception(QStringLiteral(
                "DistortionScore: cannot open '%1' for writing: %2")
                .arg(QString::fromStdString(outPath)).arg(f.errorString()));
        f.write(QJsonDocument(root).toJson());
    }

    DistortionTrainingResult result;
    result.numSamples  = static_cast<int>(N);
    result.numFeatures = D;
    result.savedPath   = QString::fromStdString(outPath);
    return result;
}

// ===========================================================================
// Editor class registration
// ===========================================================================

IMPLEMENT_CREATABLE_OVITO_CLASS(DistortionScoreModifierEditor);
SET_OVITO_OBJECT_EDITOR(DistortionScoreModifier, DistortionScoreModifierEditor);

// ===========================================================================
// createUI
// ===========================================================================

void DistortionScoreModifierEditor::createUI(const RolloutInsertionParameters& rolloutParams)
{
    QWidget*     rollout    = createRollout(tr("Distortion Score Modifier"), rolloutParams);
    QVBoxLayout* mainLayout = new QVBoxLayout(rollout);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(6);

    // -----------------------------------------------------------------------
    // Descriptor parameters
    // -----------------------------------------------------------------------
    {
        QGroupBox*   box  = new QGroupBox(tr("Descriptor"), rollout);
        QGridLayout* grid = new QGridLayout(box);
        grid->setContentsMargins(4, 4, 4, 4);
        grid->setColumnStretch(1, 1);
        int row = 0;

        FloatParameterUI* cutoffUI = createParamUI<FloatParameterUI>(
            PROPERTY_FIELD(DistortionScoreModifier::cutoffRadius));
        grid->addWidget(cutoffUI->label(), row, 0);
        grid->addLayout(cutoffUI->createFieldLayout(), row, 1);
        ++row;

        IntegerParameterUI* neighUI = createParamUI<IntegerParameterUI>(
            PROPERTY_FIELD(DistortionScoreModifier::numNeighbors));
        grid->addWidget(neighUI->label(), row, 0);
        grid->addLayout(neighUI->createFieldLayout(), row, 1);
        ++row;

        grid->addWidget(new QLabel(
            tr("<small>Sorted, normalised neighbour distances within the cutoff.\n"
               "Defines the descriptor dimensionality D. Must match training.</small>"),
            box), row, 0, 1, 2);

        mainLayout->addWidget(box);
    }

    // -----------------------------------------------------------------------
    // MCD training parameters
    // -----------------------------------------------------------------------
    {
        QGroupBox*   box  = new QGroupBox(tr("MCD training parameters"), rollout);
        QGridLayout* grid = new QGridLayout(box);
        grid->setContentsMargins(4, 4, 4, 4);
        grid->setColumnStretch(1, 1);
        int row = 0;

        FloatParameterUI* nuUI = createParamUI<FloatParameterUI>(
            PROPERTY_FIELD(DistortionScoreModifier::contaminationFactor));
        grid->addWidget(nuUI->label(), row, 0);
        grid->addLayout(nuUI->createFieldLayout(), row, 1);
        ++row;

        FloatParameterUI* regUI = createParamUI<FloatParameterUI>(
            PROPERTY_FIELD(DistortionScoreModifier::regularization));
        grid->addWidget(regUI->label(), row, 0);
        grid->addLayout(regUI->createFieldLayout(), row, 1);
        ++row;

        IntegerParameterUI* stepsUI = createParamUI<IntegerParameterUI>(
            PROPERTY_FIELD(DistortionScoreModifier::numCSteps));
        grid->addWidget(stepsUI->label(), row, 0);
        grid->addLayout(stepsUI->createFieldLayout(), row, 1);
        ++row;

        grid->addWidget(new QLabel(
            tr("<small>ν: expected fraction of outliers (0.07 = 7%).\n"
               "Retained subset h = ⌊(1−ν)×N⌋ atoms per C-step.\n"
               "Regularisation ε added to covariance diagonal.</small>"),
            box), row, 0, 1, 2);

        mainLayout->addWidget(box);
    }

    // -----------------------------------------------------------------------
    // Model file path
    // -----------------------------------------------------------------------
    {
        QGroupBox*   box = new QGroupBox(tr("Model file"), rollout);
        QVBoxLayout* lay = new QVBoxLayout(box);
        lay->setContentsMargins(4, 4, 4, 4);
        lay->setSpacing(4);

        lay->addWidget(new QLabel(
            tr("JSON file where the trained MCD model is saved / loaded:"), box));

        QStringList jsonFilter = { tr("JSON files (*.json)"), tr("All files (*)") };
        FilenameParameterUI* pathUI = createParamUI<FilenameParameterUI>(
            PROPERTY_FIELD(DistortionScoreModifier::modelFilePath),
            jsonFilter, /*existingFile=*/false);
        lay->addWidget(pathUI->selectorWidget());

        mainLayout->addWidget(box);
    }

    // -----------------------------------------------------------------------
    // Train button
    // -----------------------------------------------------------------------
    {
        QGroupBox*   box = new QGroupBox(tr("Train"), rollout);
        QVBoxLayout* lay = new QVBoxLayout(box);
        lay->setContentsMargins(4, 4, 4, 4);
        lay->setSpacing(6);

        lay->addWidget(new QLabel(
            tr("<small>Navigate to a defect-free reference frame\n"
               "(or select reference 'bulk' atoms), then click Train.\n"
               "If atoms are selected, only selected atoms are used.</small>"),
            box));

        _trainButton = new QPushButton(tr("Train on current frame"), box);
        _trainButton->setToolTip(tr(
            "Evaluate the current animation frame, build sorted-normalised-\n"
            "neighbour-distance descriptors, run simplified FAST-MCD,\n"
            "and save the model as a JSON file."));
        lay->addWidget(_trainButton);
        connect(_trainButton, &QPushButton::clicked,
                this, &DistortionScoreModifierEditor::onTrainClicked);

        _statusLabel = new QLabel(tr("Not trained yet."), box);
        _statusLabel->setWordWrap(true);
        lay->addWidget(_statusLabel);

        mainLayout->addWidget(box);
    }

    // -----------------------------------------------------------------------
    // Defect detection / threshold
    // -----------------------------------------------------------------------
    {
        QGroupBox*   box  = new QGroupBox(tr("Defect detection"), rollout);
        QGridLayout* grid = new QGridLayout(box);
        grid->setContentsMargins(4, 4, 4, 4);
        grid->setColumnStretch(1, 1);
        int row = 0;

        FloatParameterUI* thrUI = createParamUI<FloatParameterUI>(
            PROPERTY_FIELD(DistortionScoreModifier::threshold));
        grid->addWidget(thrUI->label(), row, 0);
        grid->addLayout(thrUI->createFieldLayout(), row, 1);
        ++row;

        grid->addWidget(new QLabel(
            tr("<small>Atoms with DistortionScore_MCD > threshold\n"
               "are flagged as Defect in IsDefect_MCD.</small>"),
            box), row, 0, 1, 2);

        mainLayout->addWidget(box);
    }

    // Pipeline status display.
    mainLayout->addSpacing(4);
    mainLayout->addWidget(createParamUI<ObjectStatusDisplay>()->statusWidget());
}

// ===========================================================================
// onTrainClicked
// ===========================================================================

void DistortionScoreModifierEditor::onTrainClicked()
{
    auto* mod = static_cast<DistortionScoreModifier*>(editObject());
    if(!mod) return;

    if(mod->cutoffRadius() <= 0) {
        QMessageBox::warning(parentWindow(), tr("Invalid parameter"),
            tr("Cutoff radius must be positive."));
        return;
    }
    if(mod->numNeighbors() <= 0) {
        QMessageBox::warning(parentWindow(), tr("Invalid parameter"),
            tr("Max neighbours must be at least 1."));
        return;
    }
    if(mod->modelFilePath().trimmed().isEmpty()) {
        QMessageBox::warning(parentWindow(), tr("Invalid parameter"),
            tr("Please specify a model file path (.json) before training."));
        return;
    }

    // Capture parameters by value.
    const float        cutoff   = static_cast<float>(mod->cutoffRadius());
    const int          maxNeigh = mod->numNeighbors();
    const double       nu       = static_cast<double>(mod->contaminationFactor());
    const double       reg      = static_cast<double>(mod->regularization());
    const int          nCSteps  = mod->numCSteps();
    const std::string  outPath  = mod->modelFilePath().toStdString();

    if(_statusLabel)
        _statusLabel->setText(tr("Collecting data from current frame…"));

    // Retrieve the upstream pipeline state for the current frame.
    PipelineFlowState state = getPipelineInput();
    if(state.status().type() == PipelineStatus::Error || !state) {
        if(_statusLabel)
            _statusLabel->setText(
                tr("Error: no valid pipeline data for the current frame."));
        return;
    }

    if(_statusLabel)
        _statusLabel->setText(tr("Training FAST-MCD model…"));

    QPointer<DistortionScoreModifierEditor> self(this);

    auto future = asyncLaunch(
        [state = std::move(state),
         cutoff, maxNeigh, nu, reg, nCSteps, outPath]() mutable
        -> DistortionTrainingResult
    {
        return runTraining(std::move(state),
                           cutoff, maxNeigh, nu, reg, nCSteps, outPath);
    });

    scheduleOperationAfter(std::move(future),
        [self](DistortionTrainingResult result) {
            if(!self) return;

            // Invalidate cache so the modifier reloads from the new JSON.
            if(auto* mod = static_cast<DistortionScoreModifier*>(self->editObject()))
                mod->_modelCacheSlot.reset();

            if(self->_statusLabel)
                self->_statusLabel->setText(QStringLiteral(
                    "Training complete!\n"
                    "  Atoms: %1   D: %2\n"
                    "  Saved: %3")
                    .arg(result.numSamples)
                    .arg(result.numFeatures)
                    .arg(result.savedPath));
        });
}

}  // namespace Ovito
