////////////////////////////////////////////////////////////////////////////////////////
//
//  Copyright 2026 OVITO GmbH, Germany
//
//  This file is part of OVITO (Open Visualization Tool).
//
////////////////////////////////////////////////////////////////////////////////////////

#include <ovito/particles/Particles.h>
#include <ovito/particles/objects/Particles.h>
#include <ovito/stdobj/properties/PropertyReference.h>
#include <ovito/stdobj/table/DataTable.h>
#include <ovito/core/utilities/concurrent/ParallelFor.h>
#include <ovito/core/dataset/pipeline/ModificationNode.h>
#include <ovito/core/dataset/data/BufferAccess.h>
#include <ovito/core/utilities/units/UnitsManager.h>
#include "SOMClusteringModifier.h"

#include <QHash>
#include <QJsonDocument>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <limits>
#include <map>
#include <optional>
#include <vector>

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(SOMClusteringModifier);
OVITO_CLASSINFO(SOMClusteringModifier, "DisplayName", "SOM clustering");
OVITO_CLASSINFO(SOMClusteringModifier, "Description", "Unsupervised per-atom clustering with a trained multilayer Self-Organizing Map (Kohonen network).");
OVITO_CLASSINFO(SOMClusteringModifier, "ModifierCategory", "Structure identification");

DEFINE_PROPERTY_FIELD(SOMClusteringModifier, layersConfigJson);
DEFINE_PROPERTY_FIELD(SOMClusteringModifier, mappingMode);
DEFINE_PROPERTY_FIELD(SOMClusteringModifier, onlySelectedParticles);
DEFINE_PROPERTY_FIELD(SOMClusteringModifier, randomSeed);
DEFINE_PROPERTY_FIELD(SOMClusteringModifier, trainedModelJson);
DEFINE_PROPERTY_FIELD(SOMClusteringModifier, outputPropertyBaseName);
DEFINE_PROPERTY_FIELD(SOMClusteringModifier, trainingProvenance);

SET_PROPERTY_FIELD_LABEL(SOMClusteringModifier, mappingMode, "Layer id mapping");
SET_PROPERTY_FIELD_LABEL(SOMClusteringModifier, onlySelectedParticles, "Use only selected particles");
SET_PROPERTY_FIELD_LABEL(SOMClusteringModifier, randomSeed, "Random seed (-1 = non-reproducible)");
SET_PROPERTY_FIELD_LABEL(SOMClusteringModifier, outputPropertyBaseName, "Output property name");

bool SOMClusteringModifier::SOMClusteringModifierClass::isApplicableTo(const DataCollection& input) const
{
    if(const Particles* p = input.getObject<Particles>())
        return p->getProperty(Particles::PositionProperty) != nullptr;
    return false;
}

bool SOMClusteringModifier::resolveFeatureRefs(
    const std::vector<SomAlgorithm::LayerConfig>& layers,
    const Particles* particles,
    QStringList& outRefList,
    std::vector<ConstPropertyPtr>& outRefProps,
    std::vector<int>& outRefComps,
    QString& errorMessage)
{
    QSet<QString> allRefs;
    for(const SomAlgorithm::LayerConfig& layer : layers)
        for(const QString& ref : layer.featureRefs)
            allRefs.insert(ref);

    outRefList = allRefs.values();
    outRefProps.clear();
    outRefComps.clear();
    outRefProps.reserve(outRefList.size());
    outRefComps.reserve(outRefList.size());
    for(const QString& ref : outRefList) {
        PropertyReference pref(ref);
        QString err;
        auto [prop, comp] = pref.findInContainerWithComponent(particles, err, false);
        if(!prop) {
            errorMessage = ref;
            return false;
        }
        outRefProps.push_back(ConstPropertyPtr(prop));
        outRefComps.push_back(comp < 0 ? 0 : comp);
    }
    return true;
}

