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

#include <ovito/particles/gui/ParticlesGui.h>
#include <ovito/particles/modifier/modify/CreateBondsModifier.h>
#include <ovito/gui/desktop/mainwin/MainWindow.h>
#include <ovito/gui/desktop/mainwin/MainWindowUI.h>
#include <ovito/gui/desktop/mainwin/cmdpanel/CommandPanel.h>
#include <ovito/gui/desktop/mainwin/cmdpanel/ModifyCommandPage.h>
#include <ovito/gui/desktop/properties/PropertiesPanel.h>
#include <ovito/gui/desktop/properties/PropertiesEditor.h>
#include <ovito/gui/desktop/properties/DefaultPropertiesEditor.h>
#include <ovito/gui/base/mainwin/AvailableModifiersModel.h>
#include <ovito/gui/base/mainwin/PipelineListModel.h>
#include <ovito/gui/base/mainwin/PipelineListItem.h>
#include <ovito/stdobj/gui/widgets/DataTablePlotWidget.h>
#include <ovito/core/app/StandaloneApplication.h>
#include <ovito/core/dataset/pipeline/ModificationNode.h>
#include <ovito/core/dataset/pipeline/PipelineEvaluationRequest.h>
#include <ovito/core/dataset/scene/Pipeline.h>
#include <ovito/core/utilities/concurrent/MainThreadOperation.h>
#include "GuiEditorsSelfTestService.h"

#include <QCommandLineParser>
#include <QTextStream>

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(GuiEditorsSelfTestService);

namespace {

/// 64-bit LCG, uniform in [0,1).
struct Random {
    uint64_t state = 123456789;
    double operator()() {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return (double)(state >> 11) / (double)(1ULL << 53);
    }
};

/// Diamond-cubic carbon, 4x4x4 unit cells (512 atoms), three frames with small random displacements.
/// Atom 1 is missing from frames 1 and 2, so that they contain one vacancy relative to frame 0.
void writeCarbonDump(const QString& path)
{
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly | QIODevice::Text))
        throw Exception(QStringLiteral("Cannot write test data file %1").arg(path));
    QTextStream out(&file);
    const double a = 3.567;
    const int n = 4;
    static const double basis[8][3] = {{0,0,0},{0,0.5,0.5},{0.5,0,0.5},{0.5,0.5,0},
                                       {0.25,0.25,0.25},{0.25,0.75,0.75},{0.75,0.25,0.75},{0.75,0.75,0.25}};
    Random random;
    for(int frame = 0; frame < 3; frame++) {
        out << "ITEM: TIMESTEP\n" << frame * 100 << "\n";
        out << "ITEM: NUMBER OF ATOMS\n" << 8 * n * n * n - (frame == 0 ? 0 : 1) << "\n";
        out << "ITEM: BOX BOUNDS pp pp pp\n";
        for(int d = 0; d < 3; d++) out << "0 " << a * n << "\n";
        out << "ITEM: ATOMS id element x y z\n";
        int id = 1;
        for(int i = 0; i < n; i++) for(int j = 0; j < n; j++) for(int k = 0; k < n; k++) for(const auto& b : basis) {
            const double noise = frame == 0 ? 0.0 : 0.05;
            const double x = a * (i + b[0]) + noise * (random() - 0.5);
            const double y = a * (j + b[1]) + noise * (random() - 0.5);
            const double z = a * (k + b[2]) + noise * (random() - 0.5);
            if(id == 1 && frame != 0) {
                id++;
                continue;
            }
            out << QString::asprintf("%d C %.6f %.6f %.6f\n", id++, x, y, z);
        }
    }
}

