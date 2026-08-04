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

#include <ovito/stdmod/gui/StdModGui.h>
#include <ovito/stdmod/modifiers/CombineMultipleDatasetsModifier.h>
#include <ovito/gui/desktop/properties/ObjectStatusDisplay.h>
#include <ovito/gui/desktop/properties/RefTargetListParameterUI.h>
#include <ovito/gui/desktop/dialogs/ImportFileDialog.h>
#include <ovito/core/dataset/io/FileSource.h>
#include <ovito/core/dataset/io/FileImporter.h>
#include <ovito/core/dataset/io/FileSourceImporter.h>
#include <ovito/core/app/PluginManager.h>
#include "CombineMultipleDatasetsModifierEditor.h"

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(CombineMultipleDatasetsModifierEditor);
SET_OVITO_OBJECT_EDITOR(CombineMultipleDatasetsModifier, CombineMultipleDatasetsModifierEditor);

/******************************************************************************
* Sets up the UI widgets of the editor.
******************************************************************************/
void CombineMultipleDatasetsModifierEditor::createUI(const RolloutInsertionParameters& rolloutParams)
{
    // Create a rollout.
    QWidget* rollout = createRollout(tr("Combine Multiple Datasets"), rolloutParams, "manual:particles.modifiers.combine_particle_sets");

    // Create the rollout contents.
    QVBoxLayout* layout = new QVBoxLayout(rollout);
    layout->setContentsMargins(4,4,4,4);
    layout->setSpacing(4);

    layout->addWidget(new QLabel(tr("Input files to be merged into the pipeline:")));

    // Container into which the sub-editor of the currently selected source is inserted.
    QWidget* subEditorContainer = new QWidget(rollout);
    QVBoxLayout* subLayout = new QVBoxLayout(subEditorContainer);
    subLayout->setContentsMargins(0,0,0,0);

    // List of secondary sources. Selecting an entry opens its FileSource sub-editor below.
    _sourcesListUI = createParamUI<RefTargetListParameterUI>(PROPERTY_FIELD(CombineMultipleDatasetsModifier::secondaryDataSources),
        RolloutInsertionParameters().insertInto(subEditorContainer));
    layout->addWidget(_sourcesListUI->listWidget(120));

    // Add / Remove buttons.
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->setContentsMargins(0,0,0,0);
    QPushButton* addButton = new QPushButton(tr("Add file..."));
    QPushButton* removeButton = new QPushButton(tr("Remove"));
    buttonLayout->addWidget(addButton);
    buttonLayout->addWidget(removeButton);
    buttonLayout->addStretch(1);
    layout->addLayout(buttonLayout);
    connect(addButton, &QPushButton::clicked, this, &CombineMultipleDatasetsModifierEditor::onAddSource);
    connect(removeButton, &QPushButton::clicked, this, &CombineMultipleDatasetsModifierEditor::onRemoveSource);

    // The sub-editor for the currently selected source is shown here.
    layout->addWidget(subEditorContainer);

    // Status label.
    layout->addSpacing(6);
    layout->addWidget(createParamUI<ObjectStatusDisplay>()->statusWidget());
}

/******************************************************************************
* Opens a file dialog and appends the chosen file as a new secondary source.
******************************************************************************/
void CombineMultipleDatasetsModifierEditor::onAddSource()
{
    CombineMultipleDatasetsModifier* modifier = static_object_cast<CombineMultipleDatasetsModifier>(editObject());
    if(!modifier)
        return;

    QUrl newSourceUrl;
    OvitoClassPtr importerType;
    QString importerFormat;

    // Let the user pick a file. Put code in a block so the dialog is released before importing.
    {
        // Offer only file importer types that are compatible with a FileSource.
        auto importerClasses = PluginManager::instance().metaclassMembers<FileImporter>(FileSourceImporter::OOClass());

        ImportFileDialog dialog(importerClasses, container()->window(), tr("Add input file to be merged"), false);
        if(dialog.exec() != QDialog::Accepted)
            return;

        newSourceUrl = dialog.urlToImport();
        std::tie(importerType, importerFormat) = dialog.selectedFileImporter();
    }

    OORef<FileSource> newFileSource;

    performTransaction(tr("Add dataset"), [&]() {
        // Create the file importer instance.
        OORef<FileImporter> importer;
        if(!importerType) {
            // Auto-detect the file format.
            importer = FileImporter::autodetectFileFormat(newSourceUrl).blockForResult();
            if(!importer)
                throw Exception(tr("Could not detect the format of the file to be imported. The format might not be supported."));
        }
        else {
            importer = static_object_cast<FileImporter>(importerType->createInstance());
            if(!importer)
                return;
            importer->setSelectedFileFormat(importerFormat);
        }

        // The importer must be a FileSourceImporter.
        OORef<FileSourceImporter> fileSourceImporter = dynamic_object_cast<FileSourceImporter>(importer);
        if(!fileSourceImporter)
            throw Exception(tr("The selected file type is not compatible with this modifier."));

        // Create a new file source and load the picked file into it.
        newFileSource = OORef<FileSource>::create();
        newFileSource->setSource({ newSourceUrl }, fileSourceImporter, false);

        // Append the new source to the modifier's list of sources.
        modifier->addDataSource(newFileSource);
    });

    // Select the newly added source so its sub-editor opens for further adjustments.
    if(newFileSource && _sourcesListUI)
        _sourcesListUI->setSelectedObject(newFileSource);
}

/******************************************************************************
* Removes the currently selected secondary source from the list.
******************************************************************************/
void CombineMultipleDatasetsModifierEditor::onRemoveSource()
{
    CombineMultipleDatasetsModifier* modifier = static_object_cast<CombineMultipleDatasetsModifier>(editObject());
    if(!modifier || !_sourcesListUI)
        return;

    RefTarget* selected = _sourcesListUI->selectedObject();
    if(!selected)
        return;

    // Determine the list index of the selected source.
    int index = -1, i = 0;
    for(PipelineNode* source : modifier->secondaryDataSources()) {
        if(source == selected) { index = i; break; }
        i++;
    }
    if(index < 0)
        return;

    performTransaction(tr("Remove dataset"), [&]() {
        modifier->removeDataSourceByIndex(index);
    });
}

}   // End of namespace
