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
 * In-process regression test for BondAnalysisModifier.
 *
 * Writes a periodic LAMMPS data file with one central atom (type 1) bonded to three atoms: along +x
 * (length 1.1, type 2 atom, bond type 1), along +y (length 2.2, type 2 atom, bond type 2) and along -x
 * through the periodic boundary (length 1.6, type 1 atom, bond type 1). The bond angles at the center
 * are 90, 90 and 180 degrees. With 9 bins and a length cutoff of 2.7 no value falls on a bin edge, so
 * the expected histograms are exact. Covers all partitioning modes, cosine mode and the length cutoff.
 *
 * Run with `ovito --nogui --bond-analysis-selftest`; wired into `ctest` via src/main/CMakeLists.txt.
 */
class OVITO_PARTICLES_EXPORT BondAnalysisSelfTestService : public ApplicationService
{
    OVITO_CLASS(BondAnalysisSelfTestService)

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
