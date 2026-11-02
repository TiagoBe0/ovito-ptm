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
#include <ovito/core/oo/OvitoObject.h>

namespace Ovito {

/**
 * \brief Supplies line segments with a Burgers vector to the Dislocations mode of SpatialBinningModifier.
 *
 * The Particles plugin cannot depend on the plugin that defines the dislocation data objects, so the
 * modifier finds the instantiable subclasses of this class at runtime (PluginManager::metaclassMembers)
 * and uses the first one that recognizes its input. CrystalAnalysis implements it for DislocationNetwork.
 */
class OVITO_PARTICLES_EXPORT BinningLineSource : public OvitoObject
{
    OVITO_CLASS(BinningLineSource)

public:

    /// A straight line segment from a to b (unwrapped Cartesian coordinates) and its spatial Burgers vector.
    struct Segment {
        Point3 a;
        Point3 b;
        Vector3 burgersVector;
    };

    /// Returns whether the data collection contains line data handled by this source.
    virtual bool hasLines(const DataCollection& input) const = 0;

    /// Appends all line segments found in the data collection to the list.
    virtual void collectSegments(const DataCollection& input, std::vector<Segment>& segments) const = 0;
};

}   // End of namespace
