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

namespace Ovito {

/**
 * This dialog box lets the user pick a camera-path CSV file, previews its control points,
 * and hands them back to the caller so they can be applied as camera/target keyframes.
 */
class CameraPathDialog : public QDialog
{
    Q_OBJECT

public:

    /// One control point of the camera trajectory.
    struct ControlPoint {
        int frame;
        Vector3 position;
        Vector3 target;
    };

    /// Constructor.
    explicit CameraPathDialog(QWidget* parent = nullptr);

    /// Returns the parsed control points, sorted by frame, after the dialog has been accepted.
    const std::vector<ControlPoint>& controlPoints() const { return _controlPoints; }

private Q_SLOTS:

    /// Lets the user choose a CSV file.
    void onBrowse();

private:

    /// Parses the given CSV file. Throws Exception on error.
    static std::vector<ControlPoint> parseFile(const QString& filePath);

    /// Re-parses the file at the given path and updates the preview table.
    void loadFile(const QString& filePath);

    /// Rebuilds the preview table from _controlPoints.
    void updatePreviewTable();

    QLineEdit* _filePathEdit;
    QTableWidget* _previewTable;
    QDialogButtonBox* _buttonBox;
    std::vector<ControlPoint> _controlPoints;
};

}   // End of namespace