Future<PipelineFlowState> SOMClusteringModifier::evaluateModifier(
    const ModifierEvaluationRequest& request,
    PipelineFlowState&& state)
{
    Particles* particles = state.expectMutableObject<Particles>();
    particles->verifyIntegrity();

    const QString modelJsonText = trainedModelJson();
    if(modelJsonText.trimmed().isEmpty()) {
        state.setStatus(PipelineStatus(PipelineStatus::Warning,
            tr("No trained SOM model yet. Open this modifier's panel and click \"Train\".")));
        return std::move(state);
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(modelJsonText.toUtf8(), &parseError);
    if(parseError.error != QJsonParseError::NoError || !doc.isObject())
        throw Exception(tr("SOM clustering: failed to parse the trained model data (%1).").arg(parseError.errorString()));

    SomAlgorithm::TrainedModel model;
    QString modelErr;
    if(!SomAlgorithm::TrainedModel::fromJson(doc.object(), model, modelErr))
        throw Exception(tr("SOM clustering: %1").arg(modelErr));
    if(model.layers.empty())
        throw Exception(tr("SOM clustering: the trained model has no layers."));

    // Resolve every distinct feature referenced by any layer against the current pipeline data.
    QStringList refList;
    std::vector<ConstPropertyPtr> refProps;
    std::vector<int> refComps;
    QString missingRef;
    if(!resolveFeatureRefs(model.layers, particles, refList, refProps, refComps, missingRef))
        throw Exception(tr("SOM clustering: input feature '%1' was not found in the pipeline data.").arg(missingRef));

    const Property* positions = particles->expectProperty(Particles::PositionProperty);
    const size_t particleCount = positions->size();

    // A model fitted on a sub-population is only valid on that sub-population, so the flag
    // baked into the model -- not the modifier's current onlySelectedParticles() field --
    // decides this. expectProperty() raises a clear error if the selection is gone, which is
    // the right outcome: silently classifying every atom with a model trained on a selection
    // would quietly answer a different question than the one the user asked.
    ConstPropertyPtr selectionProperty;
    if(model.trainedOnSelection)
        selectionProperty = ConstPropertyPtr(particles->expectProperty(Particles::SelectionProperty));

    return asyncLaunch([
        state = std::move(state),
        particles,
        model = std::move(model),
        refList = std::move(refList),
        refProps = std::move(refProps),
        refComps = std::move(refComps),
        selectionProperty = std::move(selectionProperty),
        outputBaseName = outputPropertyBaseName(),
        particleCount,
        createdByNode = request.modificationNodeWeak()]() mutable
    {
        TaskProgress progress(this_task::ui());
        progress.setText(tr("Classifying atoms with trained SOM model"));
        progress.setMaximum(static_cast<qlonglong>(particleCount));

        // Use the mapping mode baked into the trained model itself, NOT the modifier's
        // current (possibly since-edited) mappingMode field -- see the "self-describing
        // trained model" invariant documented on SomAlgorithm::TrainedModel. The live
        // mappingMode() field only controls what gets baked in on the *next* Train click.
        const SomAlgorithm::MappingMode algMode = model.mappingMode;

        // One typed accessor per distinct referenced feature, plus a name -> index map for
        // the featureLookup closure passed into SomAlgorithm::classify().
        std::vector<BufferAccessConvertedTo<float>> accessors;
        accessors.reserve(refProps.size());
        for(const ConstPropertyPtr& prop : refProps)
            accessors.emplace_back(prop.get());

        QHash<QString, int> refIndex;
        for(int i = 0; i < refList.size(); ++i)
            refIndex.insert(refList[i], i);

        // Resolve every layer's feature columns to plain array offsets ONCE. The per-atom
        // callback then costs an array index instead of a string hash, and a feature that
        // failed to resolve becomes an error here rather than silently reading column 0
        // for the whole trajectory.
        std::vector<std::vector<int>> layerSlots(model.layers.size());
        for(size_t l = 0; l < model.layers.size(); ++l) {
            layerSlots[l].reserve(model.layers[l].featureRefs.size());
            for(const QString& ref : model.layers[l].featureRefs) {
                const auto it = refIndex.constFind(ref);
                if(it == refIndex.constEnd())
                    throw Exception(tr("SOM clustering: input feature '%1' could not be resolved.").arg(ref));
                layerSlots[l].push_back(*it);
            }
        }

        const int numLayers = static_cast<int>(model.layers.size());
        const bool useDefault = (algMode == SomAlgorithm::MappingMode::Default);
        const bool useGodel = (algMode == SomAlgorithm::MappingMode::Godel);

        std::vector<PropertyPtr> defaultLayerProps;
        PropertyPtr singleIntProp, singleFloatProp;
        std::vector<BufferWriteAccess<int64_t, access_mode::discard_write>> defaultLayerAcc;
        std::optional<BufferWriteAccess<int64_t, access_mode::discard_write>> singleIntAcc;
        std::optional<BufferWriteAccess<double, access_mode::discard_write>> singleFloatAcc;

        if(useDefault) {
            defaultLayerProps.reserve(static_cast<size_t>(numLayers));
            defaultLayerAcc.reserve(static_cast<size_t>(numLayers));
            for(int k = 0; k < numLayers; ++k) {
                PropertyPtr prop = Particles::OOClass().createUserProperty(
                    DataBuffer::Uninitialized, particleCount, Property::Int64, 1,
                    tr("%1 Layer %2").arg(outputBaseName).arg(k + 1));
                defaultLayerAcc.emplace_back(prop);
                defaultLayerProps.push_back(std::move(prop));
            }
        }
        else if(useGodel) {
            singleIntProp = Particles::OOClass().createUserProperty(
                DataBuffer::Uninitialized, particleCount, Property::Int64, 1, outputBaseName);
            singleIntAcc.emplace(singleIntProp);
        }
        else {
            singleFloatProp = Particles::OOClass().createUserProperty(
                DataBuffer::Uninitialized, particleCount, Property::Float64, 1, outputBaseName);
            singleFloatAcc.emplace(singleFloatProp);
        }

        std::optional<BufferReadAccess<SelectionIntType>> selectionAcc;
        if(selectionProperty)
            selectionAcc.emplace(selectionProperty);

        // Mixed-radix code of the per-layer id tuple, which is what a "cluster" actually is
        // in every mapping mode (Godel and Linear are just two encodings of that same tuple).
        // Digit l holds perLayerIds[l] + 1, so 0 encodes "this layer was never reached" --
        // the case for an atom that fell into an untrained branch or overflowed the Godel id.
        // -1 is reserved for atoms that were not classified at all.
        std::vector<int64_t> radix(model.layers.size());
        bool tupleCodeFits = true;
        {
            int64_t stride = 1;
            for(size_t l = 0; l < model.layers.size(); ++l) {
                radix[l] = stride;
                const int64_t digits = static_cast<int64_t>(std::max(1, model.layers[l].numNeurons)) + 1;
                if(stride > (std::numeric_limits<int64_t>::max)() / digits) { tupleCodeFits = false; break; }
                stride *= digits;
            }
        }
        std::vector<int64_t> tupleCodes(tupleCodeFits ? particleCount : 0, -1);

        // Cache-line aligned: without it the four counters of adjacent workers share a line
        // and every unselected atom bounces it between cores.
        struct alignas(64) WorkerState {
            size_t missingBranch = 0;
            size_t godelOverflow = 0;
            size_t invalidFeatures = 0;
            size_t unselected = 0;
            std::vector<float> featureCache;
        };
        std::vector<WorkerState> workers;

        parallelFor(particleCount, 1024, progress,
            [&](size_t workerCount) noexcept {
                workers.resize(workerCount);
                for(WorkerState& w : workers)
                    w.featureCache.resize(static_cast<size_t>(refList.size()));
            },
            [&](size_t workerIndex, size_t i) {
                WorkerState& worker = workers[workerIndex];

                // Atoms left out of classification (unselected, or carrying a non-finite
                // feature) share one sentinel, so a single downstream expression selects
                // everything the model did not assign.
                auto writeUnassigned = [&]() {
                    if(useDefault) {
                        for(int k = 0; k < numLayers; ++k)
                            defaultLayerAcc[static_cast<size_t>(k)][i] = int64_t(-1);
                    }
                    else if(useGodel) (*singleIntAcc)[i] = int64_t(0);
                    else (*singleFloatAcc)[i] = 0.0;
                };

                if(selectionAcc && (*selectionAcc)[i] == 0) {
                    writeUnassigned();
                    ++worker.unselected;
                    return;
                }

                for(int r = 0; r < refList.size(); ++r) {
                    const int nComp = refProps[static_cast<size_t>(r)]->componentCount();
                    worker.featureCache[static_cast<size_t>(r)] = accessors[static_cast<size_t>(r)][i * static_cast<size_t>(nComp) + static_cast<size_t>(refComps[static_cast<size_t>(r)])];
                }

                const SomAlgorithm::ClassificationResult result = SomAlgorithm::classify(
                    model.tree, model.layers,
                    [&](size_t layerIdx, int k) -> float { return worker.featureCache[static_cast<size_t>(layerSlots[layerIdx][static_cast<size_t>(k)])]; },
                    algMode);

                if(result.hitMissingBranch) ++worker.missingBranch;
                if(result.godelOverflow) ++worker.godelOverflow;
                if(result.invalidFeatures) ++worker.invalidFeatures;

                if(result.invalidFeatures) {
                    writeUnassigned();
                    return;
                }

                if(useDefault) {
                    for(int k = 0; k < numLayers; ++k)
                        defaultLayerAcc[static_cast<size_t>(k)][i] = static_cast<int64_t>(result.perLayerIds[static_cast<size_t>(k)]);
                }
                else if(useGodel) {
                    (*singleIntAcc)[i] = result.intId;
                }
                else {
                    (*singleFloatAcc)[i] = result.floatId;
                }

                if(tupleCodeFits) {
                    int64_t code = 0;
                    for(size_t l = 0; l < model.layers.size(); ++l)
                        code += radix[l] * static_cast<int64_t>(result.perLayerIds[l] + 1);
                    tupleCodes[i] = code;
                }
            });

        size_t missingBranchCount = 0;
        size_t godelOverflowCount = 0;
        size_t invalidFeatureCount = 0;
        size_t unselectedCount = 0;
        for(const WorkerState& w : workers) {
            missingBranchCount += w.missingBranch;
            godelOverflowCount += w.godelOverflow;
            invalidFeatureCount += w.invalidFeatures;
            unselectedCount += w.unselected;
        }

        for(auto& acc : defaultLayerAcc) acc.reset();
        if(singleIntAcc) singleIntAcc->reset();
        if(singleFloatAcc) singleFloatAcc->reset();

        for(PropertyPtr& prop : defaultLayerProps)
            particles->createProperty(std::move(prop));
        if(singleIntProp)
            particles->createProperty(std::move(singleIntProp));
        if(singleFloatProp)
            particles->createProperty(std::move(singleFloatProp));

        // ------------------------------------------------------------------
        // Cluster census.
        //
        // A SOM tells you which cluster each atom landed in, but the question anyone reads
        // the result with is "how many clusters are there, and how big are they?" -- without
        // it, the population of each cluster has to be reconstructed by hand outside OVITO.
        // Clusters are counted over the per-layer id tuple, which is the cluster identity in
        // all three mapping modes.
        // ------------------------------------------------------------------
        struct ClusterEntry { int64_t count = 0; size_t representativeAtom = 0; };
        std::map<int64_t, ClusterEntry> census;
        for(size_t i = 0; i < tupleCodes.size(); ++i) {
            if(tupleCodes[i] < 0)
                continue;   // Unassigned: not a cluster of its own.
            ClusterEntry& entry = census[tupleCodes[i]];
            if(entry.count == 0)
                entry.representativeAtom = i;
            ++entry.count;
        }

        const size_t unassignedCount = unselectedCount + invalidFeatureCount;
        // Only claim a cluster count when one was actually taken. If the id tuple did not fit
        // into an Int64 the census is empty, and publishing "0 clusters" for a frame full of
        // clusters would be worse than publishing nothing.
        if(tupleCodeFits) {
            state.addAttribute(QStringLiteral("SOM.cluster_count"),
                QVariant::fromValue(static_cast<qlonglong>(census.size())), createdByNode);
        }
        state.addAttribute(QStringLiteral("SOM.assigned_count"),
            QVariant::fromValue(static_cast<qlonglong>(particleCount - unassignedCount)), createdByNode);
        state.addAttribute(QStringLiteral("SOM.unassigned_count"),
            QVariant::fromValue(static_cast<qlonglong>(unassignedCount)), createdByNode);

        if(!census.empty()) {
            // Largest first: the size distribution is the first thing worth seeing, and a SOM
            // that has collapsed onto one neuron is then obvious at a glance.
            std::vector<std::pair<int64_t, ClusterEntry>> ranked(census.begin(), census.end());
            std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
                return a.second.count != b.second.count ? a.second.count > b.second.count : a.first < b.first;
            });
            state.addAttribute(QStringLiteral("SOM.largest_cluster_size"),
                QVariant::fromValue(static_cast<qlonglong>(ranked.front().second.count)), createdByNode);

            const size_t numClusters = ranked.size();
            PropertyPtr rankProp = DataTable::OOClass().createUserProperty(
                DataBuffer::Uninitialized, numClusters, Property::Int64, 1, tr("Cluster"));
            PropertyPtr countProp = DataTable::OOClass().createUserProperty(
                DataBuffer::Uninitialized, numClusters, Property::Int64, 1, tr("Count"));
            std::vector<PropertyPtr> layerProps;
            for(int k = 0; k < numLayers; ++k) {
                layerProps.push_back(DataTable::OOClass().createUserProperty(
                    DataBuffer::Uninitialized, numClusters, Property::Int64, 1, tr("Layer %1").arg(k + 1)));
            }
            PropertyPtr idProp;
            if(useGodel) {
                idProp = DataTable::OOClass().createUserProperty(
                    DataBuffer::Uninitialized, numClusters, Property::Int64, 1, tr("Cluster Identifier"));
            }
            else if(!useDefault) {
                idProp = DataTable::OOClass().createUserProperty(
                    DataBuffer::Uninitialized, numClusters, Property::Float64, 1, tr("Cluster Identifier"));
            }

            {
                BufferWriteAccess<int64_t, access_mode::discard_write> rankAcc(rankProp);
                BufferWriteAccess<int64_t, access_mode::discard_write> countAcc(countProp);
                std::vector<BufferWriteAccess<int64_t, access_mode::discard_write>> layerAcc;
                for(PropertyPtr& prop : layerProps)
                    layerAcc.emplace_back(prop);
                std::optional<BufferWriteAccess<int64_t, access_mode::discard_write>> godelAcc;
                std::optional<BufferWriteAccess<double, access_mode::discard_write>> linearAcc;
                if(useGodel) godelAcc.emplace(idProp);
                else if(!useDefault) linearAcc.emplace(idProp);

                std::vector<float> featureCache(static_cast<size_t>(refList.size()));
                for(size_t c = 0; c < numClusters; ++c) {
                    rankAcc[c] = static_cast<int64_t>(c);
                    countAcc[c] = ranked[c].second.count;

                    // Re-classify one representative atom rather than re-deriving the encoded
                    // id from the tuple here: the mapping formulas must have exactly one
                    // implementation, and it is the one in SomAlgorithm::classify().
                    const size_t rep = ranked[c].second.representativeAtom;
                    for(int r = 0; r < refList.size(); ++r) {
                        const int nComp = refProps[static_cast<size_t>(r)]->componentCount();
                        featureCache[static_cast<size_t>(r)] = accessors[static_cast<size_t>(r)][rep * static_cast<size_t>(nComp) + static_cast<size_t>(refComps[static_cast<size_t>(r)])];
                    }
                    const SomAlgorithm::ClassificationResult repResult = SomAlgorithm::classify(
                        model.tree, model.layers,
                        [&](size_t layerIdx, int k) -> float { return featureCache[static_cast<size_t>(layerSlots[layerIdx][static_cast<size_t>(k)])]; },
                        algMode);

                    for(int k = 0; k < numLayers; ++k)
                        layerAcc[static_cast<size_t>(k)][c] = static_cast<int64_t>(repResult.perLayerIds[static_cast<size_t>(k)]);
                    if(godelAcc) (*godelAcc)[c] = repResult.intId;
                    else if(linearAcc) (*linearAcc)[c] = repResult.floatId;
                }
            }

            DataTable* table = state.createObject<DataTable>(QStringLiteral("som-clusters"),
                createdByNode, DataTable::BarChart, tr("SOM cluster list"),
                countProp, idProp ? idProp : rankProp);
            table->setAxisLabelX(idProp ? tr("Cluster identifier") : tr("Cluster (by size, largest first)"));
            table->setAxisLabelY(tr("Atoms"));
            if(idProp)
                table->createProperty(std::move(rankProp));
            for(PropertyPtr& prop : layerProps)
                table->createProperty(std::move(prop));
        }

        // These conditions are independent and can co-occur, so report every one that fired
        // rather than picking a single message.
        QStringList notes;
        if(!tupleCodeFits) {
            notes << tr("The cluster census was skipped: with %1 layers of up to %2 neurons, a cluster's "
                        "layer-id tuple does not fit into a 64-bit integer. The per-atom cluster ids are "
                        "unaffected; only SOM.cluster_count and the cluster list table are missing.")
                .arg(numLayers).arg(std::max_element(model.layers.begin(), model.layers.end(),
                    [](const auto& a, const auto& b) { return a.numNeurons < b.numNeurons; })->numNeurons);
        }
        if(selectionProperty) {
            notes << tr("Trained on selected particles only: %1 of %2 atoms classified, the %3 unselected ones left unassigned (%4).")
                .arg(particleCount - unselectedCount).arg(particleCount).arg(unselectedCount)
                .arg(useDefault ? tr("id -1 in every layer") : tr("id 0"));
        }

        QStringList warnings;
        if(invalidFeatureCount > 0) {
            warnings << tr("%1 of %2 atoms carry a non-finite (NaN/inf) value in one of the input features and could not be classified; they were left unassigned (%3). Check the upstream modifiers that produce these features -- many leave NaN on atoms without enough neighbors.")
                .arg(invalidFeatureCount).arg(particleCount)
                .arg(useDefault ? tr("id -1 from the offending layer on") : tr("id 0"));
        }
        if(missingBranchCount > 0) {
            warnings << tr("%1 of %2 atoms fell into an untrained sub-branch of the SOM tree and received a coarser cluster id.")
                .arg(missingBranchCount).arg(particleCount);
        }
        if(godelOverflowCount > 0) {
            warnings << tr("%1 of %2 atoms hit the Godel id encoding's Int64 capacity limit and received a coarser cluster id. "
                   "This is not a training problem -- reduce the number/width of layers, or use a different layer id mapping.")
                .arg(godelOverflowCount).arg(particleCount);
        }
        state.setStatus(warnings.isEmpty()
            ? PipelineStatus(PipelineStatus::Success, notes.join(QStringLiteral("\n")))
            : PipelineStatus(PipelineStatus::Warning, (notes + warnings).join(QStringLiteral("\n"))));

        return std::move(state);
    });
}

} // End of namespace
