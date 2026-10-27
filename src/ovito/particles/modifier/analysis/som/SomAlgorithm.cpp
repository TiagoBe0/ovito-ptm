////////////////////////////////////////////////////////////////////////////////////////
//
//  Copyright 2026 OVITO GmbH, Germany
//
//  This file is part of OVITO (Open Visualization Tool).
//
////////////////////////////////////////////////////////////////////////////////////////

#include "SomAlgorithm.h"

#include <ovito/core/utilities/Exception.h>

#include <QJsonValue>

#include <algorithm>
#include <cmath>
#include <limits>

namespace Ovito {
namespace SomAlgorithm {

namespace {

/// Computes the Euclidean distance between an already-scaled feature vector and one
/// neuron's weight column (row 'neuron' of the flattened, row-major weight matrix).
float neuronDistance(const std::vector<float>& x, const std::vector<float>& weights, int neuron, int inputSize)
{
    double sumSq = 0.0;
    const float* w = weights.data() + static_cast<size_t>(neuron) * static_cast<size_t>(inputSize);
    for(int k = 0; k < inputSize; ++k) {
        const double d = static_cast<double>(x[k]) - static_cast<double>(w[k]);
        sumSq += d * d;
    }
    return static_cast<float>(std::sqrt(sumSq));
}

} // anonymous namespace

QString scalingMethodToString(ScalingMethod method)
{
    switch(method) {
        case ScalingMethod::Normal: return QStringLiteral("normal");
        case ScalingMethod::Standard: return QStringLiteral("standard");
        case ScalingMethod::Robust: return QStringLiteral("robust");
    }
    return QStringLiteral("normal");
}

ScalingMethod scalingMethodFromString(const QString& s, bool* ok)
{
    if(ok) *ok = true;
    if(s == QStringLiteral("normal")) return ScalingMethod::Normal;
    if(s == QStringLiteral("standard")) return ScalingMethod::Standard;
    if(s == QStringLiteral("robust")) return ScalingMethod::Robust;
    if(ok) *ok = false;
    return ScalingMethod::Normal;
}

QString mappingModeToString(MappingMode mode)
{
    switch(mode) {
        case MappingMode::Default: return QStringLiteral("default");
        case MappingMode::Godel: return QStringLiteral("godel");
        case MappingMode::Linear: return QStringLiteral("linear");
    }
    return QStringLiteral("default");
}

MappingMode mappingModeFromString(const QString& s, bool* ok)
{
    if(ok) *ok = true;
    if(s == QStringLiteral("default")) return MappingMode::Default;
    if(s == QStringLiteral("godel")) return MappingMode::Godel;
    if(s == QStringLiteral("linear")) return MappingMode::Linear;
    if(ok) *ok = false;
    return MappingMode::Default;
}

double FeatureScaling::apply(double raw) const
{
    switch(method) {
        case ScalingMethod::Normal: {
            double delta = stat2 - stat1;
            if(delta == 0.0) delta = 1.0;
            return (raw - stat1) / delta;
        }
        case ScalingMethod::Standard: {
            double std_ = stat2;
            if(std_ == 0.0) std_ = 1.0;
            return (raw - stat1) / std_;
        }
        case ScalingMethod::Robust: {
            double iqr = stat2;
            if(iqr == 0.0) iqr = 1.0;
            return (raw - stat1) / iqr;
        }
    }
    return raw;
}

QJsonObject FeatureScaling::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("method")] = scalingMethodToString(method);
    obj[QStringLiteral("stat1")] = stat1;
    obj[QStringLiteral("stat2")] = stat2;
    return obj;
}

FeatureScaling FeatureScaling::fromJson(const QJsonObject& obj)
{
    FeatureScaling fs;
    fs.method = scalingMethodFromString(obj.value(QStringLiteral("method")).toString());
    fs.stat1 = obj.value(QStringLiteral("stat1")).toDouble();
    fs.stat2 = obj.value(QStringLiteral("stat2")).toDouble(1.0);
    return fs;
}

QJsonObject LayerConfig::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("featureRefs")] = QJsonArray::fromStringList(featureRefs);
    obj[QStringLiteral("scaling")] = scalingMethodToString(scaling);
    obj[QStringLiteral("numNeurons")] = numNeurons;
    obj[QStringLiteral("maxSigma")] = maxSigma;
    obj[QStringLiteral("maxEta")] = maxEta;
    obj[QStringLiteral("sampleFraction")] = sampleFraction;
    return obj;
}

