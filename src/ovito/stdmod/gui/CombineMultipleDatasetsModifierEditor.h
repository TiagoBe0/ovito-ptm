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


#include <ovito/stdmod/gui/StdModGui.h>
#include <ovito/gui/desktop/properties/PropertiesEditor.h>

namespace Ovito {

class RefTargetListParameterUI;

/**
 * A properties editor for the CombineMultipleDatasetsModifier class.
 */
class CombineMultipleDatasetsModifierEditor : public PropertiesEditor
{
    OVITO_CLASS(CombineMultipleDatasetsModifierEditor)
    Q_OBJECT

protected:

    /// Creates the user interface controls for the editor.
    virtual void createUI(const RolloutInsertionParameters& rolloutParams) override;

private Q_SLOTS:

    /// Opens a file dialog and appends the chosen file as a new secondary source.
    void onAddSource();

    /// Removes the currently selected secondary source from the list.
    void onRemoveSource();

private:

    /// The list UI showing the secondary data sources.
    RefTargetListParameterUI* _sourcesListUI = nullptr;
};

}   // End of namespace
