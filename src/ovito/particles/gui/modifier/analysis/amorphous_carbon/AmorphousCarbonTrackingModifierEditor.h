////////////////////////////////////////////////////////////////////////////////////////
//
//  Copyright 2026 OVITO GmbH, Germany
//
//  This file is part of OVITO (Open Visualization Tool).
//
////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <ovito/particles/gui/ParticlesGui.h>
#include <ovito/gui/desktop/properties/PropertiesEditor.h>

namespace Ovito {

/**
 * A properties editor for the AmorphousCarbonTrackingModifier class.
 */
class AmorphousCarbonTrackingModifierEditor : public PropertiesEditor
{
    OVITO_CLASS(AmorphousCarbonTrackingModifierEditor)
    Q_OBJECT

private Q_SLOTS:

    /// Is called when the object being edited changes.
    void onContentsChanged(RefTarget* editObject);

    /// Is called when the user clicks one of the reference source mode buttons.
    void onSourceButtonClicked(int id);

protected:

    /// Creates the user interface controls for the editor.
    virtual void createUI(const RolloutInsertionParameters& rolloutParams) override;

    QButtonGroup* _sourceButtonGroup;
};

}   // End of namespace
