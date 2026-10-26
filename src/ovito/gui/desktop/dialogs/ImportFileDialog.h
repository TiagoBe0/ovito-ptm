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


#include <ovito/gui/desktop/GUI.h>
#include <ovito/core/dataset/io/FileImporter.h>
#include "HistoryFileDialog.h"

namespace Ovito {

/**
 * This file chooser dialog lets the user select a file to be imported.
 */
class OVITO_GUI_EXPORT ImportFileDialog : public HistoryFileDialog
{
    Q_OBJECT

public:

    /// Returns what should happen if the user imports several files of the same kind.
    static FileImporter::MultiFileImportMode multiFileImportMode() {
#ifndef OVITO_BUILD_BASIC
        return QSettings().value("file/multi_file_import_mode", FileImporter::ImportAsTrajectory).value<FileImporter::MultiFileImportMode>();
#else
        return FileImporter::ImportAsTrajectory;
#endif
    }

    /// Sets what should happen if the user imports several files of the same kind.
    static void setMultiFileImportMode(FileImporter::MultiFileImportMode mode) {
        QSettings settings;
        if(mode != FileImporter::ImportAsTrajectory) settings.setValue("file/multi_file_import_mode", mode);
        else settings.remove("file/multi_file_import_mode");
    }

public:

    /// Constructor.
    ImportFileDialog(const std::vector<const FileImporterClass*>& importerTypes, QWidget* parent, const QString& caption, bool allowMultiSelection, const QString& dialogClass = QStringLiteral("import"));

    /// Returns the file to import after the dialog has been closed with "OK".
    QString fileToImport() const;

    /// Returns the file to import after the dialog has been closed with "OK".
    QUrl urlToImport() const;

    /// Returns the list of files to import after the dialog has been closed with "OK".
    std::vector<QUrl> urlsToImport() const;

    /// Returns the selected importer class and sub-format name.
    const std::pair<const FileImporterClass*, QString>& selectedFileImporter() const;

private:

    std::vector<std::pair<const FileImporterClass*, QString>> _importerFormats;
};

}   // End of namespace
