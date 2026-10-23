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

#include <ovito/core/utilities/Exception.h>
#include "CameraPathDialog.h"

namespace Ovito {

/******************************************************************************
* Constructor.
******************************************************************************/
CameraPathDialog::CameraPathDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Animate Camera Along Path"));

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    QHBoxLayout* fileLayout = new QHBoxLayout();
    fileLayout->addWidget(new QLabel(tr("Trajectory CSV file:")));
    _filePathEdit = new QLineEdit(this);
    _filePathEdit->setReadOnly(true);
    fileLayout->addWidget(_filePathEdit, 1);
    QPushButton* browseButton = new QPushButton(tr("Browse..."), this);
    connect(browseButton, &QPushButton::clicked, this, &CameraPathDialog::onBrowse);
    fileLayout->addWidget(browseButton);
    mainLayout->addLayout(fileLayout);

    QLabel* helpLabel = new QLabel(tr(
        "CSV header: frame,pos_x,pos_y,pos_z,target_x,target_y,target_z\n"
        "One row per control point. The camera position and the point it looks at are "
        "interpolated between control points for every frame of the animation in between."), this);
    helpLabel->setWordWrap(true);
    mainLayout->addWidget(helpLabel);

    _previewTable = new QTableWidget(0, 7, this);
    _previewTable->setHorizontalHeaderLabels({tr("Frame"), tr("Pos X"), tr("Pos Y"), tr("Pos Z"), tr("Target X"), tr("Target Y"), tr("Target Z")});
    _previewTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _previewTable->horizontalHeader()->setStretchLastSection(true);
    mainLayout->addWidget(_previewTable, 1);

    _buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, this);
    connect(_buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    _buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
    mainLayout->addWidget(_buttonBox);

    resize(560, 420);
}

/******************************************************************************
* Lets the user choose a CSV file.
******************************************************************************/
void CameraPathDialog::onBrowse()
{
    QString filePath = QFileDialog::getOpenFileName(this, tr("Select Camera Trajectory File"),
        _filePathEdit->text(), tr("CSV files (*.csv);;All files (*)"));
    if(!filePath.isEmpty())
        loadFile(filePath);
}

/******************************************************************************
* Re-parses the file at the given path and updates the preview table.
******************************************************************************/
void CameraPathDialog::loadFile(const QString& filePath)
{
    try {
        std::vector<ControlPoint> points = parseFile(filePath);
        if(points.size() < 2)
            throw Exception(tr("The trajectory must contain at least two control points (rows)."));
        _controlPoints = std::move(points);
        _filePathEdit->setText(filePath);
        updatePreviewTable();
        _buttonBox->button(QDialogButtonBox::Ok)->setEnabled(true);
    }
    catch(const Exception& ex) {
        _controlPoints.clear();
        _previewTable->setRowCount(0);
        _buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
        QMessageBox::critical(this, tr("Invalid Trajectory File"), ex.message());
    }
}

/******************************************************************************
* Parses the given CSV file. Throws Exception on error.
******************************************************************************/
std::vector<CameraPathDialog::ControlPoint> CameraPathDialog::parseFile(const QString& filePath)
{
    QFile file(filePath);
    if(!file.open(QIODevice::ReadOnly | QIODevice::Text))
        throw Exception(tr("Could not open file '%1' for reading.").arg(filePath));

    QTextStream stream(&file);
    if(stream.atEnd())
        throw Exception(tr("The file is empty."));

    // Parse the header row and determine the column index of each required field.
    QStringList header = stream.readLine().split(QLatin1Char(','));
    static const QStringList requiredColumns = {
        QStringLiteral("frame"),
        QStringLiteral("pos_x"), QStringLiteral("pos_y"), QStringLiteral("pos_z"),
        QStringLiteral("target_x"), QStringLiteral("target_y"), QStringLiteral("target_z")
    };
    QHash<QString, int> columnIndex;
    for(int i = 0; i < header.size(); i++)
        columnIndex.insert(header[i].trimmed().toLower(), i);
    for(const QString& name : requiredColumns) {
        if(!columnIndex.contains(name))
            throw Exception(tr("Missing required column '%1' in the CSV header.").arg(name));
    }

    std::vector<ControlPoint> points;
    int lineNumber = 1;
    while(!stream.atEnd()) {
        lineNumber++;
        QString line = stream.readLine();
        if(line.trimmed().isEmpty())
            continue;
        QStringList fields = line.split(QLatin1Char(','));

        auto fieldAt = [&](const QString& columnName) -> QString {
            int index = columnIndex.value(columnName);
            if(index >= fields.size())
                throw Exception(tr("Line %1: missing value for column '%2'.").arg(lineNumber).arg(columnName));
            return fields[index].trimmed();
        };
        auto floatFieldAt = [&](const QString& columnName) -> FloatType {
            QString text = fieldAt(columnName);
            bool ok;
            FloatType value = (FloatType)text.toDouble(&ok);
            if(!ok)
                throw Exception(tr("Line %1: '%2' is not a valid number for column '%3'.").arg(lineNumber).arg(text, columnName));
            return value;
        };

        ControlPoint point;
        QString frameText = fieldAt(QStringLiteral("frame"));
        bool frameOk;
        point.frame = frameText.toInt(&frameOk);
        if(!frameOk)
            throw Exception(tr("Line %1: '%2' is not a valid integer frame number.").arg(lineNumber).arg(frameText));
        point.position = Vector3(floatFieldAt(QStringLiteral("pos_x")), floatFieldAt(QStringLiteral("pos_y")), floatFieldAt(QStringLiteral("pos_z")));
        point.target = Vector3(floatFieldAt(QStringLiteral("target_x")), floatFieldAt(QStringLiteral("target_y")), floatFieldAt(QStringLiteral("target_z")));
        points.push_back(point);
    }

    std::sort(points.begin(), points.end(), [](const ControlPoint& a, const ControlPoint& b) { return a.frame < b.frame; });
    return points;
}

/******************************************************************************
* Rebuilds the preview table from _controlPoints.
******************************************************************************/
void CameraPathDialog::updatePreviewTable()
{
    _previewTable->setRowCount((int)_controlPoints.size());
    for(int row = 0; row < (int)_controlPoints.size(); row++) {
        const ControlPoint& point = _controlPoints[row];
        int column = 0;
        _previewTable->setItem(row, column++, new QTableWidgetItem(QString::number(point.frame)));
        for(int c = 0; c < 3; c++)
            _previewTable->setItem(row, column++, new QTableWidgetItem(QString::number(point.position[c])));
        for(int c = 0; c < 3; c++)
            _previewTable->setItem(row, column++, new QTableWidgetItem(QString::number(point.target[c])));
    }
}

}   // End of namespace