/// Gaussian cube file (bohr units) with two atoms and a sum of two Gaussian densities on a 20^3 grid.
void writeCube(const QString& path)
{
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly | QIODevice::Text))
        throw Exception(QStringLiteral("Cannot write test data file %1").arg(path));
    QTextStream out(&file);
    const int n = 20;
    const double h = 0.5;
    const double atoms[2][3] = {{3.1, 4.0, 5.2}, {6.3, 5.1, 4.4}};
    out << "GUI editors self-test\nTwo Gaussian densities\n";
    out << "2 0.0 0.0 0.0\n";
    for(int d = 0; d < 3; d++)
        out << n << " " << (d == 0 ? h : 0.0) << " " << (d == 1 ? h : 0.0) << " " << (d == 2 ? h : 0.0) << "\n";
    for(const auto& p : atoms)
        out << QString::asprintf("1 1.0 %.6f %.6f %.6f\n", p[0], p[1], p[2]);
    int column = 0;
    for(int i = 0; i < n; i++) for(int j = 0; j < n; j++) for(int k = 0; k < n; k++) {
        double rho = 0;
        for(const auto& p : atoms) {
            const double dx = i * h - p[0], dy = j * h - p[1], dz = k * h - p[2];
            rho += std::exp(-(dx*dx + dy*dy + dz*dz) / 1.2);
        }
        out << QString::asprintf("%.8e ", rho);
        if(++column % 6 == 0) out << "\n";
    }
    out << "\n";
}

}   // End of anonymous namespace

/******************************************************************************
* Registers the command-line options handled by this service.
******************************************************************************/
void GuiEditorsSelfTestService::registerCommandLineOptions(QCommandLineParser& cmdLineParser)
{
    cmdLineParser.addOption(QCommandLineOption(QStringList{QStringLiteral("gui-editors-selftest")},
        tr("Run the GUI smoke test of the modifier editors on generated data ('particles' or 'grid') and exit "
           "(0 on success, 1 on any failure)."), QStringLiteral("mode")));
}

/******************************************************************************
* Starts the test after the main window has been created.
******************************************************************************/
void GuiEditorsSelfTestService::applicationStarted()
{
    const QCommandLineParser& parser = StandaloneApplication::instance()->cmdLineParser();
    if(!parser.isSet(QStringLiteral("gui-editors-selftest")))
        return;
    _ui = dynamic_object_cast<MainWindowUI>(this_task::ui()).get();
    if(!_ui)
        throw Exception(tr("The GUI editors self-test requires the graphical user interface (do not pass --nogui)."));

    // The widgets are created all the same while the window is hidden. Painting is not needed, and on Qt's
    // offscreen platform flushing the OpenGL viewport widgets crashes inside Qt. Without a visible window,
    // Qt would leave the event loop as soon as the task manager releases its QEventLoopLocker.
    QGuiApplication::setQuitOnLastWindowClosed(false);
    QCoreApplication::setQuitLockEnabled(false);
    _ui->mainWindow()->hide();

    QString dataFile;
    const QString mode = parser.value(QStringLiteral("gui-editors-selftest"));
    if(mode == QStringLiteral("particles")) {
        dataFile = _dir.filePath(QStringLiteral("carbon.dump"));
        writeCarbonDump(dataFile);
        _steps = {
            {"TimeSeriesModifier"}, {"TimeAveragingModifier"}, {"ReducePropertyModifier"},
            {"DifferenceBetweenFramesModifier"}, {"SpatialBinningModifier"},
            {"IdentifyFCCPlanarFaultsModifier", true},      // Requires a structure identification upstream.
            {"CalculateLocalEntropyModifier"}, {"BondLengthDistributionModifier"}, {"BondAngleDistributionModifier"},
            {"FindRingsModifier"}, {"StructureFactorModifier"}, {"ShrinkWrapBoxModifier"},
            {"RenderLAMMPSRegionsModifier"}, {"BondOrderModifier"},
            {"ColorByTypeModifier"}, {"SliceModifier"}, {"ConstructSurfaceModifier"}, {"DislocationAnalysisModifier"},
            {"VacancyAnalysisModifier", false, true},       // Evaluated at the last frame, which has a vacancy.
        };
    }
    else if(mode == QStringLiteral("grid")) {
        dataFile = _dir.filePath(QStringLiteral("density.cube"));
        writeCube(dataFile);
        _steps = { {"BaderChargeIntegrationModifier"}, {"CreateIsosurfaceModifier"} };
    }
    else {
        throw Exception(tr("Unknown mode for --gui-editors-selftest: '%1' (expected 'particles' or 'grid').").arg(mode));
    }

    // Watchdog: a modal dialog (e.g. an unexpected error message box) would block the test forever.
    QTimer::singleShot(std::chrono::minutes(5), qApp, []() {
        qCritical().noquote() << "GUI editors self-test: FAILED, timed out (a modal dialog may be blocking the event loop)";
        std::_Exit(1);
    });

    qInfo().noquote() << "GUI editors self-test: importing" << dataFile;
    _ui->importFiles({QUrl::fromLocalFile(dataFile)});

    _timer.setInterval(50);
    QObject::connect(&_timer, &QTimer::timeout, &_timer, [this]() {
        try {
            tick();
        }
        catch(const Exception& ex) {
            qCritical().noquote() << "GUI editors self-test: FAILED with an exception:" << ex.messages().join(QStringLiteral(" "));
            _failures++;
            finish();
        }
    });
    _timer.start();
}