LayerConfig LayerConfig::fromJson(const QJsonObject& obj)
{
    LayerConfig cfg;
    for(const QJsonValue& v : obj.value(QStringLiteral("featureRefs")).toArray())
        cfg.featureRefs.push_back(v.toString());
    cfg.scaling = scalingMethodFromString(obj.value(QStringLiteral("scaling")).toString());
    cfg.numNeurons = obj.value(QStringLiteral("numNeurons")).toInt(4);
    cfg.maxSigma = obj.value(QStringLiteral("maxSigma")).toDouble(0.5);
    cfg.maxEta = obj.value(QStringLiteral("maxEta")).toDouble(0.5);
    cfg.sampleFraction = obj.value(QStringLiteral("sampleFraction")).toDouble(1.0);
    return cfg;
}

std::vector<float> SomNode::scaleFeatures(const std::vector<float>& raw) const
{
    std::vector<float> out(raw.size());
    for(size_t k = 0; k < raw.size(); ++k)
        out[k] = static_cast<float>(scaling[k].apply(raw[k]));
    return out;
}

int SomNode::bmu(const std::vector<float>& scaledFeatureVec) const
{
    int best = 0;
    float bestDist = std::numeric_limits<float>::max();
    for(int m = 0; m < numNeurons; ++m) {
        const float d = neuronDistance(scaledFeatureVec, weights, m, inputSize);
        if(d < bestDist) {
            bestDist = d;
            best = m;
        }
    }
    return best;
}

QJsonObject SomNode::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("inputSize")] = inputSize;
    obj[QStringLiteral("numNeurons")] = numNeurons;
    QJsonArray w;
    for(float v : weights)
        w.push_back(static_cast<double>(v));
    obj[QStringLiteral("weights")] = w;
    QJsonArray sc;
    for(const FeatureScaling& fs : scaling)
        sc.push_back(fs.toJson());
    obj[QStringLiteral("scaling")] = sc;
    return obj;
}

SomNode SomNode::fromJson(const QJsonObject& obj)
{
    SomNode node;
    node.inputSize = obj.value(QStringLiteral("inputSize")).toInt();
    node.numNeurons = obj.value(QStringLiteral("numNeurons")).toInt();
    for(const QJsonValue& v : obj.value(QStringLiteral("weights")).toArray())
        node.weights.push_back(static_cast<float>(v.toDouble()));
    for(const QJsonValue& v : obj.value(QStringLiteral("scaling")).toArray())
        node.scaling.push_back(FeatureScaling::fromJson(v.toObject()));
    return node;
}

QJsonObject SomTreeNode::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("som")] = som.toJson();
    QJsonObject childrenObj;
    for(const auto& [localId, child] : children)
        childrenObj[QString::number(localId)] = child.toJson();
    obj[QStringLiteral("children")] = childrenObj;
    return obj;
}

SomTreeNode SomTreeNode::fromJson(const QJsonObject& obj)
{
    SomTreeNode node;
    node.som = SomNode::fromJson(obj.value(QStringLiteral("som")).toObject());
    const QJsonObject childrenObj = obj.value(QStringLiteral("children")).toObject();
    for(auto it = childrenObj.constBegin(); it != childrenObj.constEnd(); ++it)
        node.children.emplace(it.key().toInt(), SomTreeNode::fromJson(it.value().toObject()));
    return node;
}

