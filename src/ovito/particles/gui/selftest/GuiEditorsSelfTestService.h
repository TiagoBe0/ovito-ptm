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


#include <ovito/particles/gui/ParticlesGui.h>
#include <ovito/gui/desktop/app/GuiApplicationService.h>
#include <ovito/core/dataset/pipeline/PipelineFlowState.h>
#include <ovito/core/utilities/concurrent/SharedFuture.h>

#include <QTemporaryDir>
#include <QTimer>

namespace Ovito {

class MainWindowUI;
class Pipeline;
class ModificationNode;
class PropertiesEditor;

/**
 * GUI smoke test for the modifiers ported from OVITO Pro (and the Pro features that were unlocked).
 *
 * Imports a generated data file into the main window, then, one modifier at a time, inserts the modifier
 * through the same menu action the user triggers, checks that the properties editor of the modifier gets
 * created in the command panel, waits for the pipeline evaluation to settle, records the modifier status
 * and deletes the modifier again.
 *
 *   --gui-editors-selftest particles   diamond-cubic carbon trajectory (3 frames) with bonds
 *   --gui-editors-selftest grid        Gaussian cube file with a charge density (Bader, isosurface)
 *
 * Run with `QT_QPA_PLATFORM=offscreen ovito --gui-editors-selftest <mode>`; wired into ctest, which also points
 * XDG_CONFIG_HOME to a private directory so that parameters memorized in the user's Ovito.conf do not interfere.
 * Exits with 0 when every modifier was found in the menu, got its editor and did not end in an error.
 */
class GuiEditorsSelfTestService : public GuiApplicationService
{
    OVITO_CLASS(GuiEditorsSelfTestService)

public:

    /// Registers the command-line options handled by this service.
    virtual void registerCommandLineOptions(QCommandLineParser& cmdLineParser) override;

    /// Starts the test after the main window has been created.
    virtual void applicationStarted() override;

private:

    struct Step {
        QString className;
        bool errorExpected = false;     ///< The modifier cannot work on the test data (e.g. needs FCC); an error status is fine.
        bool plotsExpected = false;     ///< Every plot in the editor must show a non-empty data table.
    };

    /// Advances the test state machine; called periodically by the timer.
    void tick();

    /// Inserts the modifier of the current step and checks its editor.
    void insertCurrentModifier();

    /// Switches to the evaluation phase once the editor of the inserted modifier is up.
    void beginEvaluation();

    /// Returns the properties editor of the given modifier shown in the command panel, if any.
    PropertiesEditor* findEditor(ModificationNode* node) const;

    /// Finds the pipeline node holding a modifier of the given class.
    ModificationNode* findNode(const QString& className) const;

    /// Starts a pipeline evaluation at the current animation time.
    void startEvaluation();

    /// Prints the summary and leaves the event loop.
    void finish();

    MainWindowUI* _ui = nullptr;
    QTemporaryDir _dir;
    QTimer _timer;
    std::vector<Step> _steps;
    size_t _current = 0;
    enum class Phase { WaitForImport, Setup, Insert, WaitForEditor, Evaluate, Delete } _phase = Phase::WaitForImport;
    SharedFuture<PipelineFlowState> _evaluation;
    QString _lastStatus;
    int _stableTicks = 0;
    int _ticksInPhase = 0;
    QElapsedTimer _stepClock;
    QStringList _report;
    int _failures = 0;
};

}   // End of namespace
