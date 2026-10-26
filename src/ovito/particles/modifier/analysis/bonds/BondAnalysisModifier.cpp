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


#include <ovito/particles/Particles.h>
#include <ovito/particles/objects/Particles.h>
#include <ovito/particles/objects/Bonds.h>
#include <ovito/particles/objects/ParticleBondMap.h>
#include <ovito/stdobj/simcell/SimulationCell.h>
#include <ovito/stdobj/table/DataTable.h>
#include <ovito/core/dataset/pipeline/ModificationNode.h>
#include "BondAnalysisModifier.h"

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(BondAnalysisModifier);
OVITO_CLASSINFO(BondAnalysisModifier, "DisplayName", "Bond analysis");
OVITO_CLASSINFO(BondAnalysisModifier, "ModifierCategory", "Analysis");
OVITO_CLASSINFO(BondAnalysisModifier, "Description", "Compute the bond length and bond angle distributions (superseded by the two bond distribution modifiers).");
DEFINE_PROPERTY_FIELD(BondAnalysisModifier, bins);
DEFINE_PROPERTY_FIELD(BondAnalysisModifier, useCosine);
DEFINE_PROPERTY_FIELD(BondAnalysisModifier, lengthCutoff);
DEFINE_PROPERTY_FIELD(BondAnalysisModifier, partition);
SET_PROPERTY_FIELD_LABEL(BondAnalysisModifier, bins, "Number of bins");
SET_PROPERTY_FIELD_LABEL(BondAnalysisModifier, useCosine, "Use cosines of angles");
SET_PROPERTY_FIELD_LABEL(BondAnalysisModifier, lengthCutoff, "Bond length cutoff");
SET_PROPERTY_FIELD_LABEL(BondAnalysisModifier, partition, "Partitioning");
SET_PROPERTY_FIELD_UNITS_AND_RANGE(BondAnalysisModifier, bins, IntegerParameterUnit, 1, 100000);
SET_PROPERTY_FIELD_UNITS_AND_MINIMUM(BondAnalysisModifier, lengthCutoff, WorldParameterUnit, 0);

IMPLEMENT_CREATABLE_OVITO_CLASS(BondLengthDistributionModifier);
OVITO_CLASSINFO(BondLengthDistributionModifier, "DisplayName", "Bond length distribution");
OVITO_CLASSINFO(BondLengthDistributionModifier, "Description", "Compute the distribution of bond lengths.");
OVITO_CLASSINFO(BondLengthDistributionModifier, "ModifierCategory", "Analysis");

IMPLEMENT_CREATABLE_OVITO_CLASS(BondAngleDistributionModifier);
OVITO_CLASSINFO(BondAngleDistributionModifier, "DisplayName", "Bond angle distribution");
OVITO_CLASSINFO(BondAngleDistributionModifier, "Description", "Compute the distribution of angles between bonds.");
OVITO_CLASSINFO(BondAngleDistributionModifier, "ModifierCategory", "Analysis");

namespace {

/// Index of the unordered pair (a, b) among the N(N+1)/2 pairs of N kinds.
inline size_t pairIndex(size_t a, size_t b, size_t n)
{
    if(a > b) std::swap(a, b);
    return a * n - a * (a - 1) / 2 + (b - a);
}

/// Maps the values of a typed property to consecutive indices (sorted by numeric type ID) and returns their labels.
std::vector<size_t> classifyByType(const Property* typeProperty, QStringList& labels)
{
    std::map<int, QString> types;
    for(const ElementType* type : typeProperty->elementTypes())
        types.emplace(type->numericId(), type->name().isEmpty() ? QString::number(type->numericId()) : type->name());
    BufferReadAccess<int32_t> typeIds(typeProperty);
    for(int32_t t : typeIds)
        types.emplace(t, QString::number(t));  // Types without an ElementType definition.

    std::map<int, size_t> typeToIndex;
    for(const auto& [id, name] : types) {
        typeToIndex.emplace(id, labels.size());
        labels.push_back(name);
    }
    std::vector<size_t> classes(typeIds.size());
    for(size_t i = 0; i < classes.size(); i++)
        classes[i] = typeToIndex[typeIds[i]];
    return classes;
}

/// Maps a selection property to the two classes 0 (unselected) and 1 (selected).
std::vector<size_t> classifyBySelection(const Property* selectionProperty, QStringList& labels)
{
    labels = QStringList{QStringLiteral("Unselected"), QStringLiteral("Selected")};
    BufferReadAccess<SelectionIntType> selection(selectionProperty);
    std::vector<size_t> classes(selection.size());
    for(size_t i = 0; i < classes.size(); i++)
        classes[i] = selection[i] ? 1 : 0;
    return classes;
}

}   // End of anonymous namespace

/******************************************************************************
* Constructor.
******************************************************************************/
void BondLengthDistributionModifier::initializeObject(ObjectInitializationFlags flags)
{
    BondAnalysisModifier::initializeObject(flags);
    if(!flags.testFlag(ObjectInitializationFlag::DontInitializeObject))
        setBins(100);   // Default of OVITO Pro.
}