namespace {

/// Recursive worker for trainTree(). 'sampleIndices' is the set of samples (indices into
/// the global 'samples' array) belonging to this branch.
SomTreeNode trainSubtree(
    const std::vector<LayerConfig>& layers,
    size_t layerIdx,
    const std::vector<size_t>& sampleIndices,
    const std::vector<TrainingSample>& samples,
    const std::map<QString, int>& featureIndex,
    std::mt19937& rng,
    const std::function<void(const QString&)>& progressText,
    TrainingReport* report)
{
    const LayerConfig& cfg = layers[layerIdx];
    if(cfg.featureRefs.isEmpty())
        throw Exception(QStringLiteral("SOM layer has no input features configured."));

    // Resolve this layer's feature columns.
    std::vector<int> colIdx;
    colIdx.reserve(cfg.featureRefs.size());
    for(const QString& ref : cfg.featureRefs) {
        auto it = featureIndex.find(ref);
        if(it == featureIndex.end())
            throw Exception(QStringLiteral("SOM layer references an unavailable feature: %1").arg(ref));
        colIdx.push_back(it->second);
    }
    const int inputSize = static_cast<int>(colIdx.size());

    // Drop every sample carrying a non-finite (NaN/inf) value in one of THIS layer's
    // features. Letting a single one through would poison the whole layer: its distances
    // are all NaN, so h = exp(-NaN) = NaN and the weight update writes NaN into every
    // neuron, permanently. From that point on bmu() returns neuron 0 for every atom
    // (a NaN comparison is always false), so the layer degenerates into one cluster while
    // still serializing as a perfectly well-formed model. Dropped samples are also kept
    // out of the scaling statistics and out of the partition into child branches -- an
    // atom with no usable feature vector has no meaningful branch to belong to.
    std::vector<size_t> finiteIndices;
    finiteIndices.reserve(sampleIndices.size());
    for(size_t idx : sampleIndices) {
        bool usable = true;
        for(int k = 0; k < inputSize && usable; ++k)
            usable = std::isfinite(samples[idx].allFeatureValues[static_cast<size_t>(colIdx[k])]);
        if(usable)
            finiteIndices.push_back(idx);
        else if(report)
            ++report->skippedNonFinite;
    }

    if(progressText)
        progressText(QStringLiteral("Training SOM layer %1 (%2 atoms)...").arg(layerIdx + 1).arg(finiteIndices.size()));

    SomTreeNode node;
    node.som.inputSize = inputSize;
    node.som.numNeurons = std::max(1, cfg.numNeurons);
    node.som.scaling.resize(inputSize);

    // Compute per-feature scaling statistics from the full usable population of this branch
    // (every atom belonging to it that survived the non-finite filter above), independent
    // of the training-sample fraction below --
    // this matches the MultiSOM-main reference, where normalize_data() is always called
    // on the complete branch data before sample(frac=f) subsamples for the online
    // training walk.
    for(int k = 0; k < inputSize; ++k) {
        std::vector<double> col;
        col.reserve(sampleIndices.size());
        for(size_t idx : finiteIndices)
            col.push_back(static_cast<double>(samples[idx].allFeatureValues[static_cast<size_t>(colIdx[k])]));

        FeatureScaling fs;
        fs.method = cfg.scaling;
        if(col.empty()) {
            node.som.scaling[k] = fs;
            continue;
        }
        if(cfg.scaling == ScalingMethod::Normal) {
            const auto [minIt, maxIt] = std::minmax_element(col.begin(), col.end());
            fs.stat1 = *minIt;
            fs.stat2 = *maxIt;
        }
        else if(cfg.scaling == ScalingMethod::Standard) {
            double sum = 0.0;
            for(double v : col) sum += v;
            const double mean = sum / static_cast<double>(col.size());
            double sqSum = 0.0;
            for(double v : col) sqSum += (v - mean) * (v - mean);
            // Sample standard deviation (ddof = 1), matching pandas' Series.std() as used by
            // the MultiSOM-main reference. The difference from the population form is
            // negligible for the root layer but reaches ~12% for a branch of 5 atoms, which
            // is exactly the size deep layers of a hierarchy end up with. A single-sample
            // branch has no spread to speak of; 0 here is turned into a scale of 1 by
            // FeatureScaling::apply(), which is the reference's behaviour too.
            const double std_ = col.size() > 1
                ? std::sqrt(sqSum / static_cast<double>(col.size() - 1))
                : 0.0;
            fs.stat1 = mean;
            fs.stat2 = std_;
        }
        else { // Robust
            std::vector<double> sorted = col;
            std::sort(sorted.begin(), sorted.end());
            auto quantile = [&](double q) -> double {
                const double pos = q * static_cast<double>(sorted.size() - 1);
                const size_t lo = static_cast<size_t>(std::floor(pos));
                const size_t hi = static_cast<size_t>(std::ceil(pos));
                if(lo == hi) return sorted[lo];
                const double frac = pos - static_cast<double>(lo);
                return sorted[lo] * (1.0 - frac) + sorted[hi] * frac;
            };
            const double median = quantile(0.5);
            const double iqr = quantile(0.75) - quantile(0.25);
            fs.stat1 = median;
            fs.stat2 = iqr;
        }
        node.som.scaling[k] = fs;
    }

    // Random weight initialization, uniform in [0,1) -- matches SOM.py's np.random.rand().
    node.som.weights.resize(static_cast<size_t>(node.som.numNeurons) * static_cast<size_t>(inputSize));
    {
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        for(float& w : node.som.weights)
            w = dist(rng);
    }

    // Select the training subsample (fraction f of this branch's population, shuffled --
    // matches og_training_df.sample(frac=f)), then run serial online training.
    if(!finiteIndices.empty() && inputSize > 0) {
        std::vector<size_t> trainOrder = finiteIndices;
        std::shuffle(trainOrder.begin(), trainOrder.end(), rng);
        const double frac = std::clamp(cfg.sampleFraction, 1e-6, 1.0);
        size_t trainCount = static_cast<size_t>(std::llround(frac * static_cast<double>(trainOrder.size())));
        trainCount = std::clamp(trainCount, size_t(1), trainOrder.size());
        trainOrder.resize(trainCount);

        std::vector<float> D(static_cast<size_t>(node.som.numNeurons));
        std::vector<float> raw(static_cast<size_t>(inputSize));
        for(size_t t = 0; t < trainOrder.size(); ++t) {
            const TrainingSample& sample = samples[trainOrder[t]];
            for(int k = 0; k < inputSize; ++k)
                raw[static_cast<size_t>(k)] = sample.allFeatureValues[static_cast<size_t>(colIdx[k])];
            const std::vector<float> x = node.som.scaleFeatures(raw);

            int dMin = 0;
            for(int m = 0; m < node.som.numNeurons; ++m) {
                D[static_cast<size_t>(m)] = neuronDistance(x, node.som.weights, m, inputSize);
                if(D[static_cast<size_t>(m)] < D[static_cast<size_t>(dMin)])
                    dMin = m;
            }

            // t is 1-indexed in the reference formulas.
            const double tStep = static_cast<double>(t + 1);
            const double sigma = std::max(cfg.maxSigma, 1e-9) / tStep;
            const double eta = cfg.maxEta / tStep;
            const double denom = 2.0 * sigma * sigma;

            for(int m = 0; m < node.som.numNeurons; ++m) {
                const double diff = static_cast<double>(D[static_cast<size_t>(m)]) - static_cast<double>(D[static_cast<size_t>(dMin)]);
                const double h = std::exp(-(diff * diff) / denom);
                const double factor = eta * h;
                float* wRow = node.som.weights.data() + static_cast<size_t>(m) * static_cast<size_t>(inputSize);
                for(int k = 0; k < inputSize; ++k)
                    wRow[k] = static_cast<float>(static_cast<double>(wRow[k]) + factor * (static_cast<double>(x[static_cast<size_t>(k)]) - static_cast<double>(wRow[k])));
            }
        }
    }

    // Assign every atom of this branch (not just the training subsample) to its BMU, to
    // partition the data for the next configured layer -- matches predicting on og_df.
    if(layerIdx + 1 < layers.size() && inputSize > 0) {
        std::map<int, std::vector<size_t>> buckets;
        std::vector<float> raw(static_cast<size_t>(inputSize));
        for(size_t idx : finiteIndices) {
            const TrainingSample& sample = samples[idx];
            for(int k = 0; k < inputSize; ++k)
                raw[static_cast<size_t>(k)] = sample.allFeatureValues[static_cast<size_t>(colIdx[k])];
            const std::vector<float> x = node.som.scaleFeatures(raw);
            const int group = node.som.bmu(x);
            buckets[group].push_back(idx);
        }
        for(auto& [group, indices] : buckets) {
            if(indices.empty())
                continue;
            node.children.emplace(group, trainSubtree(layers, layerIdx + 1, indices, samples, featureIndex, rng, progressText, report));
        }
    }

    return node;
}

} // anonymous namespace

