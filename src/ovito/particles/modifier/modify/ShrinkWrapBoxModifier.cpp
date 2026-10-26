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
#include <ovito/particles/objects/Particles.h>
#include <ovito/stdobj/simcell/SimulationCell.h>
#include <ovito/core/dataset/pipeline/ModificationNode.h>
#include "ShrinkWrapBoxModifier.h"

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(ShrinkWrapBoxModifier);
OVITO_CLASSINFO(ShrinkWrapBoxModifier, "DisplayName", "Shrink-wrap simulation box");
OVITO_CLASSINFO(ShrinkWrapBoxModifier, "Description", "Fit the simulation cell tightly to the bounding box of the particles.");
OVITO_CLASSINFO(ShrinkWrapBoxModifier, "ModifierCategory", "Modification");

/******************************************************************************
* Asks the modifier whether it can be applied to the given input data.
******************************************************************************/
bool ShrinkWrapBoxModifier::OOMetaClass::isApplicableTo(const DataCollection& input) const
{
    return input.containsObject<Particles>();
}

/******************************************************************************
* Modifies the input data.
******************************************************************************/
Future<PipelineFlowState> ShrinkWrapBoxModifier::evaluateModifier(const ModifierEvaluationRequest& request, PipelineFlowState&& state)
{
    const Particles* particles = state.expectObject<Particles>();
    particles->verifyIntegrity();
    if(particles->elementCount() == 0) {
        state.setStatus(PipelineStatus(PipelineStatus::Warning, tr("There are no particles; the simulation cell is left unchanged.")));
        return std::move(state);
    }

    // Axis-aligned bounding box of the particle coordinates.
    Box3 bbox;
    bbox.addPoints(BufferReadAccess<Point3>(particles->expectProperty(Particles::PositionProperty)));

    SimulationCell* cell = state.getMutableObject<SimulationCell>();
    if(!cell) {
        // Create a non-periodic cell if the input has none.
        cell = state.createObject<SimulationCell>(request.modificationNode(), AffineTransformation::Identity(), false, false, false);
    }

    AffineTransformation matrix = cell->cellMatrix();
    const int dims = cell->is2D() ? 2 : 3;
    for(int d = 0; d < 3; d++) {
        if(d < dims) {
            matrix.column(d) = Vector3::Zero();
            matrix(d, d) = bbox.maxc[d] - bbox.minc[d];
            matrix(d, 3) = bbox.minc[d];
        }
        else {
            // 2D cell: keep the z extent, but make the in-plane vectors orthogonal to it.
            matrix(0, d) = matrix(1, d) = 0;
        }
    }
    if(dims == 2)
        matrix(2, 0) = matrix(2, 1) = 0;
    cell->setCellMatrix(matrix);

    state.setStatus(PipelineStatus(PipelineStatus::Success, tr("Cell size: %1 x %2 x %3").arg(matrix(0, 0)).arg(matrix(1, 1)).arg(matrix(2, 2))));
    return std::move(state);
}

}   // End of namespace
