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

#include <ovito/crystalanalysis/CrystalAnalysis.h>
#include <ovito/crystalanalysis/objects/DislocationNetwork.h>
#include "DislocationBinningLineSource.h"

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(DislocationBinningLineSource);

/******************************************************************************
* Returns whether the data collection contains a dislocation network.
******************************************************************************/
bool DislocationBinningLineSource::hasLines(const DataCollection& input) const
{
    return input.containsObject<DislocationNetwork>();
}

/******************************************************************************
* Appends the straight pieces of all dislocation lines to the list.
******************************************************************************/
void DislocationBinningLineSource::collectSegments(const DataCollection& input, std::vector<Segment>& segments) const
{
    const DislocationNetwork* dislocations = input.getObject<DislocationNetwork>();
    if(!dislocations)
        return;
    for(const DislocationSegment* segment : dislocations->segments()) {
        if(segment->isDegenerate())
            continue;
        // The Burgers vector is stored in the coordinate system of the embedding crystal cluster.
        const Vector3 burgersVector = segment->burgersVector.toSpatialVector();
        for(auto p = segment->line.cbegin(), next = std::next(p); next != segment->line.cend(); p = next, ++next)
            segments.push_back({*p, *next, burgersVector});
    }
}

}   // End of namespace