SomTreeNode trainTree(
    const std::vector<LayerConfig>& layers,
    const std::vector<TrainingSample>& samples,
    const std::map<QString, int>& featureIndex,
    std::mt19937& rng,
    const std::function<void(const QString&)>& progressText,
    TrainingReport* report)
{
    if(layers.empty())
        throw Exception(QStringLiteral("No SOM layers configured."));

    std::vector<size_t> allIndices(samples.size());
    for(size_t i = 0; i < samples.size(); ++i)
        allIndices[i] = i;

    if(report) {
        report->samplesTotal = samples.size();
        report->skippedNonFinite = 0;
    }

    return trainSubtree(layers, 0, allIndices, samples, featureIndex, rng, progressText, report);
}

int64_t nthPrime(int n)
{
    // Deliberately stateless: classify() calls this from parallelFor() workers, and a shared
    // static cache grown with push_back() was a data race on the first Godel evaluation.
    // n is the layer index, so trial division from scratch costs nothing.
    int count = 0;
    for(int64_t candidate = 2; ; ++candidate) {
        bool isPrime = true;
        for(int64_t d = 2; d * d <= candidate; ++d) {
            if(candidate % d == 0) { isPrime = false; break; }
        }
        if(isPrime && ++count >= n)
            return candidate;
    }
}

