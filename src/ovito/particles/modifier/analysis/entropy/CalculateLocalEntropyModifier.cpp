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
#include <ovito/particles/util/CutoffNeighborFinder.h>
#include <ovito/particles/objects/Particles.h>
#include <ovito/stdobj/simcell/SimulationCell.h>
#include <ovito/core/dataset/pipeline/ModificationNode.h>
#include <ovito/core/utilities/concurrent/ParallelFor.h>
#include <ovito/core/utilities/units/UnitsManager.h>
#include "CalculateLocalEntropyModifier.h"

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(CalculateLocalEntropyModifier);
OVITO_CLASSINFO(CalculateLocalEntropyModifier, "DisplayName", "Calculate local entropy");
OVITO_CLASSINFO(CalculateLocalEntropyModifier, "Description", "Compute the local pair entropy fingerprint of each particle.");
OVITO_CLASSINFO(CalculateLocalEntropyModifier, "ModifierCategory", "Structure identification");
DEFINE_PROPERTY_FIELD(CalculateLocalEntropyModifier, cutoff);
DEFINE_PROPERTY_FIELD(CalculateLocalEntropyModifier, sigma);
DEFINE_PROPERTY_FIELD(CalculateLocalEntropyModifier, useLocalDensity);
DEFINE_PROPERTY_FIELD(CalculateLocalEntropyModifier, computeAverage);
DEFINE_PROPERTY_FIELD(CalculateLocalEntropyModifier, averageCutoff);
SET_PROPERTY_FIELD_LABEL(CalculateLocalEntropyModifier, cutoff, "Cutoff");
SET_PROPERTY_FIELD_LABEL(CalculateLocalEntropyModifier, sigma, "Sigma");
SET_PROPERTY_FIELD_LABEL(CalculateLocalEntropyModifier, useLocalDensity, "Use local density");
SET_PROPERTY_FIELD_LABEL(CalculateLocalEntropyModifier, computeAverage, "Compute average");
SET_PROPERTY_FIELD_LABEL(CalculateLocalEntropyModifier, averageCutoff, "Average cutoff");
SET_PROPERTY_FIELD_UNITS_AND_MINIMUM(CalculateLocalEntropyModifier, cutoff, WorldParameterUnit, 0);
SET_PROPERTY_FIELD_UNITS_AND_MINIMUM(CalculateLocalEntropyModifier, sigma, WorldParameterUnit, 0);
SET_PROPERTY_FIELD_UNITS_AND_MINIMUM(CalculateLocalEntropyModifier, averageCutoff, WorldParameterUnit, 0);

/******************************************************************************
* Asks the modifier whether it can be applied to the given input data.
******************************************************************************/
bool CalculateLocalEntropyModifier::OOMetaClass::isApplicableTo(const DataCollection& input) const
{
    return input.containsObject<Particles>();
}

/******************************************************************************
 * Is called by the pipeline system before a new modifier evaluation begins.
 ******************************************************************************/
