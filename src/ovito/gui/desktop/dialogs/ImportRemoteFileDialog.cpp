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
#include <ovito/gui/desktop/dialogs/MessageDialog.h>
#include <ovito/gui/desktop/widgets/general/EnterLineEdit.h>
#include <ovito/gui/base/actions/ActionManager.h>
#include <ovito/core/utilities/io/FileManager.h>
#include <ovito/core/utilities/io/ssh/SshConnection.h>
#include <ovito/core/utilities/io/ssh/openssh/OpensshConnection.h>
#include <ovito/core/utilities/SortZipped.h>
#include "ImportRemoteFileDialog.h"

namespace Ovito {

/******************************************************************************
* Constructs the dialog window.
******************************************************************************/
ImportRemoteFileDialog::ImportRemoteFileDialog(MainWindowUI& ui, const std::vector<const FileImporterClass*>& importerTypes, QWidget* parent, const QString& caption) : QDialog(parent), UserInterfaceComponent<MainWindowUI>(ui)
{
    setWindowTitle(caption);

    QVBoxLayout* layout1 = new QVBoxLayout(this);
    layout1->setSpacing(2);

    layout1->addWidget(new QLabel(tr("Remote URL:")));

    QHBoxLayout* layout2 = new QHBoxLayout();
    layout2->setContentsMargins(0,0,0,0);
    layout2->setSpacing(4);

    _urlEdit = new QComboBox(this);
    _urlEdit->setEditable(true);
    _urlEdit->setInsertPolicy(QComboBox::NoInsert);
    _urlEdit->setMinimumContentsLength(40);
    if(_urlEdit->lineEdit())
        _urlEdit->lineEdit()->setPlaceholderText(tr("sftp://user@hostname/path/file"));

    // Load list of recently accessed URLs.
    QSettings settings;
    settings.beginGroup("file/import_remote_file");
    QStringList list = settings.value("history").toStringList();
    for(QString entry : list)
        _urlEdit->addItem(entry);

    layout2->addWidget(_urlEdit);
    QToolButton* clearURLHistoryButton = new QToolButton();
    clearURLHistoryButton->setIcon(QIcon::fromTheme("edit_clear"));
    clearURLHistoryButton->setToolTip(tr("Clear history"));
    connect(clearURLHistoryButton, &QToolButton::clicked, [this]() {
        if(MessageDialog::question(this, tr("Clear history"),
                                       tr("Do you really want to delete the history of recently used remote URLs? This operation cannot be undone."),
                                       QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Yes) == QMessageBox::Yes) {
            QString text = _urlEdit->currentText();
            _urlEdit->clear();
            _urlEdit->setCurrentText(text);
        }
    });
    layout2->addWidget(clearURLHistoryButton);

    layout1->addLayout(layout2);
    layout1->addSpacing(10);

    layout1->addWidget(new QLabel(tr("File type:")));

    // Build list of file filter strings.
    QStringList fileFilterStrings;
    fileFilterStrings.push_back(tr("<Auto-detect file format>"));
    _importerFormats.emplace_back(nullptr, QString());

    for(const auto& importerClass : importerTypes) {
        for(const FileImporterClass::SupportedFormat& format : importerClass->supportedFormats()) {
            fileFilterStrings << format.description;
            _importerFormats.emplace_back(importerClass, format.identifier);
        }
    }
    // Sort file formats alphabetically (but leave leading <Auto-detect> item in place).
    Ovito::sort_zipped(
        std::span(fileFilterStrings.data(), fileFilterStrings.size()).subspan(1),
        std::span(_importerFormats.data(), _importerFormats.size()).subspan(1),
        [](const QString& a, const QString& b) { return a.compare(b, Qt::CaseInsensitive) < 0; });

    _formatSelector = new QComboBox(this);
    _formatSelector->addItems(fileFilterStrings);
    layout1->addWidget(_formatSelector);
    layout1->addSpacing(10);

    QGroupBox* methodBox = new QGroupBox(tr("SSH connection method:"));
    QGridLayout* layout3 = new QGridLayout(methodBox);
    layout3->setContentsMargins(4,4,4,4);
    layout3->setSpacing(4);
    layout1->addWidget(methodBox);

    layout3->setColumnStretch(1, 1);
    _libsshMethod = new QRadioButton(tr("Integrated client"));
#ifdef OVITO_SSH_CLIENT
    _libsshMethod->setText(_libsshMethod->text() + tr(" (default)"));
    _libsshMethod->setChecked(SshConnection::getSshImplementation() == SshConnection::Libssh);
#else
    _libsshMethod->setText(_libsshMethod->text() + tr(" (not available in this OVITO build)"));
    _libsshMethod->setEnabled(false);
#endif
    layout3->addWidget(_libsshMethod, 0, 0, 1, 3);
#ifndef Q_OS_WIN
#ifndef OVITO_BUILD_BASIC
    _opensshMethod = new QRadioButton(tr("External OpenSSH:"));
    _opensshMethod->setChecked(SshConnection::getSshImplementation() == SshConnection::Openssh);
#else
    _opensshMethod = new QRadioButton(tr("External OpenSSH client (available in OVITO Pro)"));
    _opensshMethod->setEnabled(false);
#endif
#else
    _opensshMethod = new QRadioButton(tr("External OpenSSH client (only available in OVITO Pro for Linux/macOS and Windows WSL)"));
    _opensshMethod->setEnabled(false);
#endif
    layout3->addWidget(_opensshMethod, 1, 0);
#ifndef OVITO_BUILD_BASIC
    _sftpPath = new EnterLineEdit();
    _sftpPath->setText(OpensshConnection::getSftpPath());
    _sftpPath->setPlaceholderText(QStringLiteral("sftp"));
    _sftpPath->setEnabled(_opensshMethod->isChecked());
    layout3->addWidget(_sftpPath, 1, 1);
    connect(_opensshMethod, &QRadioButton::toggled, _sftpPath, &QWidget::setEnabled);

    QPushButton* selectExecutablePathButton = new QPushButton(QStringLiteral("..."));
    connect(selectExecutablePathButton, &QPushButton::clicked, this, [this]() {
        TaskManager::setNativeDialogActive(true);
        QString path = QFileDialog::getOpenFileName(this, tr("Select SFTP Executable"), _sftpPath->text().trimmed());
        TaskManager::setNativeDialogActive(false);
        if(!path.isEmpty())
            _sftpPath->setText(path);
    });
    selectExecutablePathButton->setEnabled(_opensshMethod->isChecked());
    selectExecutablePathButton->setToolTip(tr("Pick sftp executable..."));
    layout3->addWidget(selectExecutablePathButton, 1, 2);
    connect(_opensshMethod, &QRadioButton::toggled, selectExecutablePathButton, &QWidget::setEnabled);
#endif

    layout1->addSpacing(10);

    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Open | QDialogButtonBox::Cancel | QDialogButtonBox::Help, Qt::Horizontal, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &ImportRemoteFileDialog::onOk);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &ImportRemoteFileDialog::reject);
    connect(buttonBox, &QDialogButtonBox::helpRequested, this, &ImportRemoteFileDialog::onHelp);
    layout1->addWidget(buttonBox);
}

