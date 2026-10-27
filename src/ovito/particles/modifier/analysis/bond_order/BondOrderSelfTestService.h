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
 * In-process regression test for BondOrderModifier.
 *
 * - 87 molecules (hydrocarbons, heteroaromatics, amides, amines, acids, nitro compounds, ...) with the reference
 *   output of OVITO Pro 3.16.1: penalty score, particle groups, and where no deliberate difference applies, the
 *   integer and fractional bond orders and bond groups. Molecules with carboxylate or nitro groups check the
 *   documented differences instead (no atom above valence 4, group bonds share their total order).
 * - Random small molecules: the branch and bound against exhaustive enumeration of all bond orders.
 * - A polyacene with a zero node budget: the fragment local search, the warning count and the bound by the optimum.
 * - A full pipeline over a LAMMPS data file with type labels (acetanilide): output properties, their types, the
 *   attribute, and the errors for unknown elements and a too small fragment limit.
 *
 * If the environment variable OVITO_BOND_ORDER_REFERENCE names a JSON file with a list of molecules
 * ({"elems": [...], "bonds": [[i, j], ...], "score": s, "bo": [...]}), their penalty scores are compared as well
 * (used offline against ~22000 random molecules evaluated with OVITO Pro).
 *
 * Run with `ovito --nogui --bond-order-selftest`; wired into `ctest` via src/main/CMakeLists.txt.
 */
class OVITO_PARTICLES_EXPORT BondOrderSelfTestService : public ApplicationService
{
    OVITO_CLASS(BondOrderSelfTestService)

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