ClassificationResult classify(
    const SomTreeNode& root,
    const std::vector<LayerConfig>& layers,
    const std::function<float(size_t layerIndex, int featureIndex)>& featureLookup,
    MappingMode mode)
{
    ClassificationResult result;
    result.perLayerIds.assign(layers.size(), -1);

    int64_t prevId = 1;
    double lo = 0.0, hi = 1.0;

    const SomTreeNode* node = &root;
    for(size_t layerIdx = 0; layerIdx < layers.size(); ++layerIdx) {
        const LayerConfig& cfg = layers[layerIdx];
        if(cfg.featureRefs.size() != node->som.inputSize) {
            throw Exception(QStringLiteral(
                "SOM clustering: the trained model is corrupted (layer %1 expects %2 input feature(s) but its SOM was trained on %3).")
                .arg(layerIdx + 1).arg(cfg.featureRefs.size()).arg(node->som.inputSize));
        }
        std::vector<float> raw(static_cast<size_t>(node->som.inputSize));
        for(int k = 0; k < node->som.inputSize; ++k)
            raw[static_cast<size_t>(k)] = featureLookup(layerIdx, k);
        // A non-finite feature makes every distance NaN, so bmu() would silently return
        // neuron 0 rather than a match. Stop here and report unambiguous sentinel ids
        // instead of a cluster assignment that means nothing.
        for(float v : raw) {
            if(!std::isfinite(v)) {
                result.invalidFeatures = true;
                result.intId = 0;
                result.floatId = 0.0;
                return result;
            }
        }

        const std::vector<float> x = node->som.scaleFeatures(raw);
        const int localId = node->som.bmu(x);
        result.perLayerIds[layerIdx] = localId;

        if(mode == MappingMode::Godel) {
            const int64_t base = nthPrime(static_cast<int>(layerIdx + 1));
            // Overflow guard: bail out (stop encoding further layers into prevId) once the
            // running product would exceed roughly 62 bits. The id computed so far remains
            // valid, just coarser than the full configured depth.
            bool overflow = false;
            int64_t factor = 1;
            for(int p = 0; p < localId + 1; ++p) {
                if(factor > (std::numeric_limits<int64_t>::max)() / std::max<int64_t>(base, 1)) { overflow = true; break; }
                factor *= base;
            }
            if(!overflow && prevId != 0 && factor != 0 &&
               std::abs(prevId) > (std::numeric_limits<int64_t>::max)() / std::abs(factor)) {
                overflow = true;
            }
            if(overflow) {
                result.godelOverflow = true;
                break;
            }
            prevId *= factor;
        }
        else if(mode == MappingMode::Linear) {
            const double N = static_cast<double>(std::max(1, cfg.numNeurons));
            const double childLo = lo + static_cast<double>(localId) / (N + 1.0) * (hi - lo);
            const double childHi = lo + static_cast<double>(localId + 1) / (N + 1.0) * (hi - lo);
            lo = childLo;
            hi = childHi;
        }

        if(layerIdx + 1 == layers.size())
            break; // Reached the deepest configured layer successfully.

        auto it = node->children.find(localId);
        if(it == node->children.end()) {
            result.hitMissingBranch = true;
            break;
        }
        node = &it->second;
    }

    result.intId = prevId;
    result.floatId = hi;
    return result;
}

