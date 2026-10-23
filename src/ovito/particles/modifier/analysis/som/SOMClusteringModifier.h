////////////////////////////////////////////////////////////////////////////////////////
//
//  Copyright 2026 OVITO GmbH, Germany
//
//  This file is part of OVITO (Open Visualization Tool).
//
////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <ovito/particles/Particles.h>
#include <ovito/core/dataset/pipeline/Modifier.h>

#include "SomAlgorithm.h"

namespace Ovito {

/**
 * \brief Unsupervised per-atom clustering with a multilayer Self-Organizing Map (Kohonen
 * network), ported from the MultiSOM-main reference implementation (Franco Aquistapace,
 * 2021, Apache-2.0).
 *
 * The modifier is trained once via the "Train" button in its editor panel -- on whichever
 * animation frame is currently displayed -- and the trained model (weights, per-feature
 * scaling statistics, and the layer/mapping configuration in effect at training time) is
 * embedded in trainedModelJson(), so the SAME trained model is then applied consistently
 * to every frame the modifier evaluates, including frames not present when it was trained.
 *
 * Re-editing layersConfigJson()/mappingMode()/onlySelectedParticles() after training does not
 * change the applied model until the user clicks Train again -- inference always uses what is
 * baked into trainedModelJson().
 */
class OVITO_PARTICLES_EXPORT SOMClusteringModifier : public Modifier
{
    class OVITO_PARTICLES_EXPORT SOMClusteringModifierClass : public Modifier::OOMetaClass
    {
    public:
        using Modifier::OOMetaClass::OOMetaClass;
        virtual bool isApplicableTo(const DataCollection& input) const override;
    };

    OVITO_CLASS_META(SOMClusteringModifier, SOMClusteringModifierClass)

public:

    enum MappingMode {
        Default = 0,
        Godel = 1,
        Linear = 2
    };
    Q_ENUM(MappingMode)

    /// Converts the modifier-level enum (an OVITO Q_ENUM, needed for the property field) to
    /// the algorithm-level enum used by SomAlgorithm (kept independent of any Modifier/Qt-
    /// object dependency, see SomAlgorithm.h). Shared by the editor (to bake a mapping mode
    /// into a freshly trained model) so the two enums cannot drift out of sync with each other.
    static SomAlgorithm::MappingMode toAlgorithmMappingMode(MappingMode mode) {
        switch(mode) {
            case Godel: return SomAlgorithm::MappingMode::Godel;
            case Linear: return SomAlgorithm::MappingMode::Linear;
            default: return SomAlgorithm::MappingMode::Default;
        }
    }

    virtual Future<PipelineFlowState> evaluateModifier(
        const ModifierEvaluationRequest& request,
        PipelineFlowState&& state) override;

    virtual bool shouldRefreshViewportsAfterEvaluation() override { return true; }

    /// Resolves the union of every feature referenced by any of 'layers' against 'particles',
    /// in deterministic (sorted) order. Shared by evaluateModifier() (inference) and the
    /// editor's onTrainClicked() (training) so the two cannot silently diverge in how they
    /// look up the same PropertyReference strings. Returns false and fills 'errorMessage'
    /// (naming the first unresolved feature) if any referenced feature is not found; the two
    /// callers differ only in how they surface that message (thrown Exception vs. a status label).
    static bool resolveFeatureRefs(
        const std::vector<SomAlgorithm::LayerConfig>& layers,
        const Particles* particles,
        QStringList& outRefList,
        std::vector<ConstPropertyPtr>& outRefProps,
        std::vector<int>& outRefComps,
        QString& errorMessage);

private:

    /// JSON-encoded array of per-layer configurations (feature list, scaling method, N,
    /// sigma, eta, training fraction). Edited only through the editor's layer-list widget;
    /// see SomAlgorithm::layersToJson/layersFromJson for the schema.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(QString{QStringLiteral("[]")}, layersConfigJson, setLayersConfigJson, PROPERTY_FIELD_MEMORIZE)

    /// How a child SOM's local BMU index combines with its parent branch's id.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(MappingMode{Default}, mappingMode, setMappingMode, PROPERTY_FIELD_MEMORIZE)

    /// Restricts training to the currently selected particles. Like mappingMode, it takes
    /// effect on the next Train click; what inference does is decided by the flag baked into
    /// the trained model (SomAlgorithm::TrainedModel::trainedOnSelection).
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(bool{false}, onlySelectedParticles, setOnlySelectedParticles, PROPERTY_FIELD_MEMORIZE)

    /// Seed for weight initialization and training subsampling. Defaults to a fixed value so
    /// that training is reproducible out of the box: a SOM is randomly initialized, so an
    /// unseeded run gives a different partition every time it is clicked, and a result nobody
    /// can reproduce -- including the person who produced it -- is not a result. Set to -1 to
    /// seed from std::random_device instead, which is the right choice for checking how stable
    /// a clustering is across initializations.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(int{0}, randomSeed, setRandomSeed, PROPERTY_FIELD_MEMORIZE)

    /// The trained model (SomAlgorithm::TrainedModel, JSON-encoded), populated by the
    /// editor's "Train" button. Empty until the first training run.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(QString{}, trainedModelJson, setTrainedModelJson)

    /// Base name used for the output particle propert(ies).
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(QString{QStringLiteral("SOM Cluster")}, outputPropertyBaseName, setOutputPropertyBaseName, PROPERTY_FIELD_MEMORIZE)

    /// Human-readable summary of the last training run (layer/branch counts, atom count),
    /// shown in the editor's status label. Recomputed by the editor after every Train click.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(QString{}, trainingProvenance, setTrainingProvenance)
};

} // End of namespace
