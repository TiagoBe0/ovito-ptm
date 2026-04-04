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

#include <ovito/gui/desktop/properties/PropertiesEditor.h>
#include <QLabel>
#include <QPushButton>

namespace Ovito {

/**
 * \brief Properties editor panel for MLTrainingModifier.
 *
 * Layout:
 *   [Input descriptor]   Cutoff radius, max neighbours, label property
 *   [Architecture]       Hidden layer sizes
 *   [Training]           Epochs, learning rate, batch size
 *   [Output]             Model output path (.pt)
 *   [Train button]       "Collect from All Frames & Train"
 *   [Status label]       Shows last training result
 */
class MLTrainingModifierEditor : public PropertiesEditor
{
    OVITO_CLASS(MLTrainingModifierEditor)
    Q_OBJECT

protected:
    virtual void createUI(const RolloutInsertionParameters& rolloutParams) override;

private Q_SLOTS:
    /// Triggered when the user clicks the "Collect & Train" button.
    void onTrainClicked();

private:
    /// Displays the training status (last result or "Not trained yet").
    QLabel*      _statusLabel  = nullptr;

    /// The "Collect from All Frames & Train" button.
    QPushButton* _trainButton  = nullptr;

    /// Enables/disables the train button based on whether LibTorch is available.
    void updateTrainButtonState();
};

}  // namespace Ovito