namespace {

/// Recursively checks that a deserialized tree is self-consistent with the declared layer
/// configuration. Guards against a corrupted or hand-edited trainedModelJson (it is persisted
/// as a plain QString in the .ovito scene XML) causing out-of-bounds vector access in
/// scaleFeatures()/bmu()/neuronDistance() later, at classify() time.
bool validateTreeNode(const SomTreeNode& node, const std::vector<LayerConfig>& layers, size_t layerIdx, QString& errorMessage)
{
    const SomNode& som = node.som;
    const int expectedInputSize = layerIdx < layers.size() ? layers[layerIdx].featureRefs.size() : som.inputSize;
    if(som.inputSize <= 0 || som.numNeurons <= 0) {
        errorMessage = QStringLiteral("The trained SOM model is corrupted (a tree node has a non-positive inputSize or numNeurons).");
        return false;
    }
    if(som.inputSize != expectedInputSize) {
        errorMessage = QStringLiteral("The trained SOM model is corrupted (a tree node's inputSize (%1) does not match its layer's feature count (%2)).")
            .arg(som.inputSize).arg(expectedInputSize);
        return false;
    }
    if(som.weights.size() != static_cast<size_t>(som.numNeurons) * static_cast<size_t>(som.inputSize)) {
        errorMessage = QStringLiteral("The trained SOM model is corrupted (a tree node's weight matrix has the wrong number of entries).");
        return false;
    }
    if(som.scaling.size() != static_cast<size_t>(som.inputSize)) {
        errorMessage = QStringLiteral("The trained SOM model is corrupted (a tree node's scaling statistics array has the wrong size).");
        return false;
    }
    for(const auto& [localId, child] : node.children) {
        if(layerIdx + 1 >= layers.size()) {
            errorMessage = QStringLiteral("The trained SOM model is corrupted (the tree is deeper than the configured layers).");
            return false;
        }
        if(!validateTreeNode(child, layers, layerIdx + 1, errorMessage))
            return false;
    }
    return true;
}

} // anonymous namespace

QJsonObject TrainedModel::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("version")] = 1;
    obj[QStringLiteral("layers")] = layersToJson(layers);
    obj[QStringLiteral("mappingMode")] = mappingModeToString(mappingMode);
    obj[QStringLiteral("trainedOnSelection")] = trainedOnSelection;
    obj[QStringLiteral("tree")] = tree.toJson();
    return obj;
}

bool TrainedModel::fromJson(const QJsonObject& obj, TrainedModel& out, QString& errorMessage)
{
    if(!obj.contains(QStringLiteral("tree")) || !obj.contains(QStringLiteral("layers"))) {
        errorMessage = QStringLiteral("Not a valid trained SOM model (missing 'tree' or 'layers').");
        return false;
    }
    out.layers = layersFromJson(obj.value(QStringLiteral("layers")).toArray(), errorMessage);
    if(out.layers.empty() && !errorMessage.isEmpty())
        return false;
    out.mappingMode = mappingModeFromString(obj.value(QStringLiteral("mappingMode")).toString());
    out.trainedOnSelection = obj.value(QStringLiteral("trainedOnSelection")).toBool(false);
    out.tree = SomTreeNode::fromJson(obj.value(QStringLiteral("tree")).toObject());
    if(!out.layers.empty() && !validateTreeNode(out.tree, out.layers, 0, errorMessage))
        return false;
    return true;
}

QJsonArray layersToJson(const std::vector<LayerConfig>& layers)
{
    QJsonArray arr;
    for(const LayerConfig& cfg : layers)
        arr.push_back(cfg.toJson());
    return arr;
}

std::vector<LayerConfig> layersFromJson(const QJsonArray& array, QString& errorMessage)
{
    std::vector<LayerConfig> layers;
    errorMessage.clear();
    for(const QJsonValue& v : array) {
        if(!v.isObject()) {
            errorMessage = QStringLiteral("SOM layer configuration list contains a malformed (non-object) entry.");
            return {};
        }
        layers.push_back(LayerConfig::fromJson(v.toObject()));
    }
    return layers;
}

} // namespace SomAlgorithm
} // namespace Ovito
