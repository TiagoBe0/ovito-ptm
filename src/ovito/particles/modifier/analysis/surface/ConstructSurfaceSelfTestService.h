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
#include <ovito/core/app/ApplicationService.h>

namespace Ovito {

/**
 * In-process regression test for the region analysis of ConstructSurfaceModifier (the
 * "Identify volumetric regions" option, which only OVITO Pro exposes in its GUI).
 *
 * Writes an 8x8x8 fcc crystal (a = 4.08) with two spherical pores of radius 5 and builds the surface
 * with the alpha-shape and the Gaussian density method. Checks the ConstructSurfaceMesh.* global
 * attributes in periodic and open cells: region counts (1 filled, 2 voids), consistency of volumes,
 * fractions and specific surface area, pore volumes within geometric bounds, and the per-particle region
 * mapping. For open cells, the empty volume must count only the part of the exterior inside the cell:
 * checked against a closed-form case (a cell cutting the crystal) and by additivity when a cell is
 * split in two, which exercises the exact computation of filled volume sticking out of the cell.
 *
 * Run with `ovito --nogui --construct-surface-selftest`; wired into `ctest` via src/main/CMakeLists.txt.
 */
class OVITO_PARTICLES_EXPORT ConstructSurfaceSelfTestService : public ApplicationService
{
    OVITO_CLASS(ConstructSurfaceSelfTestService)

public:

    /// Registers the command-line options handled by this service.
    virtual void registerCommandLineOptions(QCommandLineParser& cmdLineParser) override;

    /// Runs the self-test after the application has entered the main event loop.
    virtual void applicationStarted() override;

private:

    /// Runs all checks. Returns true iff every check passed.
    static bool runTest();
};

}   // End of namespace
