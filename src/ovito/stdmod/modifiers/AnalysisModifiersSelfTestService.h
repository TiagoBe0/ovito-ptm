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


#include <ovito/stdmod/StdMod.h>
#include <ovito/core/app/ApplicationService.h>

namespace Ovito {

/**
 * In-process regression tests for the analysis modifiers reimplemented in this fork
 * (TimeSeriesModifier, TimeAveragingModifier, ReducePropertyModifier).
 *
 * Writes a small synthetic LAMMPS dump whose attributes are known in advance, builds a real
 * pipeline (FileSource -> Expression selection -> Time series), evaluates it non-interactively
 * and checks the generated table for: the default frame time axis, a custom time attribute with
 * sub-sampling, a custom frame interval, and a missing attribute (NaN). Changing the modifier
 * parameters between checks also exercises the invalidation of the cached samples.
 *
 * The TimeAveragingModifier test averages an attribute, a per-atom property (atoms are listed in a
 * different order on odd frames, so they must be matched by ID), a histogram table and the cell,
 * in both "create new values" and "replace original values" modes.
 *
 * The ReducePropertyModifier test checks every reduction operation on a vector property, a single
 * component, and the restriction to selected elements (median of an odd count, argmax index into
 * the full list).
 *
 * The DifferenceBetweenFramesModifier test checks attribute, per-atom (matched by ID) and table
 * differences against a fixed and a sliding reference frame, the out-of-range error, and a
 * trajectory in which a particle appears (error by default; zero difference + selection if tolerated).
 *
 * Run with `ovito --nogui --time-series-selftest`, `--time-averaging-selftest`,
 * `--reduce-property-selftest` or `--difference-between-frames-selftest`; all are wired into
 * `ctest` via src/main/CMakeLists.txt.
 */
class OVITO_STDMOD_EXPORT AnalysisModifiersSelfTestService : public ApplicationService
{
    OVITO_CLASS(AnalysisModifiersSelfTestService)

public:

    /// Registers the command-line options handled by this service.
    virtual void registerCommandLineOptions(QCommandLineParser& cmdLineParser) override;

    /// Runs the self-test after the application has entered the main event loop.
    virtual void applicationStarted() override;

private:

    /// Runs the TimeSeriesModifier checks. Returns true iff every check passed.
    static bool runTimeSeriesTest();

    /// Runs the TimeAveragingModifier checks. Returns true iff every check passed.
    static bool runTimeAveragingTest();

    /// Runs the ReducePropertyModifier checks. Returns true iff every check passed.
    static bool runReducePropertyTest();

    /// Runs the DifferenceBetweenFramesModifier checks. Returns true iff every check passed.
    static bool runDifferenceBetweenFramesTest();
};

}   // End of namespace
