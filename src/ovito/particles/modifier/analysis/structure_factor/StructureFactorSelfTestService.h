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
 * In-process regression test for StructureFactorModifier.
 *
 * Writes a slightly distorted Cu3Au (L1_2) crystal and evaluates both methods with and without partial
 * factors, X-ray form factors and particle selection. The results are compared with values obtained from
 * OVITO Pro 3.16.1 (ovitos) for the same input, sampled at a subset of the k bins. With the environment
 * variable OVITO_SF_DUMP set to a file path, all computed curves are written there as JSON instead.
 *
 * Run with `ovito --nogui --structure-factor-selftest`; wired into `ctest` via src/main/CMakeLists.txt.
 */
class OVITO_PARTICLES_EXPORT StructureFactorSelfTestService : public ApplicationService
{
    OVITO_CLASS(StructureFactorSelfTestService)

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
