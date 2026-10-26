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

namespace Ovito {

/**
 * \brief Finds the rings formed by bonds between particles.
 *
 * Clean-room implementation written from the user manual. A ring is a closed path of bonds without
 * shortcuts: the shortest path between any two of its particles runs along the ring (shortest-path rings,
 * Franzblau, Phys. Rev. B 44, 4925 (1991)). Periodic images are tracked, so a path that closes only by
 * wrapping around a periodic cell is not a ring, and shortcuts are measured between the actual images.
 *
 * Outputs a data table "N-rings" for each ring size N in [minRingSize, maxRingSize], one row per ring with
 * the 0-based indices of its particles in ring order; a ring size histogram ("ring-histogram"); the
 * attribute "FindRings.count"; and a surface mesh "rings" with one polygon per ring and the face property
 * "Ring Size".
 */
class OVITO_PARTICLES_EXPORT FindRingsModifier : public Modifier
{
    /// Give this modifier class its own metaclass.
    class FindRingsModifierClass : public Modifier::OOMetaClass
    {
    public:

        /// Inherit constructor from base metaclass.
        using Modifier::OOMetaClass::OOMetaClass;

        /// Asks the metaclass whether the modifier can be applied to the given input data.
        virtual bool isApplicableTo(const DataCollection& input) const override;
    };

    OVITO_CLASS_META(FindRingsModifier, FindRingsModifierClass)

public:

    /// The largest ring size supported (as in OVITO Pro).
    static constexpr int MaxSupportedRingSize = 36;

    /// Identifier of the ring size histogram table.
    static constexpr const char* histogramTableIdentifier = "ring-histogram";

    /// One ring: particle indices in ring order.
    using Ring = std::vector<size_t>;

    /// Finds all shortest-path rings with min <= size <= max. A bond is (particle 1, particle 2, image shift
    /// of particle 2 relative to particle 1). Rings are returned starting at their lowest particle index,
    /// oriented so that the second index is lower than the last one, sorted by size and then lexicographically.
    static std::vector<Ring> findRings(size_t particleCount, const std::vector<std::tuple<size_t, size_t, Vector3I>>& bonds, int minSize, int maxSize);

    /// Constructor.
    void initializeObject(ObjectInitializationFlags flags);

    /// Is called by the pipeline system before a new modifier evaluation begins.
    virtual void preevaluateModifier(const ModifierEvaluationRequest& request, PipelineEvaluationResult::EvaluationTypes& evaluationTypes, TimeInterval& validityInterval) const override;

    /// Modifies the input data.
    virtual Future<PipelineFlowState> evaluateModifier(const ModifierEvaluationRequest& request, PipelineFlowState&& state) override;

    /// Replaces any references the modifier has to the given visual element with a new compatible object.
    virtual void replaceVisualElement(DataVis* visElement, const std::function<OORef<DataVis>(const QString&)>& getReplacement) override;

private:

    /// Smallest ring size to find (inclusive, at least 3).
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(int{4}, minRingSize, setMinRingSize, PROPERTY_FIELD_MEMORIZE);

    /// Largest ring size to find (inclusive, at most MaxSupportedRingSize).
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(int{8}, maxRingSize, setMaxRingSize, PROPERTY_FIELD_MEMORIZE);

    /// The vis element rendering the ring polygons.
    DECLARE_MODIFIABLE_REFERENCE_FIELD_FLAGS(OORef<SurfaceMeshVis>, ringsVis, setRingsVis, PROPERTY_FIELD_DONT_PROPAGATE_MESSAGES | PROPERTY_FIELD_MEMORIZE | PROPERTY_FIELD_OPEN_SUBEDITOR);
};

}   // End of namespace