/******************************************************************************
* Finds the pipeline node holding a modifier of the given class.
******************************************************************************/
ModificationNode* GuiEditorsSelfTestService::findNode(const QString& className) const
{
    Pipeline* pipeline = _ui->mainWindow()->commandPanel()->modifyPage()->pipelineListModel()->selectedPipeline();
    if(!pipeline)
        return nullptr;
    for(PipelineNode* node = pipeline->head(); node; ) {
        ModificationNode* modNode = dynamic_object_cast<ModificationNode>(node);
        if(!modNode)
            break;
        if(modNode->modifier() && modNode->modifier()->getOOClass().name() == className)
            return modNode;
        node = modNode->input();
    }
    return nullptr;
}

/******************************************************************************
* Starts a pipeline evaluation at the current animation time.
******************************************************************************/
void GuiEditorsSelfTestService::startEvaluation()
{
    Pipeline* pipeline = _ui->mainWindow()->commandPanel()->modifyPage()->pipelineListModel()->selectedPipeline();
    MainThreadOperation operation(*_ui, MainThreadOperation::Isolated);
    _evaluation = pipeline->evaluatePipeline(PipelineEvaluationRequest(_ui->datasetContainer().currentAnimationTime()));
}

/******************************************************************************
* Advances the test state machine.
******************************************************************************/
void GuiEditorsSelfTestService::tick()
{
    ModifyCommandPage* page = _ui->mainWindow()->commandPanel()->modifyPage();
    PipelineListModel* pipelineModel = page->pipelineListModel();
    _ticksInPhase++;

    switch(_phase) {
    case Phase::WaitForImport:
        if(!pipelineModel->selectedPipeline()) {
            if(_ticksInPhase > 400)
                throw Exception(tr("The imported data file did not produce a selected pipeline."));
            return;
        }
        if(!_evaluation)
            startEvaluation();
        if(!_evaluation.isFinished())
            return;
        _evaluation.reset();
        _phase = Phase::Setup;
        _ticksInPhase = 0;
        [[fallthrough]];

    case Phase::Setup:
        if(_steps.front().className != QStringLiteral("BaderChargeIntegrationModifier") && !findNode(QStringLiteral("CreateBondsModifier"))) {
            // The bond-based modifiers need bonds. Insert Create bonds first; it stays in the pipeline.
            _steps.insert(_steps.begin(), Step{QStringLiteral("CreateBondsModifier")});
        }
        _phase = Phase::Insert;
        _ticksInPhase = 0;
        return;

    case Phase::Insert:
        insertCurrentModifier();
        return;

    case Phase::WaitForEditor: {
        ModificationNode* node = findNode(_steps[_current].className);
        if(!node)
            throw Exception(tr("Modifier %1 disappeared from the pipeline.").arg(_steps[_current].className));
        if(PropertiesEditor* editor = findEditor(node)) {
            qInfo().noquote() << "GUI editors self-test: inserted" << _steps[_current].className << "with editor" << editor->getOOClass().name();
            beginEvaluation();
        }
        else if(_ticksInPhase > 100) {
            _failures++;
            _report << QStringLiteral("  FAIL  %1 has no properties editor in the command panel").arg(_steps[_current].className, -34);
            qCritical().noquote() << _report.back();
            _phase = Phase::Delete;
            _ticksInPhase = 0;
        }
        return;
    }

    case Phase::Evaluate: {
        ModificationNode* node = findNode(_steps[_current].className);
        if(!node)
            throw Exception(tr("Modifier %1 disappeared from the pipeline.").arg(_steps[_current].className));
        if(!_evaluation)
            startEvaluation();
        if(!_evaluation.isFinished())
            return;
        _evaluation.reset();
        // Trajectory modifiers (time series, time averaging) keep working in the background and update their
        // status when done. Wait until the status text has been the same for 10 evaluations in a row.
        const QString status = QStringLiteral("%1|%2").arg((int)node->status().type()).arg(node->status().text());
        if(status != _lastStatus) {
            _lastStatus = status;
            _stableTicks = 0;
        }
        if(++_stableTicks < 10 && _stepClock.elapsed() < 60000)
            return;

        const Step& step = _steps[_current];
        const PipelineStatus& s = node->status();
        const char* typeName = s.type() == PipelineStatus::Error ? "ERROR" : (s.type() == PipelineStatus::Warning ? "warning" : "ok");
        const bool failed = s.type() == PipelineStatus::Error && !step.errorExpected;
        if(failed) _failures++;
        _report << QStringLiteral("  %1 %2 [%3, %4 ms] %5").arg(failed ? "FAIL" : "PASS", -5).arg(step.className, -34)
            .arg(QString::fromLatin1(typeName)).arg(_stepClock.elapsed()).arg(s.text().simplified().left(160));
        qInfo().noquote() << _report.back();

        // The editor's plots must show the data tables the modifier produced.
        if(step.plotsExpected) {
            for(DataTablePlotWidget* plot : _ui->mainWindow()->commandPanel()->findChildren<DataTablePlotWidget*>()) {
                const DataTable* table = plot->table();
                if(!table || !table->y() || table->y()->size() == 0) {
                    _failures++;
                    _report << QStringLiteral("  FAIL  %1 shows an empty plot").arg(step.className, -34);
                    qCritical().noquote() << _report.back();
                    continue;
                }
                QStringList values;
                table->y()->forAnyType([&](auto _) {
                    using T = decltype(_);
                    BufferReadAccess<T*> y(table->y());
                    for(size_t i = 0; i < y.size(); i++)
                        values << QString::number((double)y.get(i, 0));
                });
                qInfo().noquote() << QStringLiteral("        plot '%1': %2").arg(table->objectTitle(), values.join(QStringLiteral(" ")));
            }
        }

        if(step.className == QStringLiteral("CreateBondsModifier")) {
            _current++;
            _phase = Phase::Insert;
        }
        else {
            _phase = Phase::Delete;
        }
        _ticksInPhase = 0;
        return;
    }

    case Phase::Delete: {
        // Select the modifier's list item and delete it like the user does with the delete button.
        ModificationNode* node = findNode(_steps[_current].className);
        if(node) {
            for(int row = 0; row < pipelineModel->rowCount(); row++) {
                PipelineListItem* item = pipelineModel->item(row);
                if(item && item->object() == node) {
                    pipelineModel->deleteItems({item});
                    break;
                }
            }
            if(findNode(_steps[_current].className)) {
                _failures++;
                _report << QStringLiteral("  FAIL  %1 could not be deleted from the pipeline").arg(_steps[_current].className);
            }
        }
        _current++;
        _phase = Phase::Insert;
        _ticksInPhase = 0;
        return;
    }
    }
}