/******************************************************************************
* This is called when the user presses the help button of the dialog.
******************************************************************************/
void ImportRemoteFileDialog::onHelp()
{
    actionManager()->openHelpTopic(QStringLiteral("manual:usage.import.remote"));
}

/******************************************************************************
* Sets the current URL in the dialog.
******************************************************************************/
void ImportRemoteFileDialog::selectFile(const QUrl& url)
{
    _urlEdit->setCurrentText(url.toString(QUrl::RemovePassword | QUrl::PreferLocalFile | QUrl::PrettyDecoded));
}

/******************************************************************************
* This is called when the user has pressed the OK button of the dialog.
* Validates and saves all input made by the user and closes the dialog box.
******************************************************************************/
void ImportRemoteFileDialog::onOk()
{
    handleExceptions([&]() {
        QUrl url = QUrl::fromUserInput(_urlEdit->currentText());
        if(!url.isValid())
            throw Exception(tr("The entered URL is invalid."));

#ifndef OVITO_BUILD_BASIC
        if(_libsshMethod->isChecked()) {
            SshConnection::setSshImplementation(SshConnection::Libssh);
        }
        else if(_opensshMethod->isChecked()) {
            QString sftpPath = QDir::fromNativeSeparators(_sftpPath->text().trimmed());
            if(sftpPath.isEmpty())
                sftpPath = QStringLiteral("sftp");
            OpensshConnection::setSftpPath(sftpPath);
            SshConnection::setSshImplementation(SshConnection::Openssh);
        }
#endif

        // Save list of recently accessed URLs.
        QStringList list;
        for(int index = 0; index < _urlEdit->count(); index++) {
            list << _urlEdit->itemText(index);
        }
        QString newEntry = url.toString(QUrl::RemovePassword | QUrl::PreferLocalFile | QUrl::PrettyDecoded);
        list.removeAll(newEntry);
        list.prepend(newEntry);
        while(list.size() > 40)
            list.removeLast();
        QSettings settings;
        settings.beginGroup("file/import_remote_file");
        settings.setValue("history", list);

        // Close dialog box.
        accept();
    });
}

/******************************************************************************
* Returns the file to import after the dialog has been closed with "OK".
******************************************************************************/
QUrl ImportRemoteFileDialog::urlToImport() const
{
    return QUrl::fromUserInput(_urlEdit->currentText());
}

/******************************************************************************
* Returns the selected importer type or NULL if auto-detection is requested.
******************************************************************************/
const std::pair<const FileImporterClass*, QString>& ImportRemoteFileDialog::selectedFileImporter() const
{
    int importFilterIndex = _formatSelector->currentIndex();
    OVITO_ASSERT(importFilterIndex >= -1 && importFilterIndex < _importerFormats.size());
    return _importerFormats[importFilterIndex];
}

}   // End of namespace
