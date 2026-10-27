////////////////////////////////////////////////////////////////////////////////////////
//
//  Copyright 2026 OVITO GmbH, Germany
//
//  This file is part of OVITO (Open Visualization Tool).
//
////////////////////////////////////////////////////////////////////////////////////////

#include <ovito/particles/Particles.h>
#include <ovito/core/utilities/Exception.h>
#include <ovito/core/app/StandaloneApplication.h>
#include "SomAlgorithm.h"
#include "SomSelfTestService.h"

#include <QCommandLineParser>
#include <QCoreApplication>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <map>
#include <random>
#include <thread>

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(SomSelfTestService);

namespace {

/// Accumulates pass/fail results and prints each check as it runs.
class Checker {
public:
    template<typename A, typename B>
    void equal(const QString& label, const A& got, const B& expected) {
        ++_total;
        const bool ok = (got == expected);
        if(!ok) ++_failed;
        qInfo().noquote() << (ok ? "  [PASS] " : "  [FAIL] ") << label
            << "-- got" << QVariant::fromValue(got) << "expected" << QVariant::fromValue(expected);
    }

    void check(const QString& label, bool ok) {
        ++_total;
        if(!ok) ++_failed;
        qInfo().noquote() << (ok ? "  [PASS] " : "  [FAIL] ") << label;
    }

