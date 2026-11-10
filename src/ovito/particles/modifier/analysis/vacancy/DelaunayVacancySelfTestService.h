////////////////////////////////////////////////////////////////////////////////////////
//
//  Copyright 2026 OVITO GmbH, Germany
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
 * In-process regression test for DelaunayVacancyModifier.
 *
 * Builds periodic BCC (Fe) and FCC (Cu) crystals with thermal-like random displacements and with a known
 * set of atoms removed: none, one vacancy, a first-neighbor divacancy, and three isolated vacancies.
 * The modifier must report exactly the removed sites, and the reported sites must coincide with them.
 *
 * Run with `ovito --nogui --delaunay-vacancy-selftest`; wired into `ctest` via src/main/CMakeLists.txt.
 *
 * The same service also offers a batch mode for validation against external ground truth:
 * `ovito --nogui --delaunay-vacancy-run DUMP --delaunay-vacancy-out SITES.txt [--delaunay-vacancy-lattice bcc|fcc]`
 * evaluates the modifier on the last frame of DUMP and writes one "x y z" line per detected vacancy.
 */
class OVITO_PARTICLES_EXPORT DelaunayVacancySelfTestService : public ApplicationService
{
    OVITO_CLASS(DelaunayVacancySelfTestService)

public:

    /// Registers the command-line options handled by this service.
    virtual void registerCommandLineOptions(QCommandLineParser& cmdLineParser) override;

    /// Runs the self-test after the application has entered the main event loop.
    virtual void applicationStarted() override;

private:

    /// Runs all checks. Returns true iff every check passed.
    static bool runTest();

    /// Batch mode: runs the modifier on a file and writes the detected sites.
    static void runBatch(const QCommandLineParser& parser);
};

}   // End of namespace
