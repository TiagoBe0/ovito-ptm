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
 * In-process regression test for the covalent radius mode and the "Discard existing bonds" option of
 * CreateBondsModifier (ported from OVITO 3.16).
 *
 * 80 random atoms of ten species (C, H, N, O, S, P, B, Al, Si, He) in a periodic 9^3 box give 168 candidate
 * pairs, which the maximum-coordination pruning reduces to 81 bonds. The full bond list, including periodic
 * images, is compared with the output of OVITO Pro 3.16.1 for the same file. Also checks the discard option
 * (Pro: 88 bonds without it, 81 with it, after a uniform 1.0 cutoff pass), noble gases (radius 0 in OVITO, so
 * He-He bonds only below 0.4) and the error for type names that are not element symbols.
 *
 * Run with `ovito --nogui --create-bonds-selftest`; wired into `ctest` via src/main/CMakeLists.txt.
 */
class OVITO_PARTICLES_EXPORT CreateBondsSelfTestService : public ApplicationService
{
    OVITO_CLASS(CreateBondsSelfTestService)

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