/******************************************************************************
* Inserts the modifier of the current step and checks its editor.
******************************************************************************/
void GuiEditorsSelfTestService::insertCurrentModifier()
{
    if(_current >= _steps.size()) {
        finish();
        return;
    }
    const Step& step = _steps[_current];
    ModifyCommandPage* page = _ui->mainWindow()->commandPanel()->modifyPage();
    AvailableModifiersModel* modifiersModel = page->availableModifiersModel();

    auto fail = [&](const QString& message) {
        _failures++;
        _report << QStringLiteral("  FAIL  %1 %2").arg(step.className, -34).arg(message);
        qCritical().noquote() << _report.back();
        _current++;
    };

    // Find the menu entry of the modifier.
    ModifierAction* action = nullptr;
    int index = -1;
    for(int row = 0; row < modifiersModel->rowCount(QModelIndex()); row++) {
        ModifierAction* candidate = modifiersModel->actionFromIndex(row);
        if(candidate && candidate->modifierClass() && candidate->modifierClass()->name() == step.className) {
            action = candidate;
            index = row;
            break;
        }
    }
    if(!action)
        return fail(tr("is not listed in the modifier menu"));
    if(!action->isEnabled())
        return fail(tr("is disabled in the modifier menu (not applicable to the input data)"));

    if(step.plotsExpected) {
        // Move to the last animation frame, where the test data differs from the reference frame.
        _ui->datasetContainer().activeAnimationSettings()->jumpToAnimationEnd();
    }

    _stepClock.start();
    modifiersModel->insertModifierByIndex(index);
    ModificationNode* node = findNode(step.className);
    if(!node)
        return fail(tr("was not inserted into the pipeline by its menu action"));

    if(step.className == QStringLiteral("CreateBondsModifier")) {
        // Nearest neighbors of diamond are 1.545 apart, second neighbors 2.52.
        _ui->performTransaction(tr("Set cutoff"), [&]() {
            CreateBondsModifier* createBonds = static_object_cast<CreateBondsModifier>(node->modifier());
            createBonds->setCutoffMode(CreateBondsModifier::UniformCutoff);
            createBonds->setUniformCutoff(1.8);
        });
    }

    // The new modifier gets selected in the pipeline editor, which opens its editor in the command panel.
    _phase = Phase::WaitForEditor;
    _ticksInPhase = 0;
}