/******************************************************************************
* Asks the modifier whether it can be applied to the given input data.
******************************************************************************/
bool BondAnalysisModifier::OOMetaClass::isApplicableTo(const DataCollection& input) const
{
    return input.containsObject<Particles>();
}

/******************************************************************************
 * Is called by the pipeline system before a new modifier evaluation begins.
 ******************************************************************************/
void BondAnalysisModifier::preevaluateModifier(const ModifierEvaluationRequest& request, PipelineEvaluationResult::EvaluationTypes& evaluationTypes, TimeInterval& validityInterval) const
{
    // Indicate that we will do different computations depending on whether the pipeline is evaluated in interactive mode or not.
    if(request.interactiveMode())
        evaluationTypes = PipelineEvaluationResult::EvaluationType::Interactive;
    else
        evaluationTypes = PipelineEvaluationResult::EvaluationType::Noninteractive;
}

/******************************************************************************
* Modifies the input data.
******************************************************************************/
Future<PipelineFlowState> BondAnalysisModifier::evaluateModifier(const ModifierEvaluationRequest& request, PipelineFlowState&& state)
{
    if(bins() < 1)
        throw Exception(tr("Invalid number of bins. It must be at least 1."));
    if(lengthCutoff() <= 0)
        throw Exception(tr("Invalid bond length cutoff. It must be positive."));

    const Particles* particles = state.expectObject<Particles>();
    particles->verifyIntegrity();

    // In interactive mode, do not perform a real computation. Instead, reuse old results if available in the pipeline cache.
    if(request.interactiveMode()) {
        if(PipelineFlowState cachedState = request.modificationNode()->getCachedPipelineNodeOutput(request.time(), true)) {
            for(const char* identifier : {lengthTableIdentifier, angleTableIdentifier}) {
                if(const DataTable* cachedTable = cachedState.getObjectBy<DataTable>(request.modificationNode(), QString::fromLatin1(identifier)))
                    state.addObject(cachedTable);
            }
        }
        return std::move(state);
    }

    const Bonds* bonds = particles->expectBonds();
    bonds->verifyIntegrity();

    // Classify the bonds (bond partitioning modes) or the particles (particle partitioning modes).
    QStringList labels;
    std::vector<size_t> bondClasses;
    std::vector<size_t> particleClasses;
    switch(partition()) {
    case Off:
        break;
    case ByBondType:
        bondClasses = classifyByType(bonds->expectProperty(Bonds::TypeProperty), labels);
        break;
    case ByBondSelection:
        bondClasses = classifyBySelection(bonds->expectProperty(Bonds::SelectionProperty), labels);
        break;
    case ByParticleType:
        particleClasses = classifyByType(particles->expectProperty(Particles::TypeProperty), labels);
        break;
    case ByParticleSelection:
        particleClasses = classifyBySelection(particles->expectProperty(Particles::SelectionProperty), labels);
        break;
    }
    if(partition() != Off && labels.empty())
        throw Exception(tr("Cannot partition the distributions: no types are defined."));
    if(labels.size() > 20)
        throw Exception(tr("Partitioning is limited to 20 types; the input contains %1.").arg(labels.size()));

    // Component layout and names of the two histograms.
    const size_t n = std::max<size_t>(labels.size(), 1);
    const size_t numPairs = n * (n + 1) / 2;
    QStringList lengthNames, angleNames;
    if(!bondClasses.empty()) {
        lengthNames = labels;
        for(size_t a = 0; a < n; a++)
            for(size_t b = a; b < n; b++)
                angleNames.push_back(QStringLiteral("%1-%2").arg(labels[a], labels[b]));
    }
    else if(!particleClasses.empty()) {
        for(size_t a = 0; a < n; a++)
            for(size_t b = a; b < n; b++)
                lengthNames.push_back(QStringLiteral("%1-%2").arg(labels[a], labels[b]));
        for(size_t c = 0; c < n; c++)
            for(size_t a = 0; a < n; a++)
                for(size_t b = a; b < n; b++)
                    angleNames.push_back(QStringLiteral("%1-%2-%3").arg(labels[a], labels[c], labels[b]));
    }
    const size_t numLengthComponents = std::max<qsizetype>(lengthNames.size(), 1);
    const size_t numAngleComponents = std::max<qsizetype>(angleNames.size(), 1);

    const SimulationCell* cell = state.getObject<SimulationCell>();

    // Perform the calculation in a separate thread.
    return asyncLaunch([
            state = std::move(state),
            particles,
            bonds,
            cell,
            bondClasses = std::move(bondClasses),
            particleClasses = std::move(particleClasses),
            lengthNames = std::move(lengthNames),
            angleNames = std::move(angleNames),
            n, numPairs, numLengthComponents, numAngleComponents,
            numBins = (size_t)bins(),
            useCosine = useCosine(),
            cutoff = lengthCutoff(),
            withLengths = computesLengths(),
            withAngles = computesAngles(),
            createdByNode = request.modificationNodeWeak()]() mutable
    {
        TaskProgress progress(this_task::ui());
        progress.setText(tr("Bond analysis"));

        BufferReadAccess<Point3> positions(particles->expectProperty(Particles::PositionProperty));
        BufferReadAccess<ParticleIndexPair> topology(bonds->expectProperty(Bonds::TopologyProperty));
        BufferReadAccess<Vector3I> periodicImages(bonds->getProperty(Bonds::PeriodicImageProperty));
        const size_t particleCount = positions.size();

        // Vector from the first to the second particle of a bond, including its periodic image shift.
        auto bondVector = [&](size_t i1, size_t i2, const Vector3I& pbcShift) {
            Vector3 delta = positions[i2] - positions[i1];
            if(cell && pbcShift != Vector3I::Zero())
                delta += cell->cellMatrix() * pbcShift.toDataType<FloatType>();
            return delta;
        };

        // Bond length histogram.
        std::vector<int64_t> lengthCounts(numBins * numLengthComponents, 0);
        for(size_t b = 0; withLengths && b < topology.size(); b++) {
            size_t i1 = topology[b][0], i2 = topology[b][1];
            if(i1 >= particleCount || i2 >= particleCount)
                continue;
            FloatType length = bondVector(i1, i2, periodicImages ? periodicImages[b] : Vector3I::Zero()).length();
            if(length >= cutoff)
                continue;
            size_t bin = std::min(static_cast<size_t>(length / cutoff * numBins), numBins - 1);
            size_t component = 0;
            if(!bondClasses.empty())
                component = bondClasses[b];
            else if(!particleClasses.empty())
                component = pairIndex(particleClasses[i1], particleClasses[i2], n);
            lengthCounts[bin * numLengthComponents + component]++;
        }

        // Bond angle histogram: all pairs of bonds meeting at each particle.
        std::vector<int64_t> angleCounts(numBins * numAngleComponents, 0);
        ParticleBondMap bondMap(*bonds);
        progress.setMaximum(particleCount);
        std::vector<Vector3> vectors;
        std::vector<size_t> neighborClasses;
        for(size_t center = 0; withAngles && center < particleCount; center++) {
            vectors.clear();
            neighborClasses.clear();
            for(const BondWithIndex& bond : bondMap.bondsOfParticle(center)) {
                if(bond.index2 >= particleCount)
                    continue;
                Vector3 v = bondVector(center, bond.index2, bond.pbcShift);
                if(v.isZero())
                    continue;
                vectors.push_back(v);
                neighborClasses.push_back(!bondClasses.empty() ? bondClasses[bond.bondIndex] : !particleClasses.empty() ? particleClasses[bond.index2] : 0);
            }
            for(size_t p = 0; p < vectors.size(); p++) {
                for(size_t q = p + 1; q < vectors.size(); q++) {
                    FloatType cosine = std::clamp(vectors[p].dot(vectors[q]) / (vectors[p].length() * vectors[q].length()), FloatType(-1), FloatType(1));
                    FloatType x = useCosine ? (cosine + 1) / 2 : std::acos(cosine) / FLOATTYPE_PI;
                    size_t bin = std::min(static_cast<size_t>(x * numBins), numBins - 1);
                    size_t component = 0;
                    if(!bondClasses.empty())
                        component = pairIndex(neighborClasses[p], neighborClasses[q], n);
                    else if(!particleClasses.empty())
                        component = particleClasses[center] * numPairs + pairIndex(neighborClasses[p], neighborClasses[q], n);
                    angleCounts[bin * numAngleComponents + component]++;
                }
            }
            if((center % 4096) == 0)
                progress.setValue(center);
        }

        // Output the two histograms.
        auto outputTable = [&](const char* identifier, const QString& title, std::vector<int64_t>& counts, size_t numComponents, QStringList names,
                FloatType xmin, FloatType xmax, const QString& xlabel) {
            PropertyPtr y = DataTable::OOClass().createUserProperty(DataBuffer::Uninitialized, numBins, Property::Int64, numComponents,
                QStringLiteral("Count"), 0, std::move(names));
            std::ranges::copy(counts, BufferWriteAccess<int64_t*, access_mode::discard_write>(y).begin());
            // Plot mode and axis labels as in OVITO Pro.
            DataTable* table = state.createObject<DataTable>(QString::fromLatin1(identifier), createdByNode, DataTable::Line, title, std::move(y));
            table->setIntervalStart(xmin);
            table->setIntervalEnd(xmax);
            table->setAxisLabelX(xlabel);
        };
        if(withLengths)
            outputTable(lengthTableIdentifier, tr("Bond length distribution"), lengthCounts, numLengthComponents, lengthNames, 0, cutoff, tr("Length"));
        if(withAngles) {
            if(useCosine)
                outputTable(angleTableIdentifier, tr("Bond angle distribution"), angleCounts, numAngleComponents, angleNames, -1, 1, tr("Cosine of angle"));
            else
                outputTable(angleTableIdentifier, tr("Bond angle distribution"), angleCounts, numAngleComponents, angleNames, 0, 180, tr("Angle"));
        }

        return std::move(state);
    });
}

}   // End of namespace
