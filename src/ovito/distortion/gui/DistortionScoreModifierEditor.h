////////////////////////////////////////////////////////////////////////////////////////
//
//  Copyright 2025 OVITO GmbH, Germany
//
//  This file is part of OVITO (Open Visualization Tool).
//
//  OVITO is free software; you can redistribute it and/or modify it either under the
//  terms of the GNU General Public License version 3 as published by the Free Software
//  Foundation (the "GPL") or, at your option, under the terms of the MIT License.
//
////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <ovito/gui/desktop/properties/PropertiesEditor.h>
#include <QLabel>
#include <QPushButton>

namespace Ovito {

/**
 * \brief Properties editor panel for DistortionScoreModifier.
 *
 * Layout:
 *   [Descriptor]   Cutoff radius, max neighbours
 *   [MCD training] Contamination ν, regularisation, C-steps
 *   [Model file]   Path to the JSON model file
 *   [Train button] "Train on current frame"  — triggers FAST-MCD training
 *   [Inference]    Defect threshold
 *   [Status]       Last training result / error
 */
class DistortionScoreModifierEditor : public PropertiesEditor
{
    OVITO_CLASS(DistortionScoreModifierEditor)
    Q_OBJECT

protected:
    virtual void createUI(const RolloutInsertionParameters& rolloutParams) override;

private Q_SLOTS:
    /// Triggered when the user clicks the "Train on current frame" button.
    void onTrainClicked();

private:
    /// "Train on current frame" button.
    QPushButton* _trainButton   = nullptr;

    /// Status label shown below the train button.
    QLabel*      _statusLabel   = nullptr;
};

}  // namespace Ovito
