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

#include <ovito/gui/desktop/GUI.h>
#include <ovito/gui/desktop/mainwin/MainWindow.h>
#include <ovito/gui/desktop/app/GuiApplication.h>
#include <ovito/gui/desktop/dialogs/HistoryFileDialog.h>
#include <ovito/gui/desktop/dialogs/ImportFileDialog.h>
#include <ovito/gui/base/mainwin/AvailableModifiersModel.h>
#include <ovito/core/app/Application.h>
#include <ovito/core/dataset/io/FileImporter.h>
#include "GeneralSettingsPage.h"

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(GeneralSettingsPage);

/******************************************************************************
* Creates the widget that contains the plugin specific setting controls.
******************************************************************************/
void GeneralSettingsPage::insertSettingsDialogPage(QTabWidget* tabWidget)
{
    QWidget* page = new QWidget();
    tabWidget->addTab(page, tr("General"));
    QVBoxLayout* layout1 = new QVBoxLayout(page);

    QSettings settings;

    // Group "User interface options":
    QGroupBox* uiGroupBox = new QGroupBox(tr("User interface options"), page);
    layout1->addWidget(uiGroupBox);
    QGridLayout* layout2 = new QGridLayout(uiGroupBox);

    _enableAutomaticDarkMode = new QCheckBox(tr("Enable automatic dark mode"));
    _enableAutomaticDarkMode->setToolTip(tr(
            "<p>Automatically switch between light and dark UI depending on current system color scheme.</p>"));
    layout2->addWidget(_enableAutomaticDarkMode, 0, 0);
    _enableAutomaticDarkMode->setChecked(GuiApplication::automaticallyEnableDarkMode());
#if defined(Q_OS_LINUX) || defined(Q_OS_MACOS)
    _enableAutomaticDarkMode->setEnabled(false);
#else
    #if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        _enableAutomaticDarkMode->setText(_enableAutomaticDarkMode->text() + tr(" (requires application restart to take effect)"));
    #else
        _enableAutomaticDarkMode->setEnabled(false);
    #endif
#endif

    _keepDirHistory = new QCheckBox(tr("Use separate working directories for data import/export and session states"));
    _keepDirHistory->setToolTip(tr(
            "<p>Maintain individual working directories for different types of file I/O operations.</p>"));
    layout2->addWidget(_keepDirHistory, 1, 0);
    _keepDirHistory->setChecked(HistoryFileDialog::keepWorkingDirectoryHistoryEnabled());

    _sortModifiersByCategory = new QCheckBox(tr("Sort list of available modifiers by category"));
    _sortModifiersByCategory->setToolTip(tr("<p>Show a categorized list of available modifiers in the command panel.</p>"));
    layout2->addWidget(_sortModifiersByCategory, 2, 0);
    _sortModifiersByCategory->setChecked(AvailableModifiersModel::useCategoriesGlobal());

    // Group "Data import":
    QGroupBox* importGroupBox = new QGroupBox(tr("Data import options"), page);
    layout1->addWidget(importGroupBox);
    layout2 = new QGridLayout(importGroupBox);
    layout2->setColumnStretch(1, 1);

    layout2->addWidget(new QLabel(tr("Import multiple files of the same type:")), 0, 0);
    _importMultipleFilesBehavior = new QButtonGroup(page);
    QRadioButton* asTrajectoryBtn = new QRadioButton(tr("As trajectory (default)"));
    QRadioButton* asSeparateObjectsBtn = new QRadioButton(tr("As separate objects"));
    _importMultipleFilesBehavior->addButton(asTrajectoryBtn, FileImporter::ImportAsTrajectory);
    _importMultipleFilesBehavior->addButton(asSeparateObjectsBtn, FileImporter::ImportAsSeparateObjects);
    _importMultipleFilesBehavior->button(ImportFileDialog::multiFileImportMode())->setChecked(true);
    layout2->addWidget(asTrajectoryBtn, 0, 1);
    layout2->addWidget(asSeparateObjectsBtn, 1, 1);
#ifdef OVITO_BUILD_BASIC
    asTrajectoryBtn->setEnabled(false);
    asSeparateObjectsBtn->setEnabled(false);
    asSeparateObjectsBtn->setText(asSeparateObjectsBtn->text() + tr(" (requires OVITO Pro)"));
#endif

    // Group "Program updates":
#if !defined(OVITO_BUILD_APPSTORE_VERSION)
    QGroupBox* updateGroupBox = new QGroupBox(tr("Program updates"), page);
    layout1->addWidget(updateGroupBox);
    layout2 = new QGridLayout(updateGroupBox);

    _enableUpdateChecks = new QCheckBox(tr("Periodically check ovito.org website for program updates (and display notice when available)"), updateGroupBox);
    _enableUpdateChecks->setToolTip(tr(
            "<p>The news page is fetched from <i>www.ovito.org</i> on each program startup. "
            "It displays information about new program releases as soon as they become available.</p>"));
    layout2->addWidget(_enableUpdateChecks, 0, 0);

    _enableUpdateChecks->setChecked(settings.value("updates/check_for_updates", true).toBool());
#endif

    layout1->addStretch();
}

/******************************************************************************
* Lets the page save all changed settings.
******************************************************************************/
void GeneralSettingsPage::saveValues(QTabWidget* tabWidget)
{
    QSettings settings;
    HistoryFileDialog::setKeepWorkingDirectoryHistoryEnabled(_keepDirHistory->isChecked());
    AvailableModifiersModel::setUseCategoriesGlobal(_sortModifiersByCategory->isChecked());
#if !(defined(Q_OS_LINUX) || defined(Q_OS_MACOS)) && QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if(_enableAutomaticDarkMode->isChecked())
        settings.setValue("ui/automatic_dark_mode", true);
    else
        settings.remove("ui/automatic_dark_mode");
#endif
#ifndef OVITO_BUILD_BASIC
    ImportFileDialog::setMultiFileImportMode(static_cast<FileImporter::MultiFileImportMode>(_importMultipleFilesBehavior->checkedId()));
#endif

#if !defined(OVITO_BUILD_APPSTORE_VERSION)
    settings.setValue("updates/check_for_updates", _enableUpdateChecks->isChecked());
#endif
}

}   // End of namespace
