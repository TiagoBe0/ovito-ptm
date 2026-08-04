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
#include <ovito/core/dataset/pipeline/PipelineFlowState.h>

namespace Ovito {

/**
 * In-process regression test for AmorphousCarbonTrackingModifier.
 *
 * There is no C++ test framework anywhere in this fork (see AMORPHOUS_CARBON_MODIFIER.md,
 * "Current scope and limitations"): the tracking modifier's majority-vote fragment-matching logic
 * and its bidirectional cluster genealogy were previously verified only by an unversioned
 * standalone program and by ad-hoc Python scripts respectively. This service closes that gap for
 * the highest-risk code (AmorphousCarbonModifier's own clustering is already cross-checked
 * against a real 473k-atom Python reference; AmorphousCarbonTrackingModifier had nothing) by
 * driving the *actual* modifier class through OVITO's own pipeline machinery -- no reimplemented
 * logic, no file I/O -- against a single hand-built synthetic scenario.
 *
 * The scenario builds two in-memory configurations (reference and current) directly as
 * DataOORef<Particles> objects held by StaticSource pipeline nodes, with "Carbon Cluster
 * Identifier" / "Carbon Cluster Size" / "Carbon Cluster Rank" / "Hybridization" / "Relevant
 * Carbon Cluster" set by hand instead of computed by AmorphousCarbonModifier, so every expected
 * outcome is known in advance. It exercises, in one pass: every value of the "Cluster Fate" and
 * "Reference Cluster Genealogy" / "Cluster Genealogy" enums, the rank-1 aggregate fractions
 * (retained/main-fragment/left-phase/left-core/dispersed/vanished), the majority-vote tie logic's
 * non-tie sibling (a clear 6-vs-4 split), and the mapping counters (unmapped/vanished atoms).
 *
 * Run with `ovito --nogui --carbon-track-selftest`; also wired into `ctest` (see the root
 * CMakeLists.txt) as the first real add_test() in this fork -- ENABLE_TESTING() was present but
 * vestigial (no test had ever been registered).
 */
class OVITO_PARTICLES_EXPORT AmorphousCarbonSelfTestService : public ApplicationService
{
    OVITO_CLASS(AmorphousCarbonSelfTestService)

public:

    /// Registers the command-line options handled by this service.
    virtual void registerCommandLineOptions(QCommandLineParser& cmdLineParser) override;

    /// Runs the self-test after the application has entered the main event loop.
    virtual void applicationStarted() override;

private:

    /// Builds the synthetic scenario, runs it through the real modifier, and checks every
    /// expected outcome. Returns true iff every check passed.
    static bool runSelfTest();
};

}   // End of namespace
