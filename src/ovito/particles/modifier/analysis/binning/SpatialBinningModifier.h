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
#include <ovito/stdobj/properties/PropertyReference.h>
#include <ovito/grid/objects/VoxelGridVis.h>
#include <ovito/grid/objects/VoxelGrid.h>
#include <ovito/stdobj/table/DataTable.h>
#include <ovito/stdobj/simcell/SimulationCell.h>
#include <ovito/core/dataset/pipeline/Modifier.h>

namespace Ovito {

class BinningLineSource;   // Defined in BinningLineSource.h

/**
 * \brief Projects a particle property onto a 1-, 2- or 3-dimensional grid of bins aligned with the
 *        simulation cell vectors, reducing the values that fall into each bin (sum, mean, min, max,
 *        sum divided by bin volume).
 *
 * 1D binning outputs a DataTable with identifier "binning" (one row per bin, optionally replaced by
 * its first derivative along the binning direction); 2D and 3D binning output a VoxelGrid with the
 * same identifier, whose shape has 1 along the cell vectors that are not binned.
 *
 * Without an input property, every particle contributes a value of 1, so "sum" counts particles
 * and "sum divided by bin volume" gives the number density. Bin volumes are always 3D cell volume
 * divided by the total number of bins.
 *
 * In the Dislocations mode the modifier instead computes the dislocation density (line length per volume)
 * and the Nye tensor (sum of b_i t_j times line length per volume, b = spatial Burgers vector, t = unit
 * line direction) of the dislocation lines. Each line segment is spread onto the bins with trilinear
 * (cloud-in-cell) weights between bin centers, integrated exactly along the segment, as in OVITO Pro
 * (Bertin, Int. J. Plast. 122 (2019), section 2). The line data comes from a BinningLineSource
 * implemented by another plugin (CrystalAnalysis for dislocation networks).
 */
class OVITO_PARTICLES_EXPORT SpatialBinningModifier : public Modifier
{
    class OVITO_PARTICLES_EXPORT SpatialBinningModifierClass : public Modifier::OOMetaClass
    {
    public:
        using Modifier::OOMetaClass::OOMetaClass;
        virtual bool isApplicableTo(const DataCollection& input) const override;
    };

    OVITO_CLASS_META(SpatialBinningModifier, SpatialBinningModifierClass)

public:

    /// The supported reduction operations.
    enum ReductionOperation {
        Mean,
        Sum,
        SumVol,     ///< Sum divided by the bin volume.
        Min,
        Max
    };
    Q_ENUM(ReductionOperation);

    /// The kind of data the modifier bins.
    enum OperateOn {
        OperateOnParticles,     ///< Reduction of a particle property.
        OperateOnDislocations   ///< Dislocation density and Nye tensor of the dislocation lines.
    };
    Q_ENUM(OperateOn);

    /// The cell vectors along which particles are binned.
    enum BinDirection {
        X = 1,
        Y = 2,
        Z = 4,
        XY = X | Y,
        XZ = X | Z,
        YZ = Y | Z,
        XYZ = X | Y | Z
    };
    Q_ENUM(BinDirection);

    /// Returns whether the given cell vector is binned in the given direction mode.
    static bool isBinned(BinDirection direction, int dim) { return (int(direction) >> dim) & 1; }

    /// Returns the number of binned cell vectors (the grid dimensionality).
    static int dimensionality(BinDirection direction) { return isBinned(direction, 0) + isBinned(direction, 1) + isBinned(direction, 2); }

    /// Constructor.
    void initializeObject(ObjectInitializationFlags flags);

    /// This method is called by the system after the modifier has been inserted into a data pipeline.
    virtual void initializeModifier(const ModifierInitializationRequest& request) override;

    /// Modifies the input data.
    virtual Future<PipelineFlowState> evaluateModifier(const ModifierEvaluationRequest& request, PipelineFlowState&& state) override;

