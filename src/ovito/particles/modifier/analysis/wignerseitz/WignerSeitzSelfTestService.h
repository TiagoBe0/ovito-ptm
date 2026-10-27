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
 * In-process regression test for WignerSeitzAnalysisModifier.
 *
 * Builds a periodic 4x4x4 fcc crystal (frame 0) and the same crystal with small random displacements and
 * one atom moved to an octahedral interstitial site (frame 1). Against frame 0 as reference, frame 0 must
 * give no defects and frame 1 exactly one Frenkel pair, with the standard and with the adaptive cell.
 * The adaptive case on frame 0 guards against the power-diagram pass skipping a site that coincides
 * with the atom (zero distance), which reassigned every atom to a neighboring site.
 *
 * Run with `ovito --nogui --wigner-seitz-selftest`; wired into `ctest` via src/main/CMakeLists.txt.
 */
class OVITO_PARTICLES_EXPORT WignerSeitzSelfTestService : public ApplicationService
{
    OVITO_CLASS(WignerSeitzSelfTestService)

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
