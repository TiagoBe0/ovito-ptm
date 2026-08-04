////////////////////////////////////////////////////////////////////////////////////////
//
//  Copyright 2026 OVITO GmbH, Germany
//
////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <ovito/particles/gui/ParticlesGui.h>
#include <ovito/gui/desktop/properties/PropertiesEditor.h>

namespace Ovito {

class AmorphousCarbonModifierEditor : public PropertiesEditor
{
    OVITO_CLASS(AmorphousCarbonModifierEditor)
    Q_OBJECT

protected:
    virtual void createUI(const RolloutInsertionParameters& rolloutParams) override;
};

} // End of namespace
