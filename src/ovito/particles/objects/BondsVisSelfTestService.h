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
 * In-process regression test for the bond order rendering of BondsVis (ported from OVITO 3.16).
 *
 * Renders a planar hexagonal ring whose bonds have the orders 1, 2, 3, 1.5, 0 and 1.5 into a frame graph,
 * without an OpenGL context, and checks the generated cylinders: their number per bond, the mapping from
 * picked cylinders back to bonds, the reduced widths and the spacing of double and triple bonds, the
 * total filled length of dashed bonds (even and odd number of segments), that dashed bonds lie on the
 * inside of the ring, the plain two-cylinder rendering when the option is off, and that bonds added to
 * a Bonds object with a "Bond Order" property become single bonds.
 *
 * Run with `ovito --nogui --bonds-vis-selftest`; wired into `ctest` via src/main/CMakeLists.txt.
 */
class OVITO_PARTICLES_EXPORT BondsVisSelfTestService : public ApplicationService
{
    OVITO_CLASS(BondsVisSelfTestService)

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
