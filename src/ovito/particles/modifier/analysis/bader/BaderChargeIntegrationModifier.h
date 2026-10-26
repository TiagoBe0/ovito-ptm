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


#pragma once


#include <ovito/particles/Particles.h>
#include <ovito/grid/objects/VoxelGrid.h>
#include <ovito/stdobj/properties/PropertyContainer.h>
#include <ovito/stdobj/properties/PropertyReference.h>
#include <ovito/core/dataset/pipeline/Modifier.h>

namespace Ovito {

/**
 * \brief Bader charge analysis of a density field on a voxel grid.
 *
 * Clean-room implementation of the weight method of M. Yu and D. R. Trinkle, J. Chem. Phys. 134, 064111 (2011),
 * written from that paper and the OVITO user manual, with the output layout matched to OVITO Pro 3.16.1 (black box):
 *
 * - The flux from grid point X to a neighbor X' with higher density is a_XX' / l_XX' * (rho' - rho), normalized over
 *   all uphill neighbors; a and l are the facet area and distance in the Voronoi cell of the grid lattice (computed
 *   with voro++, so that triclinic grids are handled). Points without uphill neighbors are density maxima and start
 *   a new Bader region; regions are numbered in order of decreasing maximum density.
 * - Weights are propagated in order of decreasing density, w_A(X) = sum J_XX' w_A(X'); integrals are
 *   sum_X V_X w_A(X) f(X) with V_X the volume per grid point.
 * - Each region is mapped to the particle nearest to its maximum (minimum image in periodic directions).
 *
 * Outputs the table "bader-charge-integration" (one row per region), the per-particle properties "Integrated Density",
 * "Integrated Volume" and "Integrated <integrand>", and the voxel properties "Region", "Maximum Region Weight" and
 * "Mapped Particle Index".
 */
class OVITO_PARTICLES_EXPORT BaderChargeIntegrationModifier : public Modifier
{
    /// Give this modifier class its own metaclass.
    class BaderChargeIntegrationModifierClass : public ModifierClass
    {
    public:

        /// Inherit constructor from base class.
        using ModifierClass::ModifierClass;

        /// Asks the metaclass whether the modifier can be applied to the given input data.
        virtual bool isApplicableTo(const DataCollection& input) const override;
    };

    OVITO_CLASS_META(BaderChargeIntegrationModifier, BaderChargeIntegrationModifierClass)

public:

    /// Identifier of the output table.
    static constexpr const char* tableIdentifier = "bader-charge-integration";

    /// Result of the Bader partitioning of a grid.
    struct Partition {
        size_t regionCount = 0;
        std::vector<size_t> maxima;                 ///< Grid point of the density maximum of each region.
        std::vector<int32_t> dominantRegion;        ///< Region with the largest weight at each grid point.
        std::vector<double> dominantWeight;         ///< That weight.
        std::vector<double> integratedDensity;      ///< Per region.
        std::vector<double> integratedVolume;       ///< Per region.
        std::vector<double> integratedValues;       ///< Per region and integrand component (row-major).
    };

    /// Partitions the grid and integrates the density and the integrand (row-major, 'components' values per grid point).
    /// 'spacing' holds the three grid lattice vectors; 'pbc' tells which directions wrap around.
    static Partition partition(const std::vector<double>& density, const std::vector<double>& integrand, size_t components,
                               const VoxelGrid::GridDimensions& shape, const Matrix3& spacing, const std::array<bool, 3>& pbc);

    /// Is called by the system when the modifier has been inserted into a pipeline.
    virtual void initializeModifier(const ModifierInitializationRequest& request) override;

    /// Is called by the pipeline system before a new modifier evaluation begins.
    virtual void preevaluateModifier(const ModifierEvaluationRequest& request, PipelineEvaluationResult::EvaluationTypes& evaluationTypes, TimeInterval& validityInterval) const override;

    /// Modifies the input data.
    virtual Future<PipelineFlowState> evaluateModifier(const ModifierEvaluationRequest& request, PipelineFlowState&& state) override;

private:

    /// The voxel grid to operate on.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(PropertyContainerReference{}, subject, setSubject);

    /// The density field that defines the Bader regions.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(PropertyReference{}, densityProperty, setDensityProperty);

    /// The field integrated over the regions (the density if not set).
    DECLARE_MODIFIABLE_PROPERTY_FIELD(PropertyReference{}, integrandProperty, setIntegrandProperty);
};

}   // End of namespace
