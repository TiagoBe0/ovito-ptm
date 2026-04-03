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
#include <QGroupBox>
#include <QListWidget>

namespace Ovito {

/**
 * \brief Properties editor panel for MLStructureModifier.
 *
 * Displays:
 *   • A file browser button to load a TorchScript (.pt) model.
 *   • Radio buttons to choose between NeighborDistances and ParticleProperties mode.
 *   • In NeighborDistances mode: spinner controls for cutoffRadius and numNeighbors.
 *   • In ParticleProperties mode: a checkable list of available particle property
 *     columns populated from the upstream pipeline state.
 *   • A status display showing modifier errors / warnings.
 */
class MLStructureModifierEditor : public PropertiesEditor
{
    OVITO_CLASS(MLStructureModifierEditor)
    Q_OBJECT

protected:

    /// Builds the rollout widget with all UI controls.
    virtual void createUI(const RolloutInsertionParameters& rolloutParams) override;

private Q_SLOTS:

    /// Switches which parameter group is visible based on the selected input mode.
    void onInputModeChanged();

    /// Repopulates the property list from the current upstream pipeline state.
    void updatePropertyList();

    /// Called when the user checks/unchecks an item in the property list.
    void onPropertyItemChanged(QListWidgetItem* item);

private:

    /// Group box shown in NeighborDistances mode.
    QGroupBox*   _descParamsBox   = nullptr;
    /// Group box shown in ParticleProperties mode.
    QGroupBox*   _propSelectBox   = nullptr;
    /// Checkable list of available particle property columns.
    QListWidget* _propListWidget  = nullptr;

    /// Guards against re-entrant updates when syncing list ↔ modifier field.
    bool _updatingPropertyList = false;
};

}  // namespace Ovito
