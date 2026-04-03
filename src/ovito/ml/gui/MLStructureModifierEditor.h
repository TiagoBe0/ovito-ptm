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
#include <QLineEdit>

namespace Ovito {

/**
 * \brief Properties editor panel for MLStructureModifier.
 *
 * Displays:
 *   • Model file browser (.pt).
 *   • Input mode: NeighborDistances or ParticleProperties (with column list).
 *   • Output mode: Classification (argmax → Int32) or Regression (raw float).
 *   • Output property name field.
 *   • Status display.
 */
class MLStructureModifierEditor : public PropertiesEditor
{
    OVITO_CLASS(MLStructureModifierEditor)
    Q_OBJECT

protected:
    virtual void createUI(const RolloutInsertionParameters& rolloutParams) override;

private Q_SLOTS:
    void onInputModeChanged();
    void onOutputModeChanged();
    void updatePropertyList();
    void onPropertyItemChanged(QListWidgetItem* item);

private:
    QGroupBox*   _descParamsBox       = nullptr;
    QGroupBox*   _propSelectBox       = nullptr;
    QListWidget* _propListWidget      = nullptr;
    QGroupBox*   _classInfoBox        = nullptr;
    QLineEdit*   _outPropNameEdit     = nullptr;
    bool         _updatingPropertyList = false;
};

}  // namespace Ovito