    int total() const { return _total; }
    int failed() const { return _failed; }

private:
    int _total = 0;
    int _failed = 0;
};

/// Test 1: a single-layer SOM trained on two well-separated 2D Gaussian blobs must
/// assign each blob to a single, blob-consistent best-matching unit.
void testSingleLayerSeparation(Checker& c)
{
    std::mt19937 rng(12345);
    std::normal_distribution<double> noise(0.0, 1.0);

    std::vector<SomAlgorithm::TrainingSample> samples;
    const int perBlob = 150;
    for(int i = 0; i < perBlob; ++i) {
        samples.push_back({{static_cast<float>(noise(rng)), static_cast<float>(noise(rng))}});
    }
    for(int i = 0; i < perBlob; ++i) {
        samples.push_back({{static_cast<float>(10.0 + noise(rng)), static_cast<float>(10.0 + noise(rng))}});
    }

    SomAlgorithm::LayerConfig layer;
    layer.featureRefs = {QStringLiteral("f0"), QStringLiteral("f1")};
    layer.scaling = SomAlgorithm::ScalingMethod::Normal;
    layer.numNeurons = 2;
    layer.maxSigma = 0.5;
    layer.maxEta = 0.5;
    layer.sampleFraction = 1.0;
    std::vector<SomAlgorithm::LayerConfig> layers{layer};

    std::map<QString, int> featureIndex{{QStringLiteral("f0"), 0}, {QStringLiteral("f1"), 1}};

    SomAlgorithm::SomTreeNode tree = SomAlgorithm::trainTree(layers, samples, featureIndex, rng);

    auto classifyOf = [&](size_t i) {
        const SomAlgorithm::TrainingSample& s = samples[i];
        auto lookup = [&](size_t layerIdx, int k) -> float {
            return s.allFeatureValues[static_cast<size_t>(featureIndex.at(layers[layerIdx].featureRefs[k]))];
        };
        return SomAlgorithm::classify(tree, layers, lookup, SomAlgorithm::MappingMode::Default);
    };

    std::map<int, int> countsA, countsB;
    for(int i = 0; i < perBlob; ++i)
        ++countsA[classifyOf(static_cast<size_t>(i)).perLayerIds[0]];
    for(int i = 0; i < perBlob; ++i)
        ++countsB[classifyOf(static_cast<size_t>(perBlob + i)).perLayerIds[0]];

    auto majority = [](const std::map<int, int>& counts) {
        return std::max_element(counts.begin(), counts.end(),
            [](const auto& a, const auto& b) { return a.second < b.second; });
    };
    auto majA = majority(countsA);
    auto majB = majority(countsB);

    c.check(QStringLiteral("blob A: >=95% share a common BMU"), majA->second >= static_cast<int>(0.95 * perBlob));
    c.check(QStringLiteral("blob B: >=95% share a common BMU"), majB->second >= static_cast<int>(0.95 * perBlob));
    c.check(QStringLiteral("blob A and blob B map to different BMUs"), majA->first != majB->first);
}

/// Test 2: a two-layer hierarchy built from crisply separated per-layer values, so the
/// correct 2x2 partition is known in advance regardless of which physical neuron index
/// ends up representing which group. Validates tree partitioning/recursion and the
/// Godel/Linear id-mapping formulas.
void testTwoLayerHierarchy(Checker& c)
{
    std::mt19937 rng(54321);

    // Four groups of 30 samples: (g,h) in {-5,+5} x {-5,+5}.
    struct Group { double g, h; std::vector<size_t> indices; };
    std::vector<Group> groups{{-5, -5, {}}, {-5, 5, {}}, {5, -5, {}}, {5, 5, {}}};

    std::vector<SomAlgorithm::TrainingSample> samples;
    const int perGroup = 30;
    for(Group& grp : groups) {
        for(int i = 0; i < perGroup; ++i) {
            grp.indices.push_back(samples.size());
            samples.push_back({{static_cast<float>(grp.g), static_cast<float>(grp.h)}});
        }
    }

    SomAlgorithm::LayerConfig layer0, layer1;
    layer0.featureRefs = {QStringLiteral("g")};
    layer0.scaling = SomAlgorithm::ScalingMethod::Standard;
    layer0.numNeurons = 2;
    layer0.maxSigma = 0.5;
    layer0.maxEta = 0.8;
    layer0.sampleFraction = 1.0;
    layer1 = layer0;
    layer1.featureRefs = {QStringLiteral("h")};
    std::vector<SomAlgorithm::LayerConfig> layers{layer0, layer1};

    std::map<QString, int> featureIndex{{QStringLiteral("g"), 0}, {QStringLiteral("h"), 1}};

    SomAlgorithm::SomTreeNode tree = SomAlgorithm::trainTree(layers, samples, featureIndex, rng);
    c.equal(QStringLiteral("root has 2 non-empty branches"), static_cast<int64_t>(tree.children.size()), int64_t(2));

    auto classifyOf = [&](size_t i, SomAlgorithm::MappingMode mode) {
        const SomAlgorithm::TrainingSample& s = samples[i];
        auto lookup = [&](size_t layerIdx, int k) -> float {
            return s.allFeatureValues[static_cast<size_t>(featureIndex.at(layers[layerIdx].featureRefs[k]))];
        };
        return SomAlgorithm::classify(tree, layers, lookup, mode);
    };

    // Per-group representative pair (local0, local1), and full-group consistency check.
    struct GroupResult { int local0, local1; int64_t godelId; double linearId; };
    std::vector<GroupResult> results;
    bool allConsistent = true;
    for(const Group& grp : groups) {
        SomAlgorithm::ClassificationResult first = classifyOf(grp.indices.front(), SomAlgorithm::MappingMode::Default);
        for(size_t idx : grp.indices) {
            SomAlgorithm::ClassificationResult r = classifyOf(idx, SomAlgorithm::MappingMode::Default);
            if(r.perLayerIds != first.perLayerIds) allConsistent = false;
        }
        SomAlgorithm::ClassificationResult godel = classifyOf(grp.indices.front(), SomAlgorithm::MappingMode::Godel);
        SomAlgorithm::ClassificationResult linear = classifyOf(grp.indices.front(), SomAlgorithm::MappingMode::Linear);
        results.push_back({first.perLayerIds[0], first.perLayerIds[1], godel.intId, linear.floatId});
    }
    c.check(QStringLiteral("every sample in a group maps to the same (layer0, layer1) id pair"), allConsistent);

    bool allDistinctPairs = true;
    for(size_t i = 0; i < results.size(); ++i)
        for(size_t j = i + 1; j < results.size(); ++j)
            if(results[i].local0 == results[j].local0 && results[i].local1 == results[j].local1)
                allDistinctPairs = false;
    c.check(QStringLiteral("the 4 groups map to 4 distinct (layer0, layer1) id pairs"), allDistinctPairs);

    // Recompute the expected Godel id directly from the documented formula:
    // newId = prevId * nthPrime(layer) ^ (localId+1), applied once per layer, root prevId=1.
    bool godelMatchesFormula = true;
    for(const GroupResult& r : results) {
        int64_t expectedId = 1;
        int64_t p0 = SomAlgorithm::nthPrime(1), p1 = SomAlgorithm::nthPrime(2);
        for(int k = 0; k < r.local0 + 1; ++k) expectedId *= p0;
        for(int k = 0; k < r.local1 + 1; ++k) expectedId *= p1;
        if(expectedId != r.godelId) godelMatchesFormula = false;
    }
    c.check(QStringLiteral("Godel ids match the documented prime-power formula"), godelMatchesFormula);

    bool godelDistinct = true;
    for(size_t i = 0; i < results.size(); ++i)
        for(size_t j = i + 1; j < results.size(); ++j)
            if(results[i].godelId == results[j].godelId) godelDistinct = false;
    c.check(QStringLiteral("Godel ids are pairwise distinct across the 4 groups"), godelDistinct);

    // classify() runs inside parallelFor(), so nthPrime() must be safe to call concurrently.
    {
        const int64_t expectedPrimes[] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29};
        std::atomic<bool> primesOk{true};
        std::vector<std::thread> threads;
        for(int t = 0; t < 8; ++t) {
            threads.emplace_back([&]() {
                for(int rep = 0; rep < 1000; ++rep)
                    for(int n = 1; n <= 10; ++n)
                        if(SomAlgorithm::nthPrime(n) != expectedPrimes[n - 1]) primesOk = false;
            });
        }
        for(std::thread& th : threads) th.join();
        c.check(QStringLiteral("nthPrime() returns the right primes when called from 8 threads at once"), primesOk.load());
    }

