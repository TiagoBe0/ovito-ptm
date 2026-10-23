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
 * In-process regression test for CalculateLocalEntropyModifier.
 *
 * Writes a periodic 4x4x4 fcc crystal (a = 4.08, 256 atoms), ideal on frame 0 and with a deterministic
 * sinusoidal displacement of up to 0.05 per component on frame 1, and compares the computed entropies
 * with reference values from an independent NumPy implementation of the same discretization
 * (brute-force minimum-image distances; cutoff 5.3 and averaging cutoff 3.5 stay >= 0.17 away from any
 * pair distance). Covers global and local density, and the neighbor average.
 *
 * Run with `ovito --nogui --local-entropy-selftest`; wired into `ctest` via src/main/CMakeLists.txt.
 */
class OVITO_PARTICLES_EXPORT CalculateLocalEntropySelfTestService : public ApplicationService
{
    OVITO_CLASS(CalculateLocalEntropySelfTestService)

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
