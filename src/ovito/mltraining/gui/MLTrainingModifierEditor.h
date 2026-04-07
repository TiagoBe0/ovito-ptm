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
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>

namespace Ovito {

/**
 * \brief Properties editor panel for MLTrainingModifier.
 *
 * Layout:
 *   [Input features]     Mode selector (neighbour distances / property columns)
 *                        Descriptor params  OR  checkable column list
 *   [Target (label)]     Combo box populated from integer pipeline properties
 *   [Architecture]       Hidden layer sizes
 *   [Training]           Epochs, learning rate, batch size
 *   [Output]             Model output path (.pt)
 *   [Train button]       "Collect from Current Frame & Train"
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

    /// Shows/hides descriptor params vs. column list depending on input mode.
    void onInputModeChanged();

    /// Repopulates the feature-column list from the upstream pipeline state.
    void updatePropertyList();

    /// Commits checked items to the modifier's inputProperties field.
    void onPropertyItemChanged(QListWidgetItem* item);

    /// Repopulates the label-property combo from the upstream pipeline state.
    void updateLabelCombo();

private:
    /// Input-features section: shown only in NeighborDistances mode.
    QGroupBox*   _descParamsBox        = nullptr;

    /// Input-features section: shown only in ParticleProperties mode.
    QGroupBox*   _propSelectBox        = nullptr;

    /// Checkable list of numeric particle properties (feature columns).
    QListWidget* _propListWidget       = nullptr;

    /// Combo box for selecting the target (label) property.
    QComboBox*   _labelCombo           = nullptr;

    /// Guard flag to prevent re-entrant list updates.
    bool         _updatingPropertyList = false;

    /// Guard flag to prevent re-entrant label-combo updates.
    bool         _updatingLabelCombo   = false;

    /// Displays the training status (last result or "Not trained yet").
    QLabel*      _statusLabel          = nullptr;

    /// The "Collect from Current Frame & Train" button.
    QPushButton* _trainButton          = nullptr;

    /// Enables/disables the train button based on whether LibTorch is available.
    void updateTrainButtonState();
};

}  // namespace Ovito
