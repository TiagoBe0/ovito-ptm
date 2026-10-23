////////////////////////////////////////////////////////////////////////////////////////
//
//  Copyright 2026 OVITO GmbH, Germany
//
//  This file is part of OVITO (Open Visualization Tool).
//
////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <ovito/particles/Particles.h>

#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QJsonArray>

#include <cstdint>
#include <functional>
#include <map>
#include <random>
#include <vector>

namespace Ovito {

/**
 * \brief Core, Qt-JSON-only implementation of the multilayer Self-Organizing-Map (Kohonen
 * network) clustering algorithm ported from the MultiSOM-main Python reference implementation
 * (Franco Aquistapace, 2021, Apache-2.0).
 *
 * This header/translation unit deliberately has NO dependency on Modifier, PipelineFlowState,
 * or any other OVITO pipeline type, so the algorithm can be exercised by a standalone self-test
 * (see SomSelfTestService) without booting the pipeline/GUI machinery. It only knows how to:
 *   - train a (possibly multi-layer, hierarchical) tree of SOMs from pre-extracted per-atom
 *     feature vectors,
 *   - classify a single atom's feature vector by walking a previously trained tree,
 *   - serialize/deserialize the whole trained model to/from JSON.
 *
 * Reading raw property values out of a Particles data collection, and writing the resulting
 * cluster ids back as new particle properties, is the job of SOMClusteringModifier.
 */
namespace SomAlgorithm {

/// Per-feature scaling method, applied independently to every input feature column,
/// with statistics computed from the training population and re-applied verbatim at
/// inference time (see FeatureScaling below).
enum class ScalingMethod {
    Normal,     ///< Min-max scaling to [0,1]: (x - min) / (max - min).
    Standard,   ///< Z-score scaling: (x - mean) / std.
    Robust      ///< Median/IQR scaling: (x - median) / IQR.
};

/// How a child SOM's local best-matching-unit (BMU) index combines with its parent
/// branch's id to produce the final per-atom cluster id, applied identically at every layer.
enum class MappingMode {
    Default,    ///< No combination: every layer's local BMU index is kept as its own separate output property.
    Godel,      ///< Prime-power encoding: newId = prevId * nthPrime(layer) ^ (localId + 1).
    /// Recursive interval narrowing within (0,1), producing a single floating-point id.
    ///
    /// NOTE: unlike Default and Godel, which reproduce the reference implementation exactly,
    /// this is a deliberate REINTERPRETATION of MultiSOM-main's map_to_cluster_linear(), not
    /// a transcription of it. The reference rescales each branch's ids by the minimum and
    /// maximum group index that happened to be *observed* in that branch, so the id an atom
    /// receives depends on which sibling clusters turned out non-empty; and it maps a group
    /// to the lower edge of its interval. Here the interval is instead divided by the layer's
    /// *configured* neuron count and the id is the upper edge of the atom's cell. The result
    /// is an id that depends only on the model and the atom -- reproducible across runs and
    /// across datasets -- at the cost of not being numerically comparable to ids produced by
    /// the Python reference.
    Linear
};

OVITO_PARTICLES_EXPORT QString scalingMethodToString(ScalingMethod method);
OVITO_PARTICLES_EXPORT ScalingMethod scalingMethodFromString(const QString& s, bool* ok = nullptr);
OVITO_PARTICLES_EXPORT QString mappingModeToString(MappingMode mode);
OVITO_PARTICLES_EXPORT MappingMode mappingModeFromString(const QString& s, bool* ok = nullptr);

/// Persisted scaling statistics for one input feature column.
/// Meaning of stat1/stat2 depends on 'method':
///   Normal:   stat1 = min,    stat2 = max
///   Standard: stat1 = mean,   stat2 = std
///   Robust:   stat1 = median, stat2 = IQR
struct OVITO_PARTICLES_EXPORT FeatureScaling {
    ScalingMethod method = ScalingMethod::Normal;
    double stat1 = 0.0;
    double stat2 = 1.0;

    /// Applies this scaling to a single raw feature value. Divide-by-zero (degenerate
    /// feature column: delta/std/IQR == 0) is guarded by substituting a scale of 1,
    /// matching the MultiSOM-main reference behavior.
    double apply(double raw) const;

