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
#include <ovito/gui/desktop/properties/PropertiesEditor.h>

namespace Ovito {

/**
 * \brief The default properties editor used for RefTarget-derived classes if they do not define their own editor type.
 */
class OVITO_GUI_EXPORT DefaultPropertiesEditor : public PropertiesEditor
{
    OVITO_CLASS(DefaultPropertiesEditor)

public:

    /// Returns the editors opened for the referenced sub-objects.
    const std::vector<OORef<PropertiesEditor>>& subEditors() const { return _subEditors; }

protected:

    /// Creates the user interface controls for the editor.
    virtual void createUI(const RolloutInsertionParameters& rolloutParams) override;

    /// This method is called when a reference target changes.
    virtual bool referenceEvent(RefTarget* source, const ReferenceEvent& event) override;

    /// Is called when the value of a reference field of this RefMaker changes.
    virtual void referenceReplaced(const PropertyFieldDescriptor* field, RefTarget* oldTarget, RefTarget* newTarget, int listIndex) override;

private:

    /// Rebuilds the list of sub-editors for the current edit object.
    void updateSubEditors();

    /// The editors for the referenced sub-objects.
    std::vector<OORef<PropertiesEditor>> _subEditors;

    /// Specifies where the sub-editors are opened and whether the sub-editors are opened in a collapsed state.
    RolloutInsertionParameters _rolloutParams;
};

}   // End of namespace