/******************************************************************************
* Returns the properties editor of the given modifier shown in the command panel.
******************************************************************************/
PropertiesEditor* GuiEditorsSelfTestService::findEditor(ModificationNode* node) const
{
    for(PropertiesPanel* panel : _ui->mainWindow()->findChildren<PropertiesPanel*>()) {
        PropertiesEditor* editor = panel->editor();
        if(!editor)
            continue;
        if(editor->editObject() == node->modifier())
            return editor;
        if(editor->editObject() == node) {
            // The modification node gets the default editor, which opens the modifier's editor as a sub-editor.
            if(DefaultPropertiesEditor* defaultEditor = dynamic_object_cast<DefaultPropertiesEditor>(editor)) {
                for(const OORef<PropertiesEditor>& subEditor : defaultEditor->subEditors())
                    if(subEditor->editObject() == node->modifier())
                        return subEditor.get();
            }
        }
    }
    return nullptr;
}

/******************************************************************************
* Starts waiting for the evaluation of the inserted modifier.
******************************************************************************/
void GuiEditorsSelfTestService::beginEvaluation()
{
    _phase = Phase::Evaluate;
    _lastStatus.clear();
    _stableTicks = 0;
    _ticksInPhase = 0;
}

/******************************************************************************
* Prints the summary and leaves the event loop.
******************************************************************************/
void GuiEditorsSelfTestService::finish()
{
    _timer.stop();
    qInfo().noquote() << "GUI editors self-test summary:";
    for(const QString& line : _report)
        qInfo().noquote() << line;
    qInfo().noquote() << (_failures == 0 ? "GUI editors self-test: ALL PASSED" : QStringLiteral("GUI editors self-test: %1 FAILURE(S)").arg(_failures));
    QCoreApplication::exit(_failures == 0 ? 0 : 1);
}

}   // End of namespace
