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
#include <ovito/mesh/surface/SurfaceMeshVis.h>
#include <ovito/core/dataset/pipeline/Modifier.h>
#include <bitset>

namespace Ovito {

/**
 * \brief Generates a surface mesh from LAMMPS region commands and optionally selects the particles inside the regions.
 *
 * Clean-room implementation written from the user manual and the LAMMPS documentation of the region command, with
 * details settled by comparing the output of OVITO Pro 3.16.1 (black box):
 *
 * - Styles block, cone, cylinder, ellipsoid, plane, prism, sphere; keywords side, open, move, rotate, units (lattice
 *   units are not supported and are ignored with a warning). Lines may be continued with a trailing '&', and '#' starts
 *   a comment. Only literal numbers are accepted, no equal-style variables; NULL in 'move' means zero.
 * - rotate theta Px Py Pz Rx Ry Rz rotates by theta (radians) about the axis R through P; it is applied before move.
 * - INF stands for the cell center -/+ half the cell diagonal, EDGE for the bounding box of the cell. (OVITO Pro 3.16.1
 *   uses the full diagonal for INF in prisms and the length of the cell vectors for EDGE in triclinic cells.)
 * - Spheres are icospheres with 2562 vertices, ellipsoids with 10242 vertices; cones and cylinders have 50 segments; a
 *   plane is the polygon in which it intersects the cell. Faces point to the outside ('side in') or inside ('side out').
 *
 * Outputs the surface mesh "lammps-regions" in a non-periodic copy of the cell with one mesh region per command
 * (region property "Region ID" typed with the region names, and "Color" if colorByRegion is set) and the face
 * property "Region". With selectParticles set, the particle selection is replaced by the union of all regions.
 */
class OVITO_PARTICLES_EXPORT RenderLAMMPSRegionsModifier : public Modifier
{
    /// Give this modifier class its own metaclass.
    class RenderLAMMPSRegionsModifierClass : public ModifierClass
    {
    public:

        /// Inherit constructor from base class.
        using ModifierClass::ModifierClass;

        /// Asks the metaclass whether the modifier can be applied to the given input data.
        virtual bool isApplicableTo(const DataCollection& input) const override;
    };

    OVITO_CLASS_META(RenderLAMMPSRegionsModifier, RenderLAMMPSRegionsModifierClass)

public:

    /// The supported region styles.
    enum class Style { Block, Cone, Cylinder, Ellipsoid, Plane, Prism, Sphere };

    /// A parsed region command. Coordinates are in box units; INF and EDGE are resolved against the cell.
    struct Region {
        QString id;
        Style style;
        int axis = 2;                       ///< Cone and cylinder axis (0, 1, 2).
        std::vector<double> params;         ///< Style arguments after the axis letter, in command order.
        bool sideOut = false;
        std::bitset<6> open;                ///< Open faces (bit i = face i + 1).
        AffineTransformation transform = AffineTransformation::Identity();   ///< Maps region coordinates to the box.
        AffineTransformation inverse = AffineTransformation::Identity();     ///< Maps box coordinates to the region.
    };

    /// Identifier of the generated surface mesh.
    static constexpr const char* meshIdentifier = "lammps-regions";

    /// Parses the region commands. INF and EDGE require a cell. Warnings (e.g. ignored units) are appended to 'warnings'.
    static std::vector<Region> parseCommands(const QString& text, const SimulationCell* cell, QStringList& warnings);

    /// Returns whether a point lies inside the region (taking 'side' into account).
    static bool isInside(const Region& region, const Point3& p);

    /// Constructor.
    void initializeObject(ObjectInitializationFlags flags);

    /// Modifies the input data.
    virtual Future<PipelineFlowState> evaluateModifier(const ModifierEvaluationRequest& request, PipelineFlowState&& state) override;

    /// Replaces any references the modifier has to the given visual element with a new compatible object.
    virtual void replaceVisualElement(DataVis* visElement, const std::function<OORef<DataVis>(const QString&)>& getReplacement) override;

private:

    /// The LAMMPS region commands.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(QStringLiteral("region 1 sphere -20 0 0 15\nregion 2 cylinder z 10 0 5 INF INF"), commands, setCommands);

    /// Assign each region a color from the standard type palette.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(bool{false}, colorByRegion, setColorByRegion);

    /// Select the particles inside any of the regions.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(bool{false}, selectParticles, setSelectParticles);

    /// The visual element for the generated mesh.
    DECLARE_MODIFIABLE_REFERENCE_FIELD_FLAGS(OORef<SurfaceMeshVis>, regionsVis, setRegionsVis, PROPERTY_FIELD_DONT_PROPAGATE_MESSAGES | PROPERTY_FIELD_MEMORIZE | PROPERTY_FIELD_OPEN_SUBEDITOR);
};

}   // End of namespace