    bool linearInRange = true;
    for(const GroupResult& r : results)
        if(r.linearId <= 0.0 || r.linearId >= 1.0) linearInRange = false;
    c.check(QStringLiteral("Linear ids lie in the open interval (0,1)"), linearInRange);

    // Groups sharing the same layer0 branch must occupy a sub-interval that does not
    // overlap with the other branch's sub-interval (interval-narrowing guarantee).
    std::vector<double> branchLo(2, 1.0), branchHi(2, 0.0);
    std::map<int, int> local0ToSlot;
    for(const GroupResult& r : results) {
        auto it = local0ToSlot.find(r.local0);
        int slot = (it != local0ToSlot.end()) ? it->second : static_cast<int>(local0ToSlot.size());
        if(it == local0ToSlot.end()) local0ToSlot.emplace(r.local0, slot);
        branchLo[static_cast<size_t>(slot)] = std::min(branchLo[static_cast<size_t>(slot)], r.linearId);
        branchHi[static_cast<size_t>(slot)] = std::max(branchHi[static_cast<size_t>(slot)], r.linearId);
    }
    const bool nonOverlapping = (branchHi[0] < branchLo[1]) || (branchHi[1] < branchLo[0]);
    c.check(QStringLiteral("Linear sub-intervals of the two layer0 branches do not overlap"), nonOverlapping);
}

