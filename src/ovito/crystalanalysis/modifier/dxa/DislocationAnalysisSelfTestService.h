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


#include <ovito/crystalanalysis/CrystalAnalysis.h>
#include <ovito/core/app/ApplicationService.h>

namespace Ovito {

/**
 * In-process regression test for DislocationAnalysisModifier (DXA), run once with CNA and once with PTM
 * as the structure identification step.
 *
 * Cases: a straight a/2<110> screw dislocation (isotropic elastic displacement field, free surfaces in x and y,
 * periodic along the line) in fcc gold and in cubic diamond silicon, which must give one dislocation of the
 * cell's length; and perfect periodic cubic and hexagonal diamond crystals, which must be fully crystalline
 * with no dislocation lines. The diamond cases guard the mapping between PTM's template neighbor order and
 * DXA's lattice vectors.
 *
 * Run with `ovito --nogui --dxa-selftest`; wired into `ctest` via src/main/CMakeLists.txt.
 */
class OVITO_CRYSTALANALYSIS_EXPORT DislocationAnalysisSelfTestService : public ApplicationService
{
    OVITO_CLASS(DislocationAnalysisSelfTestService)

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