    QJsonObject toJson() const;
    static FeatureScaling fromJson(const QJsonObject& obj);
};

/// Configuration of a single layer of the SOM hierarchy, as entered by the user.
/// sigma/eta/sampleFraction are training-time-only parameters, kept here purely for
/// provenance (they do not affect inference of an already-trained model).
struct OVITO_PARTICLES_EXPORT LayerConfig {
    /// PropertyReference::nameWithComponent() strings, in feature-vector order.
    QStringList featureRefs;
    ScalingMethod scaling = ScalingMethod::Normal;
    int numNeurons = 4;
    double maxSigma = 0.5;
    double maxEta = 0.5;
    double sampleFraction = 1.0;

    QJsonObject toJson() const;
    static LayerConfig fromJson(const QJsonObject& obj);
};

/// One trained SOM: an (inputSize x numNeurons) weight matrix plus the per-feature scaling
/// statistics that must be applied to a raw feature vector before computing distances to it.
struct OVITO_PARTICLES_EXPORT SomNode {
    int inputSize = 0;
    int numNeurons = 0;
    /// Flattened, row-major: weights[neuron * inputSize + feature].
    std::vector<float> weights;
    /// Size == inputSize.
    std::vector<FeatureScaling> scaling;

    /// Applies the stored per-feature scaling to a raw feature vector (size == inputSize).
    std::vector<float> scaleFeatures(const std::vector<float>& raw) const;

    /// Returns the index of the best-matching unit (nearest neuron, Euclidean distance)
    /// for an already-scaled feature vector.
    int bmu(const std::vector<float>& scaledFeatureVec) const;

    QJsonObject toJson() const;
    static SomNode fromJson(const QJsonObject& obj);
};

/// One node of the trained hierarchy: the SOM trained for this branch, plus a sparse map
/// of child branches (only local BMU indices that actually received training atoms are
/// present) trained at the next configured layer. Empty 'children' means this is either a
/// leaf (deepest configured layer) or a branch with no atoms.
struct OVITO_PARTICLES_EXPORT SomTreeNode {
    SomNode som;
    std::map<int, SomTreeNode> children;