void CalculateLocalEntropyModifier::preevaluateModifier(const ModifierEvaluationRequest& request, PipelineEvaluationResult::EvaluationTypes& evaluationTypes, TimeInterval& validityInterval) const
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
Future<PipelineFlowState> CalculateLocalEntropyModifier::evaluateModifier(const ModifierEvaluationRequest& request, PipelineFlowState&& state)
{
    if(cutoff() <= 0)
        throw Exception(tr("Invalid cutoff distance. It must be positive."));
    if(sigma() <= 0)
        throw Exception(tr("Invalid Gaussian width sigma. It must be positive."));
    if(computeAverage() && averageCutoff() <= 0)
        throw Exception(tr("Invalid averaging cutoff distance. It must be positive."));

    // Get input data.
    Particles* particles = state.expectMutableObject<Particles>();
    particles->verifyIntegrity();

    // In interactive mode, do not perform a real computation. Instead, reuse old results if available in the pipeline cache.
    if(request.interactiveMode()) {
        if(PipelineFlowState cachedState = request.modificationNode()->getCachedPipelineNodeOutput(request.time(), true)) {
            if(const Particles* cachedParticles = cachedState.getObject<Particles>())
                particles->tryToAdoptProperties(cachedParticles, { cachedParticles->getProperty(QStringLiteral("Entropy")) }, {particles});
        }
        return std::move(state);
    }

    const SimulationCell* cell = state.expectObject<SimulationCell>();
    if(cell->is2D())
        throw Exception(tr("The local entropy calculation is only implemented for three-dimensional systems."));
    if(!useLocalDensity() && cell->volume3D() <= 0)
        throw Exception(tr("The simulation cell volume is zero; the global density is undefined."));

    // Perform the calculation in a separate thread.
    return asyncLaunch([
            state = std::move(state),
            particles,
            cell,
            cutoff = cutoff(),
            sigma = sigma(),
            useLocalDensity = useLocalDensity(),
            computeAverage = computeAverage(),
            averageCutoff = averageCutoff()]() mutable
    {
        TaskProgress progress(this_task::ui());
        progress.setText(tr("Computing local entropy"));

        const size_t count = particles->elementCount();
        const ConstPropertyPtr positions = particles->expectProperty(Particles::PositionProperty);
        const FloatType globalDensity = count / cell->volume3D();
        const FloatType cutoffSphereVolume = FloatType(4) / 3 * FLOATTYPE_PI * cutoff * cutoff * cutoff;

        // g(r) is sampled on nbin equidistant points from 0 to the cutoff (spacing ~sigma), and every pair contributes
        // to all of them. This grid reproduces OVITO Pro 3.16.1 to machine precision (measured as a black box; a
        // window of +/-3 bins with spacing sigma, as in LAMMPS, differs by ~2% in fcc when the cutoff is not a multiple of sigma).
        const int nbin = static_cast<int>(cutoff / sigma) + 1;
        const FloatType deltar = (nbin > 1) ? cutoff / (nbin - 1) : 0;
        const FloatType twoSigmaSq = 2 * sigma * sigma;

        CutoffNeighborFinder neighFinder(cutoff, positions, cell, nullptr);
        std::vector<FloatType> entropy(count);
        parallelFor(count, 1024, progress, [&](size_t i) {
            thread_local std::vector<FloatType> gofr;
            thread_local std::vector<FloatType> distances;
            gofr.resize(nbin);
            distances.clear();
            for(CutoffNeighborFinder::Query query(neighFinder, i); !query.atEnd(); query.next())
                distances.push_back(std::sqrt(query.distanceSquared()));

            const FloatType density = useLocalDensity ? distances.size() / cutoffSphereVolume : globalDensity;
            if(density <= 0) {
                // An isolated particle with the local density option: s = -2 pi rho * (...) vanishes for rho = 0.
                entropy[i] = 0;
                return;
            }
            // Normalization of g(r) (4 pi rho r^2) times normalization of the Gaussian (sqrt(2 pi) sigma).
            const FloatType invNormBase = FloatType(1) / (4 * FLOATTYPE_PI * density * std::sqrt(2 * FLOATTYPE_PI) * sigma);

            std::fill(gofr.begin(), gofr.end(), FloatType(0));
            for(FloatType r : distances) {
                for(int k = 1; k < nbin; k++) {  // r_0 = 0 carries no weight in the integral
                    const FloatType rk = k * deltar;
                    const FloatType d = r - rk;
                    gofr[k] += invNormBase / (rk * rk) * std::exp(-d * d / twoSigmaSq);
                }
            }

            // s = -2 pi rho integral_0^rc [g ln g - g + 1] r^2 dr, trapezoidal rule; g ln g -> 0 for g -> 0.
            FloatType integral = 0;
            for(int k = 0; k < nbin; k++) {
                const FloatType rk = k * deltar;
                const FloatType g = gofr[k];
                FloatType f = (g < FloatType(1e-10)) ? rk * rk : (g * std::log(g) - g + 1) * rk * rk;
                integral += (k == 0 || k == nbin - 1) ? f / 2 : f;
            }
            entropy[i] = -2 * FLOATTYPE_PI * density * integral * deltar;
        });

        Property* entropyProperty = particles->createProperty(DataBuffer::Uninitialized, QStringLiteral("Entropy"), Property::FloatDefault);
        BufferWriteAccess<FloatType, access_mode::discard_write> output(entropyProperty);
        if(!computeAverage) {
            std::ranges::copy(entropy, output.begin());
        }
        else {
            CutoffNeighborFinder avgFinder(averageCutoff, positions, cell, nullptr);
            parallelFor(count, 4096, progress, [&](size_t i) {
                FloatType sum = entropy[i];
                int n = 1;
                for(CutoffNeighborFinder::Query query(avgFinder, i); !query.atEnd(); query.next()) {
                    sum += entropy[query.current()];
                    n++;
                }
                output[i] = sum / n;
            });
        }

        return std::move(state);
    });
}

}   // End of namespace
