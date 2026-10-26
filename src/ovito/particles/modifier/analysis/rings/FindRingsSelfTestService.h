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
 * In-process regression test for FindRingsModifier.
 *
 * Small graphs with known answers, passed straight to FindRingsModifier::findRings(): two fused hexagons
 * (two 6-rings, not the 10-ring around them, as in the manual's example), a square with a diagonal (two
 * triangles, no 4-ring), a 4-cycle that closes only through a periodic boundary (no ring) versus the same
 * cycle closing inside the cell (one ring), and the size limits. Then full pipelines over a periodic
 * diamond crystal (2 six-rings per atom) and a periodic graphene sheet (1 hexagon per 2 atoms) with bonds
 * from Create bonds, checking the N-rings tables, the histogram, the attribute and the ring mesh.
 *
 * Run with `ovito --nogui --find-rings-selftest`; wired into `ctest` via src/main/CMakeLists.txt.
 */
class OVITO_PARTICLES_EXPORT FindRingsSelfTestService : public ApplicationService
{
    OVITO_CLASS(FindRingsSelfTestService)

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