/// Test 3: a non-finite (NaN/inf) feature value must neither poison training nor be
/// silently assigned to neuron 0 at inference time. Before the guard, a single NaN sample
/// turned every weight of the layer into NaN -- after which every atom's BMU degenerated
/// to neuron 0 -- while the model still serialized as well-formed.
void testNonFiniteRobustness(Checker& c)
{
    std::mt19937 rng(777);
    std::normal_distribution<double> noise(0.0, 1.0);

    // Same two well-separated blobs as test 1, with a handful of poisoned samples mixed in.
    std::vector<SomAlgorithm::TrainingSample> samples;
    const int perBlob = 150;
    for(int i = 0; i < perBlob; ++i)
        samples.push_back({{static_cast<float>(noise(rng)), static_cast<float>(noise(rng))}});
    for(int i = 0; i < perBlob; ++i)
        samples.push_back({{static_cast<float>(10.0 + noise(rng)), static_cast<float>(10.0 + noise(rng))}});
    const size_t cleanCount = samples.size();
    samples.push_back({{std::numeric_limits<float>::quiet_NaN(), 0.0f}});
    samples.push_back({{5.0f, std::numeric_limits<float>::quiet_NaN()}});
    samples.push_back({{std::numeric_limits<float>::infinity(), 1.0f}});

    SomAlgorithm::LayerConfig layer;
    layer.featureRefs = {QStringLiteral("f0"), QStringLiteral("f1")};
    layer.scaling = SomAlgorithm::ScalingMethod::Normal;
    layer.numNeurons = 2;
    layer.maxSigma = 0.5;
    layer.maxEta = 0.5;
    layer.sampleFraction = 1.0;
    std::vector<SomAlgorithm::LayerConfig> layers{layer};
    std::map<QString, int> featureIndex{{QStringLiteral("f0"), 0}, {QStringLiteral("f1"), 1}};

    SomAlgorithm::TrainingReport report;
    SomAlgorithm::SomTreeNode tree = SomAlgorithm::trainTree(layers, samples, featureIndex, rng, {}, &report);

    c.equal(QStringLiteral("training reports the 3 non-finite samples as skipped"),
        static_cast<int64_t>(report.skippedNonFinite), int64_t(3));
    c.equal(QStringLiteral("training reports the full sample count"),
        static_cast<int64_t>(report.samplesTotal), static_cast<int64_t>(samples.size()));

    bool weightsFinite = true;
    for(float w : tree.som.weights)
        if(!std::isfinite(w)) weightsFinite = false;
    c.check(QStringLiteral("no NaN/inf leaked into the trained weights"), weightsFinite);

    bool scalingFinite = true;
    for(const SomAlgorithm::FeatureScaling& fs : tree.som.scaling)
        if(!std::isfinite(fs.stat1) || !std::isfinite(fs.stat2)) scalingFinite = false;
    c.check(QStringLiteral("the non-finite samples stayed out of the scaling statistics"), scalingFinite);

    auto classifyOf = [&](size_t i) {
        const SomAlgorithm::TrainingSample& s = samples[i];
        auto lookup = [&](size_t layerIdx, int k) -> float {
            return s.allFeatureValues[static_cast<size_t>(featureIndex.at(layers[layerIdx].featureRefs[k]))];
        };
        return SomAlgorithm::classify(tree, layers, lookup, SomAlgorithm::MappingMode::Default);
    };

    // The clean blobs must still separate, exactly as in test 1.
    std::map<int, int> countsA, countsB;
    for(int i = 0; i < perBlob; ++i)
        ++countsA[classifyOf(static_cast<size_t>(i)).perLayerIds[0]];
    for(int i = 0; i < perBlob; ++i)
        ++countsB[classifyOf(static_cast<size_t>(perBlob + i)).perLayerIds[0]];
    auto majority = [](const std::map<int, int>& counts) {
        return std::max_element(counts.begin(), counts.end(),
            [](const auto& a, const auto& b) { return a.second < b.second; });
    };
    c.check(QStringLiteral("the two clean blobs still map to different BMUs despite the NaN samples"),
        majority(countsA)->first != majority(countsB)->first);

    // And the poisoned samples must be flagged, not quietly filed under neuron 0.
    bool allFlagged = true, allSentinel = true, noneFlagged = true;
    for(size_t i = cleanCount; i < samples.size(); ++i) {
        const SomAlgorithm::ClassificationResult r = classifyOf(i);
        if(!r.invalidFeatures) allFlagged = false;
        if(r.perLayerIds[0] != -1 || r.intId != 0 || r.floatId != 0.0) allSentinel = false;
    }
    for(size_t i = 0; i < cleanCount; ++i)
        if(classifyOf(i).invalidFeatures) noneFlagged = false;
    c.check(QStringLiteral("every non-finite feature vector is flagged as invalid"), allFlagged);
    c.check(QStringLiteral("invalid atoms get the sentinel ids (-1 per layer, intId 0, floatId 0)"), allSentinel);
    c.check(QStringLiteral("no clean atom is flagged as invalid"), noneFlagged);
}