    /// Returns a short piece of information to be displayed next to the modifier's title in the pipeline editor list.
    virtual QVariant getPipelineEditorShortInfo(Scene* scene, ModificationNode* node) const override {
        if(operateOn() == OperateOnDislocations)
            return tr("dislocations");
        return sourceProperty() ? sourceProperty().nameWithComponent() : tr("count");
    }

    /// Returns the line source that handles the line data in the given data collection, or null if there is none.
    static OORef<BinningLineSource> findLineSource(const DataCollection& input);

protected:

    /// Is called when the value of a property of this object has changed.
    virtual void propertyChanged(const PropertyFieldDescriptor* field) override;

private:

    /// Points the color mapping of the voxel grid vis element at the current output property.
    void updateColorMapping();

    /// Grid dimensionality, cell vector of each grid axis (binned ones first) and grid shape.
    struct OutputLayout {
        int ndim = 0;
        std::array<int, 3> binDims = {0, 1, 2};
        VoxelGrid::GridDimensions shape;
    };

    /// Determines the output grid layout from the binning direction and the bin counts.
    OutputLayout outputLayout(const SimulationCell* cell) const;

    /// Creates the output table (1D) or voxel grid (2D/3D), titled after the given label.
    std::pair<DataTable*, VoxelGrid*> createOutput(PipelineFlowState& state, const ModifierEvaluationRequest& request,
        const SimulationCell* cell, const OutputLayout& layout, const QString& label) const;

    /// Replaces a component-major 1D profile of n bins of width h by its first derivative.
    static void differentiate(std::vector<double>& values, size_t componentCount, size_t n, double h, bool periodic);

    /// Computes the dislocation density and Nye tensor (Dislocations mode).
    Future<PipelineFlowState> evaluateDislocations(const ModifierEvaluationRequest& request, PipelineFlowState&& state);

    /// Selects whether particles or dislocation lines are binned.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(OperateOn{OperateOnParticles}, operateOn, setOperateOn);

    /// The particle property to be binned (none = uniform value 1 per particle).
    DECLARE_MODIFIABLE_PROPERTY_FIELD(PropertyReference{}, sourceProperty, setSourceProperty);

    /// The reduction operation applied per bin.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(ReductionOperation{Mean}, reductionOperation, setReductionOperation, PROPERTY_FIELD_MEMORIZE);

    /// The cell vectors to bin along.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(BinDirection{Z}, binDirection, setBinDirection, PROPERTY_FIELD_MEMORIZE);

    /// Number of bins along the first, second and third cell vector.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(int{50}, numberOfBinsX, setNumberOfBinsX, PROPERTY_FIELD_MEMORIZE);
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(int{50}, numberOfBinsY, setNumberOfBinsY, PROPERTY_FIELD_MEMORIZE);
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(int{20}, numberOfBinsZ, setNumberOfBinsZ, PROPERTY_FIELD_MEMORIZE);

    /// Replaces the 1D profile by its first derivative (central finite differences).
    DECLARE_MODIFIABLE_PROPERTY_FIELD(bool{false}, firstDerivative, setFirstDerivative);

    /// Restricts the binning to the currently selected particles.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(bool{false}, onlySelectedElements, setOnlySelectedElements);

    /// Fixes the color range of the voxel grid to [propertyAxisRangeStart, propertyAxisRangeEnd].
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(bool{false}, fixPropertyAxisRange, setFixPropertyAxisRange, PROPERTY_FIELD_MEMORIZE);
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(FloatType{0}, propertyAxisRangeStart, setPropertyAxisRangeStart, PROPERTY_FIELD_MEMORIZE);
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(FloatType{1}, propertyAxisRangeEnd, setPropertyAxisRangeEnd, PROPERTY_FIELD_MEMORIZE);

    /// The vis element for rendering the 2D/3D output grid.
    DECLARE_MODIFIABLE_REFERENCE_FIELD_FLAGS(OORef<VoxelGridVis>, voxelGridVis, setVoxelGridVis, PROPERTY_FIELD_DONT_PROPAGATE_MESSAGES | PROPERTY_FIELD_MEMORIZE | PROPERTY_FIELD_OPEN_SUBEDITOR);
};

}   // End of namespace
