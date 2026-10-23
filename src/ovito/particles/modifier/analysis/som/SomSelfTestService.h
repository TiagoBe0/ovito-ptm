////////////////////////////////////////////////////////////////////////////////////////
//
//  Copyright 2026 OVITO GmbH, Germany
//
//  This file is part of OVITO (Open Visualization Tool).
//
////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <ovito/particles/Particles.h>
#include <ovito/core/app/ApplicationService.h>

namespace Ovito {

/**
 * In-process regression test for SomAlgorithm (the core of SOMClusteringModifier).
 *
 * SomAlgorithm.h/.cpp are deliberately kept free of any Modifier/PipelineFlowState
 * dependency (see SomAlgorithm.h), specifically so they can be exercised here without
 * building a pipeline or GUI -- only synthetic in-memory feature vectors are used.
 *
 * Two scenarios are checked:
 *   1. A single-layer SOM trained on two well-separated 1D-value groups must assign each
 *      group to a single, group-consistent best-matching unit.
 *   2. A two-layer hierarchy, built from crisply separated per-layer values so the correct
 *      branch at every depth is known in advance, must partition/recurse correctly and
 *      the Godel/Linear id-mapping formulas must match a hand-computed expectation.
 *
 * Run with `ovito --nogui --som-selftest`; wired into `ctest` via src/main/CMakeLists.txt,
 * next to AmorphousCarbonSelfTestService's ADD_TEST().
 */
class OVITO_PARTICLES_EXPORT SomSelfTestService : public ApplicationService
{
    OVITO_CLASS(SomSelfTestService)

public:

    /// Registers the command-line options handled by this service.
    virtual void registerCommandLineOptions(QCommandLineParser& cmdLineParser) override;

    /// Runs the self-test after the application has entered the main event loop.
    virtual void applicationStarted() override;

private:

    /// Runs every check. Returns true iff every check passed.
    static bool runSelfTest();
};

}   // End of namespace