/// Test 4: a trained model must survive the JSON round-trip it goes through every time the
/// scene is saved and reloaded -- weights, scaling statistics, tree shape, mapping mode and
/// the trained-on-selection flag alike. The flag matters as much as the weights: were it to
/// be dropped in transit, a model fitted on a sub-population (e.g. Wigner-Seitz candidates
/// only) would silently be applied to every atom in the frame, i.e. read far outside the
/// domain its scaling statistics describe.
void testModelRoundTrip(Checker& c)
{
    std::mt19937 rng(31337);
    std::normal_distribution<double> noise(0.0, 1.0);

    std::vector<SomAlgorithm::TrainingSample> samples;
    for(int i = 0; i < 120; ++i)
        samples.push_back({{static_cast<float>(noise(rng)), static_cast<float>(noise(rng))}});
    for(int i = 0; i < 120; ++i)
        samples.push_back({{static_cast<float>(8.0 + noise(rng)), static_cast<float>(noise(rng))}});

    SomAlgorithm::LayerConfig layer0;
    layer0.featureRefs = {QStringLiteral("f0"), QStringLiteral("f1")};
    layer0.scaling = SomAlgorithm::ScalingMethod::Robust;
    layer0.numNeurons = 3;
    layer0.maxSigma = 0.4;
    layer0.maxEta = 0.6;
    layer0.sampleFraction = 0.8;
    SomAlgorithm::LayerConfig layer1 = layer0;
    layer1.scaling = SomAlgorithm::ScalingMethod::Standard;
    layer1.numNeurons = 2;

    SomAlgorithm::TrainedModel model;
    model.layers = {layer0, layer1};
    model.mappingMode = SomAlgorithm::MappingMode::Godel;
    model.trainedOnSelection = true;
    std::map<QString, int> featureIndex{{QStringLiteral("f0"), 0}, {QStringLiteral("f1"), 1}};
    model.tree = SomAlgorithm::trainTree(model.layers, samples, featureIndex, rng);

    SomAlgorithm::TrainedModel restored;
    QString err;
    const bool parsed = SomAlgorithm::TrainedModel::fromJson(model.toJson(), restored, err);
    c.check(QStringLiteral("a trained model deserializes from its own JSON"), parsed);
    if(!parsed) {
        qInfo().noquote() << "    reason:" << err;
        return;
    }

    c.check(QStringLiteral("the trained-on-selection flag survives the JSON round-trip"),
        restored.trainedOnSelection == model.trainedOnSelection);
    c.check(QStringLiteral("the mapping mode survives the JSON round-trip"),
        restored.mappingMode == model.mappingMode);
    c.equal(QStringLiteral("the layer count survives the JSON round-trip"),
        static_cast<int64_t>(restored.layers.size()), static_cast<int64_t>(model.layers.size()));
    c.equal(QStringLiteral("the root's branch count survives the JSON round-trip"),
        static_cast<int64_t>(restored.tree.children.size()), static_cast<int64_t>(model.tree.children.size()));

    // The only guarantee that actually matters: identical predictions.
    bool sameIds = true;
    for(const SomAlgorithm::TrainingSample& sample : samples) {
        auto lookup = [&](size_t layerIdx, int k) -> float {
            return sample.allFeatureValues[static_cast<size_t>(featureIndex.at(model.layers[layerIdx].featureRefs[k]))];
        };
        const auto a = SomAlgorithm::classify(model.tree, model.layers, lookup, model.mappingMode);
        const auto b = SomAlgorithm::classify(restored.tree, restored.layers, lookup, restored.mappingMode);
        if(a.intId != b.intId || a.perLayerIds != b.perLayerIds) sameIds = false;
    }
    c.check(QStringLiteral("the restored model classifies every sample identically"), sameIds);

    // A model whose flag is off must not come back on (JSON default coverage).
    model.trainedOnSelection = false;
    SomAlgorithm::TrainedModel restoredOff;
    SomAlgorithm::TrainedModel::fromJson(model.toJson(), restoredOff, err);
    c.check(QStringLiteral("a model trained without a selection stays that way"),
        restoredOff.trainedOnSelection == false);
}

