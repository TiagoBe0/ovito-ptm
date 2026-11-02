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


#include <ovito/crystalanalysis/CrystalAnalysis.h>
#include <ovito/particles/modifier/analysis/binning/BinningLineSource.h>

namespace Ovito {

/**
 * \brief Supplies the segments of a DislocationNetwork, with their spatial Burgers vectors,
 *        to the Dislocations mode of SpatialBinningModifier.
 */
class OVITO_CRYSTALANALYSIS_EXPORT DislocationBinningLineSource : public BinningLineSource
{
    OVITO_CLASS(DislocationBinningLineSource)

public:

    /// Returns whether the data collection contains a dislocation network.
    virtual bool hasLines(const DataCollection& input) const override;

    /// Appends the straight pieces of all dislocation lines to the list.
    virtual void collectSegments(const DataCollection& input, std::vector<Segment>& segments) const override;
};

}   // End of namespace