    QJsonObject toJson() const;
    static SomTreeNode fromJson(const QJsonObject& obj);
};

/// One pre-extracted training sample: every feature column referenced by ANY configured
/// layer, indexed via the 'featureIndex' map passed to trainTree(), for one atom.
struct OVITO_PARTICLES_EXPORT TrainingSample {
    std::vector<float> allFeatureValues;
};

/// Statistics about a training run that the caller needs in order to report honestly what
/// the model was actually fitted on.
struct OVITO_PARTICLES_EXPORT TrainingReport {
    /// Number of samples handed to trainTree().
    size_t samplesTotal = 0;
    /// Number of samples excluded because they carry a non-finite (NaN/inf) value in a
    /// feature of the layer they reached. Each excluded sample is counted exactly once:
    /// an excluded sample is not propagated into any child branch, so it is never revisited
    /// at a deeper layer.
    size_t skippedNonFinite = 0;
};

/// Trains the full hierarchical SOM tree described by 'layers' (layers[0] is the root,
/// trained on every sample; layers[1] is trained separately per non-empty branch found by
/// layers[0]; etc.).
///
/// 'featureIndex' maps a PropertyReference name-with-component string (as used in
/// LayerConfig::featureRefs) to its column index in TrainingSample::allFeatureValues.
///
/// Samples carrying a non-finite (NaN/inf) value in any feature of the layer they reach are
/// excluded from that layer's scaling statistics, from its online training walk, and from
/// its partition into child branches. This is not a convenience: a single NaN reaching the
/// weight update turns EVERY weight of that SOM into NaN permanently (h = exp(-NaN) = NaN),
/// after which every atom's best-matching unit degenerates to neuron 0 -- i.e. the layer
/// silently collapses to a single cluster while still producing a well-formed model. The
/// count of excluded samples is reported through 'report' so the caller can surface it.
///
/// Throws Exception (see ovito/core/Exception.h, included transitively) if a layer
/// references a feature not present in 'featureIndex', or if 'layers' is empty.
OVITO_PARTICLES_EXPORT SomTreeNode trainTree(
    const std::vector<LayerConfig>& layers,
    const std::vector<TrainingSample>& samples,
    const std::map<QString, int>& featureIndex,
    std::mt19937& rng,
    const std::function<void(const QString&)>& progressText = {},
    TrainingReport* report = nullptr);

/// Result of classifying a single atom's feature vector against a trained tree.
struct OVITO_PARTICLES_EXPORT ClassificationResult {
    /// Valid when mode == Godel.
    int64_t intId = 0;
    /// Valid when mode == Linear.
    double floatId = 0.0;
    /// Valid when mode == Default: one entry per configured layer; entries beyond the
    /// deepest layer actually reached (see hitMissingBranch) are set to -1.
    std::vector<int> perLayerIds;
    /// True if the atom's BMU at some layer pointed to a branch that received no atoms
    /// during training (an "empty supercluster"), so classification stopped early and the
    /// returned id is coarser than the full configured depth.
    bool hitMissingBranch = false;
    /// True if, under Godel mapping, the prime-power encoding ran out of Int64 headroom
    /// before reaching the deepest configured layer. Unlike hitMissingBranch, this is not
    /// a sign of insufficient training data -- every branch involved may be fully trained --
    /// it just means the configured hierarchy is too deep/wide to encode into a single
    /// Int64 id. The returned intId is coarser than the full configured depth.
    bool godelOverflow = false;
    /// True if the atom carries a non-finite (NaN/inf) value in a feature required by a
    /// layer that was actually reached. Such an atom cannot be assigned a best-matching
    /// unit at all (every distance to it is NaN), so classification stops at that layer
    /// and the ids are set to unambiguous sentinels: intId = 0 (no valid Godel id is ever
    /// 0), floatId = 0.0 (no valid Linear id is ever 0, they lie strictly inside (0,1)),
    /// and perLayerIds keeps the ids of the layers already passed, -1 from this one on.
    bool invalidFeatures = false;
};

/// Classifies one atom by walking 'root' layer by layer. 'featureLookup(layerIndex, k)' must
/// return the raw (unscaled) value, for this atom, of layers[layerIndex].featureRefs[k]; it is
/// called once per feature referenced by each layer actually visited (a layer below a missing
/// branch is never queried).
///
/// Features are addressed by position rather than by name on purpose: this function runs once
/// per atom per frame, so resolving a QString through a hash table here would put string
/// hashing in the innermost loop of the modifier. Callers resolve each layer's feature columns
/// once, up front, and the callback becomes an array index.
OVITO_PARTICLES_EXPORT ClassificationResult classify(
    const SomTreeNode& root,
    const std::vector<LayerConfig>& layers,
    const std::function<float(size_t layerIndex, int featureIndex)>& featureLookup,
    MappingMode mode);

/// The full, self-describing trained model: the layer configuration and mapping mode that
/// were in effect when training ran, plus the trained tree itself. Inference always uses
/// what is baked into this structure -- never the modifier's *current* (possibly since
/// edited) layersConfigJson/mappingMode fields -- so that re-configuring the modifier after
/// training cannot silently change what an already-trained model does until the user
/// explicitly retrains.
struct OVITO_PARTICLES_EXPORT TrainedModel {
    std::vector<LayerConfig> layers;
    MappingMode mappingMode = MappingMode::Default;
    /// True if the model was fitted on the selected particles only. Like mappingMode, this
    /// is baked in at training time and drives inference: a model trained on a sub-population
    /// has scaling statistics and cluster boundaries that only describe that sub-population,
    /// so applying it to every atom of a frame would be reading it outside its domain.
    /// The modifier therefore restricts classification to the selection whenever this is set,
    /// regardless of how its onlySelectedParticles() field currently stands.
    bool trainedOnSelection = false;
    SomTreeNode tree;

    QJsonObject toJson() const;
    /// Returns false and fills 'errorMessage' if 'obj' is not a valid trained model document.
    static bool fromJson(const QJsonObject& obj, TrainedModel& out, QString& errorMessage);
};

/// Serializes just a layer-configuration list (no trained weights), for the modifier's
/// user-editable layersConfigJson property field.
OVITO_PARTICLES_EXPORT QJsonArray layersToJson(const std::vector<LayerConfig>& layers);
OVITO_PARTICLES_EXPORT std::vector<LayerConfig> layersFromJson(const QJsonArray& array, QString& errorMessage);

/// Returns the n-th prime, 1-indexed (nthPrime(1) == 2, nthPrime(2) == 3, nthPrime(3) == 5, ...).
/// Cached; layer counts are always small (<10 in practice) so this never gets hot.
OVITO_PARTICLES_EXPORT int64_t nthPrime(int n);

}   // namespace SomAlgorithm

}   // namespace Ovito