/// Test 5: the three scaling methods must reproduce the statistics the MultiSOM-main
/// reference computes with pandas, on a hand-checkable column. In particular 'standard' uses
/// the SAMPLE standard deviation (ddof = 1), like pandas' Series.std() -- the population form
/// differs by 12% on a column of five values, which is the size a deep layer's branch has.
void testScalingStatistics(Checker& c)
{
    // Column: 1, 2, 3, 4, 10.  mean = 4, ddof1 std = sqrt(12.5) = 3.5355 (ddof0 would be
    // sqrt(10) = 3.1623), median = 3, q25 = 2, q75 = 4, IQR = 2, min = 1, max = 10.
    const std::vector<float> column{1.0f, 2.0f, 3.0f, 4.0f, 10.0f};

    auto statsFor = [&](SomAlgorithm::ScalingMethod method) {
        std::vector<SomAlgorithm::TrainingSample> samples;
        for(float v : column)
            samples.push_back({{v}});
        SomAlgorithm::LayerConfig layer;
        layer.featureRefs = {QStringLiteral("f0")};
        layer.scaling = method;
        layer.numNeurons = 1;
        layer.maxSigma = 0.5;
        layer.maxEta = 0.5;
        layer.sampleFraction = 1.0;
        std::vector<SomAlgorithm::LayerConfig> layers{layer};
        std::map<QString, int> featureIndex{{QStringLiteral("f0"), 0}};
        std::mt19937 rng(2024);
        return SomAlgorithm::trainTree(layers, samples, featureIndex, rng).som.scaling[0];
    };

    auto near = [](double got, double expected) { return std::abs(got - expected) < 1e-6; };

    const SomAlgorithm::FeatureScaling normal = statsFor(SomAlgorithm::ScalingMethod::Normal);
    c.check(QStringLiteral("normal scaling stores (min, max) = (1, 10)"),
        near(normal.stat1, 1.0) && near(normal.stat2, 10.0));

    const SomAlgorithm::FeatureScaling standard = statsFor(SomAlgorithm::ScalingMethod::Standard);
    c.check(QStringLiteral("standard scaling stores mean = 4"), near(standard.stat1, 4.0));
    c.check(QStringLiteral("standard scaling uses the sample std (ddof=1), as pandas does"),
        near(standard.stat2, std::sqrt(12.5)));
    c.check(QStringLiteral("standard scaling is NOT the population std (ddof=0)"),
        !near(standard.stat2, std::sqrt(10.0)));

    const SomAlgorithm::FeatureScaling robust = statsFor(SomAlgorithm::ScalingMethod::Robust);
    c.check(QStringLiteral("robust scaling stores (median, IQR) = (3, 2)"),
        near(robust.stat1, 3.0) && near(robust.stat2, 2.0));
}

