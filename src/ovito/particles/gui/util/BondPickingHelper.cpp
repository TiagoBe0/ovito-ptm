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

#include <ovito/particles/gui/ParticlesGui.h>
#include <ovito/particles/objects/BondsVis.h>
#include <ovito/particles/objects/Bonds.h>
#include <ovito/particles/objects/Particles.h>
#include <ovito/core/viewport/Viewport.h>
#include <ovito/core/dataset/DataSet.h>
#include <ovito/core/dataset/scene/Pipeline.h>
#include <ovito/core/dataset/animation/AnimationSettings.h>
#include <ovito/core/viewport/ViewportWindow.h>
#include "BondPickingHelper.h"

namespace Ovito {

/******************************************************************************
* Finds the bond under the mouse cursor.
******************************************************************************/
bool BondPickingHelper::pickBond(ViewportWindow* vpwin, const QPoint& clickPoint, PickResult& result)
{
    // Check if user has clicked on something.
    if(std::optional<ViewportWindow::PickResult> vpPickResult = vpwin->pick(clickPoint)) {

        // Check if that was a bond.
        if(BondPickInfo* pickInfo = dynamic_object_cast<BondPickInfo>(vpPickResult->pickInfo().get())) {
            if(pickInfo->particles()->bonds()) {
                size_t bondIndex = pickInfo->bondIndexFromSubObjectID(vpPickResult->subobjectId());
                const Property* topologyProperty = pickInfo->particles()->bonds()->getTopology();
                if(topologyProperty && topologyProperty->size() > bondIndex) {
                    // Save reference to the selected bond.
                    result.sceneNode = vpPickResult->sceneNode();
                    result.bondIndex = bondIndex;
                    return true;
                }
            }
        }
    }

    result.sceneNode = nullptr;
    return false;
}

}   // End of namespace
