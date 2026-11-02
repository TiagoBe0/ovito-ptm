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
#include <ovito/particles/objects/Bonds.h>
#include <ovito/particles/objects/Particles.h>
#include <ovito/stdobj/properties/Property.h>
#include <ovito/stdobj/simcell/SimulationCell.h>
#include <ovito/core/dataset/data/DataVis.h>
#include <ovito/core/rendering/CylinderPrimitive.h>
#include <ovito/core/rendering/SceneRenderer.h>

namespace Ovito {

/**
 * \brief A visualization element for rendering bonds.
 */
class OVITO_PARTICLES_EXPORT BondsVis : public DataVis
{
    OVITO_CLASS(BondsVis)

public:

    /// The shading modes supported by the bonds vis element.
    enum ShadingMode {
        NormalShading = CylinderPrimitive::ShadingMode::NormalShading,
        FlatShading = CylinderPrimitive::ShadingMode::FlatShading
    };
    Q_ENUM(ShadingMode);

    /// The coloring modes supported by the vis element.
    enum ColoringMode {
        UniformColoring,
        ByTypeColoring,
        ParticleBasedColoring
    };
    Q_ENUM(ColoringMode);

    /// Renders the visual element.
    virtual std::variant<PipelineStatus, Future<PipelineStatus>> render(const ConstDataObjectPath& path, const PipelineFlowState& flowState, FrameGraph& frameGraph, const SceneNode* sceneNode) override;

    /// Computes the bounding box of the visual element.
    virtual Box3 boundingBoxImmediate(AnimationTime time, const ConstDataObjectPath& path, const Pipeline* pipeline, const PipelineFlowState& flowState, TimeInterval& validityInterval) override;

    /// Returns the display color used for selected bonds.
    ColorG selectionBondColor() const { return ColorG(1,0,0); }

    /// Determines the display colors of half-bonds.
    /// Returns an array with two colors per full bond, because the two half-bonds may have different colors.
    std::vector<ColorG> halfBondColors(const Particles* particles, bool highlightSelection, ColoringMode coloringMode, bool ignoreBondColorProperty) const;

    /// Determines the bond widths used for rendering.
    ConstPropertyPtr bondWidths(const Bonds* bonds) const;

    /// Name of the bond property whose values select single, double, triple or dashed (fractional) bond rendering.
    /// It is the standard "Bond Order" property of OVITO 3.16, which the fork stores as a user property
    /// (written by the Bond order modifier and the ReaxFF bond importer).
    static constexpr const char* bondOrderPropertyName = "Bond Order";

protected:

    /// Allows the object to parse the serialized contents of a property field in a custom way.
    virtual bool loadPropertyFieldFromStream(ObjectLoadStream& stream, const RefMakerClass::SerializedClassInfo::PropertyFieldInfo& serializedField) override;

protected:

    /// Display width of bonds.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(FloatType{0.4}, bondWidth, setBondWidth, PROPERTY_FIELD_MEMORIZE);

    /// Uniform display color of the bonds.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS((Color{0.6, 0.6, 0.6}), bondColor, setBondColor, PROPERTY_FIELD_MEMORIZE);

    /// Shading mode for bond rendering.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(BondsVis::ShadingMode{NormalShading}, shadingMode, setShadingMode, PROPERTY_FIELD_MEMORIZE);

    /// Determines how the bonds are colored.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(BondsVis::ColoringMode{ParticleBasedColoring}, coloringMode, setColoringMode, PROPERTY_FIELD_MEMORIZE);

    /// Controls whether the bond order property is used to determine the number of cylinders to be rendered per bond.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(bool{true}, visualizeBondOrder, setVisualizeBondOrder);

    /// Number of filled segments for dashed bonds rendering.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(int{4}, filledSegments, setFilledSegments, PROPERTY_FIELD_MEMORIZE | PROPERTY_FIELD_RESETTABLE);

    /// Relative length of filled segments for dashed bonds rendering.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(FloatType{0.5}, filledFraction, setFilledFraction, PROPERTY_FIELD_MEMORIZE | PROPERTY_FIELD_RESETTABLE);
};

/**
 * \brief This information record is attached to the bonds by the BondsVis when rendering
 * them in the viewports. It facilitates the picking of bonds with the mouse.
 */
class OVITO_PARTICLES_EXPORT BondPickInfo : public ObjectPickInfo
{
    OVITO_CLASS(BondPickInfo)

public:

    /// Constructor.
    void initializeObject(DataOORef<const Particles> particles, DataOORef<const SimulationCell> simulationCell, std::vector<int32_t> subobjectToBondMapping = {}) {
        ObjectPickInfo::initializeObject();
        _particles = std::move(particles);
        _simulationCell = std::move(simulationCell);
        _subobjectToBondMapping = std::move(subobjectToBondMapping);
    }

    /// Returns the particles object.
    const DataOORef<const Particles>& particles() const { OVITO_ASSERT(_particles); return _particles; }

    /// Returns the simulation cell.
    const DataOORef<const SimulationCell>& simulationCell() const { return _simulationCell; }

    /// Returns a human-readable string describing the picked object, which will be displayed in the status bar by OVITO.
    virtual QString infoString(const Pipeline* pipeline, uint32_t subobjectId) override;

    /// Given a sub-object ID returned by the Viewport::pick() method, looks up the corresponding bond index.
    size_t bondIndexFromSubObjectID(uint32_t subobjectId) const {
        if(!_subobjectToBondMapping.empty())
            return subobjectId < _subobjectToBondMapping.size() ? (size_t)_subobjectToBondMapping[subobjectId] : std::numeric_limits<size_t>::max();
        return subobjectId / 2;
    }

private:

    /// The particles object.
    DataOORef<const Particles> _particles;

    /// The simulation cell object.
    DataOORef<const SimulationCell> _simulationCell;

    /// Bond index of each rendered cylinder. Empty if every bond is rendered as two half-bond cylinders.
    std::vector<int32_t> _subobjectToBondMapping;
};

}   // End of namespace