/// Test 6: the same seed must give the same model, bit for bit. A SOM starts from random
/// weights, so without this the partition changes on every click and no reported clustering
/// can be reproduced -- by a reviewer or by its own author.
void testSeedReproducibility(Checker& c)
{
    auto trainWithSeed = [](unsigned int seed) {
        std::mt19937 rng(seed);
        std::mt19937 dataRng(99);
        std::normal_distribution<double> noise(0.0, 1.0);
        std::vector<SomAlgorithm::TrainingSample> samples;
        for(int i = 0; i < 200; ++i)
            samples.push_back({{static_cast<float>(noise(dataRng)), static_cast<float>(noise(dataRng))}});

        SomAlgorithm::LayerConfig layer;
        layer.featureRefs = {QStringLiteral("f0"), QStringLiteral("f1")};
        layer.scaling = SomAlgorithm::ScalingMethod::Normal;
        layer.numNeurons = 4;
        layer.maxSigma = 0.5;
        layer.maxEta = 0.5;
        layer.sampleFraction = 0.7;   // Also exercises the seeded subsampling, not just the init.
        std::vector<SomAlgorithm::LayerConfig> layers{layer};
        std::map<QString, int> featureIndex{{QStringLiteral("f0"), 0}, {QStringLiteral("f1"), 1}};
        return SomAlgorithm::trainTree(layers, samples, featureIndex, rng).som.weights;
    };

    const std::vector<float> a = trainWithSeed(0);
    const std::vector<float> b = trainWithSeed(0);
    const std::vector<float> d = trainWithSeed(1);
    c.check(QStringLiteral("the same seed reproduces the trained weights exactly"), a == b);
    c.check(QStringLiteral("a different seed gives a different model"), a != d);
}

}   // End of anonymous namespace

/******************************************************************************
* Registers the command-line options handled by this service.
******************************************************************************/
void SomSelfTestService::registerCommandLineOptions(QCommandLineParser& cmdLineParser)
{
    cmdLineParser.addOption(QCommandLineOption(QStringList{QStringLiteral("som-selftest")},
        tr("Run the in-process regression test for the SOM clustering algorithm and exit "
           "(0 on success, 1 on any failed check).")));
}

/******************************************************************************
* Runs the self-test after the application has entered the main event loop.
******************************************************************************/
void SomSelfTestService::applicationStarted()
{
    const QCommandLineParser& parser = StandaloneApplication::instance()->cmdLineParser();
    if(!parser.isSet(QStringLiteral("som-selftest")))
        return;

    bool ok = false;
    try {
        ok = runSelfTest();
    }
    catch(const Exception& ex) {
        qCritical().noquote() << "SOM self-test: FAILED with an exception:";
        for(const QString& msg : ex.messages())
            qCritical().noquote() << msg;
        ok = false;
    }
    catch(const std::exception& ex) {
        qCritical().noquote() << "SOM self-test: FAILED with an exception:" << QString::fromLocal8Bit(ex.what());
        ok = false;
    }
    // In --nogui mode the Qt event loop is not running at this point, so QCoreApplication::exit()
    // has no effect and the process exits with 0 regardless. Throwing makes the startup code call
    // exitWithFatalError(), which shuts the application down and yields exit code 1.
    if(!ok)
        throw Exception(tr("SOM self-test failed."));
    QCoreApplication::exit(0);
}

/******************************************************************************
* Runs every check.
******************************************************************************/
bool SomSelfTestService::runSelfTest()
{
    Checker c;
    qInfo().noquote() << "SOM self-test: running checks...";

    testSingleLayerSeparation(c);
    testTwoLayerHierarchy(c);
    testNonFiniteRobustness(c);
    testModelRoundTrip(c);
    testScalingStatistics(c);
    testSeedReproducibility(c);

    qInfo().noquote() << QStringLiteral("SOM self-test: %1/%2 checks passed.").arg(c.total() - c.failed()).arg(c.total());
    return c.failed() == 0;
}

}   // End of namespace
