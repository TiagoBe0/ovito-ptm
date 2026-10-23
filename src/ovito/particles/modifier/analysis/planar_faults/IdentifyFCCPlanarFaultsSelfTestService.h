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
 * In-process regression test for IdentifyFCCPlanarFaultsModifier.
 *
 * Builds a periodic fcc crystal of 24 close-packed {111} layers with the stacking sequence
 * ABCABCACBACBABACBACACABC, which contains one coherent twin boundary (layer 6), one intrinsic stacking
 * fault (layers 12-13) and one three-layer fault (layers 18-20), each separated by at least 4 fcc layers.
 * Checks the fault type of every atom against its layer, and the ISF and TB areas against the cell
 * cross-section. Runs on the ideal lattice (frame 0) and with random displacements (frame 1), once
 * with PTM and once with adaptive CNA as the structure identification step.
 *
 * Run with `ovito --nogui --planar-faults-selftest`; wired into `ctest` via src/main/CMakeLists.txt.
 */
class OVITO_PARTICLES_EXPORT IdentifyFCCPlanarFaultsSelfTestService : public ApplicationService
{
    OVITO_CLASS(IdentifyFCCPlanarFaultsSelfTestService)

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
