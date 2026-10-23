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

#include <ovito/particles/gui/ParticlesGui.h>
#include <ovito/particles/modifier/analysis/surface/ConstructSurfaceModifier.h>
#include <ovito/gui/desktop/properties/BooleanGroupBoxParameterUI.h>
#include <ovito/gui/desktop/properties/IntegerParameterUI.h>
#include <ovito/gui/desktop/properties/IntegerRadioButtonParameterUI.h>
#include <ovito/gui/desktop/properties/FloatParameterUI.h>
#include <ovito/gui/desktop/properties/BooleanParameterUI.h>
#include <ovito/gui/desktop/properties/ObjectStatusDisplay.h>
#include <ovito/gui/desktop/properties/SubObjectParameterUI.h>
#include <ovito/gui/desktop/properties/OpenDataInspectorButton.h>
#include "ConstructSurfaceModifierEditor.h"

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(ConstructSurfaceModifierEditor);
SET_OVITO_OBJECT_EDITOR(ConstructSurfaceModifier, ConstructSurfaceModifierEditor);

/******************************************************************************
* Sets up the UI widgets of the editor.
******************************************************************************/
void ConstructSurfaceModifierEditor::createUI(const RolloutInsertionParameters& rolloutParams)
{
    // Create the rollout.
    QWidget* rollout = createRollout(tr("Construct surface mesh"), rolloutParams, "manual:particles.modifiers.construct_surface_mesh");

    QVBoxLayout* layout = new QVBoxLayout(rollout);
    layout->setContentsMargins(4,4,4,4);
    layout->setSpacing(4);

    QGroupBox* methodGroupBox = new QGroupBox(tr("Method"));
    layout->addWidget(methodGroupBox);

    QGridLayout* sublayout = new QGridLayout(methodGroupBox);
    sublayout->setContentsMargins(4,4,4,4);
    sublayout->setSpacing(6);
    sublayout->setColumnStretch(2, 1);
    sublayout->setColumnMinimumWidth(0, 20);

    int row = 0;

    IntegerRadioButtonParameterUI* methodUI = createParamUI<IntegerRadioButtonParameterUI>(PROPERTY_FIELD(ConstructSurfaceModifier::method));
    QRadioButton* alphaShapeMethodBtn = methodUI->addRadioButton(ConstructSurfaceModifier::AlphaShape, tr("Alpha-shape method:"));
    sublayout->addWidget(alphaShapeMethodBtn, row++, 0, 1, 3);

    FloatParameterUI* probeSphereRadiusUI = createParamUI<FloatParameterUI>(PROPERTY_FIELD(ConstructSurfaceModifier::probeSphereRadius));
    probeSphereRadiusUI->setEnabled(false);
    sublayout->addWidget(probeSphereRadiusUI->label(), row, 1);
    sublayout->addLayout(probeSphereRadiusUI->createFieldLayout(), row++, 2);
    connect(alphaShapeMethodBtn, &QRadioButton::toggled, probeSphereRadiusUI, &FloatParameterUI::setEnabled);

    IntegerParameterUI* smoothingLevelUI = createParamUI<IntegerParameterUI>(PROPERTY_FIELD(ConstructSurfaceModifier::smoothingLevel));
    smoothingLevelUI->setEnabled(false);
    sublayout->addWidget(smoothingLevelUI->label(), row, 1);
    sublayout->addLayout(smoothingLevelUI->createFieldLayout(), row++, 2);
    connect(alphaShapeMethodBtn, &QRadioButton::toggled, smoothingLevelUI, &IntegerParameterUI::setEnabled);

    BooleanParameterUI* selectSurfaceParticlesUI = createParamUI<BooleanParameterUI>(PROPERTY_FIELD(ConstructSurfaceModifier::selectSurfaceParticles));
    selectSurfaceParticlesUI->setEnabled(false);
    sublayout->addWidget(selectSurfaceParticlesUI->checkBox(), row++, 1, 1, 2);
    connect(alphaShapeMethodBtn, &QRadioButton::toggled, selectSurfaceParticlesUI, &BooleanParameterUI::setEnabled);

    QRadioButton* gaussianDensityBtn = methodUI->addRadioButton(ConstructSurfaceModifier::GaussianDensity, tr("Gaussian density method:"));
    sublayout->setRowMinimumHeight(row++, 10);
    sublayout->addWidget(gaussianDensityBtn, row++, 0, 1, 3);

    IntegerParameterUI* gridResolutionUI = createParamUI<IntegerParameterUI>(PROPERTY_FIELD(ConstructSurfaceModifier::gridResolution));
    gridResolutionUI->setEnabled(false);
    sublayout->addWidget(gridResolutionUI->label(), row, 1);
    sublayout->addLayout(gridResolutionUI->createFieldLayout(), row++, 2);
    connect(gaussianDensityBtn, &QRadioButton::toggled, gridResolutionUI, &IntegerParameterUI::setEnabled);

    FloatParameterUI* radiusFactorUI = createParamUI<FloatParameterUI>(PROPERTY_FIELD(ConstructSurfaceModifier::radiusFactor));
    radiusFactorUI->setEnabled(false);
    sublayout->addWidget(radiusFactorUI->label(), row, 1);
    sublayout->addLayout(radiusFactorUI->createFieldLayout(), row++, 2);
    connect(gaussianDensityBtn, &QRadioButton::toggled, radiusFactorUI, &FloatParameterUI::setEnabled);

    FloatParameterUI* isoValueUI = createParamUI<FloatParameterUI>(PROPERTY_FIELD(ConstructSurfaceModifier::isoValue));
    isoValueUI->setEnabled(false);
    sublayout->addWidget(isoValueUI->label(), row, 1);
    sublayout->addLayout(isoValueUI->createFieldLayout(), row++, 2);
    connect(gaussianDensityBtn, &QRadioButton::toggled, isoValueUI, &FloatParameterUI::setEnabled);

    BooleanGroupBoxParameterUI* regionsSettingsUI =
        createParamUI<BooleanGroupBoxParameterUI>(PROPERTY_FIELD(ConstructSurfaceModifier::identifyRegions));
#ifndef OVITO_BUILD_BASIC
    regionsSettingsUI->groupBox()->setTitle(tr("Identify volumetric regions"));
#else
    regionsSettingsUI->groupBox()->setTitle(tr("Identify volumetric regions (requires OVITO Pro)"));
    regionsSettingsUI->setEnabled(false);
#endif
    layout->addWidget(regionsSettingsUI->groupBox());

    sublayout = new QGridLayout(regionsSettingsUI->childContainer());
    sublayout->setContentsMargins(4, 4, 4, 4);
    sublayout->setSpacing(6);
    sublayout->setColumnStretch(1, 1);
    row = 0;

    BooleanParameterUI* mapParticlesToRegionsUI =
        createParamUI<BooleanParameterUI>(PROPERTY_FIELD(ConstructSurfaceModifier::mapParticlesToRegions));
    mapParticlesToRegionsUI->setEnabled(false);
    sublayout->addWidget(mapParticlesToRegionsUI->checkBox(), row++, 1, 1, 2);
#ifndef OVITO_BUILD_BASIC
    auto mapParticlesToRegionsUpdater = [=]() { mapParticlesToRegionsUI->setEnabled(alphaShapeMethodBtn->isChecked()); };
    connect(alphaShapeMethodBtn, &QRadioButton::toggled, mapParticlesToRegionsUI, mapParticlesToRegionsUpdater);
    connect(gaussianDensityBtn, &QRadioButton::toggled, mapParticlesToRegionsUI, mapParticlesToRegionsUpdater);
#endif

    OpenDataInspectorButton* ShowRegionsListBtn =
        new OpenDataInspectorButton(this, tr("List of identified regions"), QStringLiteral("surface"),
                                    2);  // Note: Mode hint "2" is used to switch to the surface mesh regions view.
    ShowRegionsListBtn->setEnabled(false);
    sublayout->addWidget(ShowRegionsListBtn, row++, 1, 1, 2);
#ifndef OVITO_BUILD_BASIC
    connect(this, &PropertiesEditor::contentsChanged, this, [this, ShowRegionsListBtn]() {
        ConstructSurfaceModifier* modifier = static_object_cast<ConstructSurfaceModifier>(editObject());
        ShowRegionsListBtn->setEnabled(modifier && modifier->identifyRegions());
    });
#endif

    QGroupBox* generalGroupBox = new QGroupBox(tr("Options"));
    layout->addWidget(generalGroupBox);
    row = 0;

    sublayout = new QGridLayout(generalGroupBox);
    sublayout->setContentsMargins(4,4,4,4);
    sublayout->setSpacing(6);
    sublayout->setColumnStretch(1, 1);

    BooleanParameterUI* onlySelectedUI = createParamUI<BooleanParameterUI>(PROPERTY_FIELD(ConstructSurfaceModifier::onlySelectedParticles));
    sublayout->addWidget(onlySelectedUI->checkBox(), row++, 0, 1, 2);

    BooleanParameterUI* transferParticlePropertiesUI = createParamUI<BooleanParameterUI>(PROPERTY_FIELD(ConstructSurfaceModifier::transferParticleProperties));
    sublayout->addWidget(transferParticlePropertiesUI->checkBox(), row++, 0, 1, 2);

    BooleanParameterUI* computeSurfaceDistanceUI = createParamUI<BooleanParameterUI>(PROPERTY_FIELD(ConstructSurfaceModifier::computeSurfaceDistance));
    sublayout->addWidget(computeSurfaceDistanceUI->checkBox(), row++, 0, 1, 2);

    // Status label.
    StatusWidget* statusWidget = createParamUI<ObjectStatusDisplay>()->statusWidget();
    layout->addWidget(statusWidget);
    statusWidget->setMinimumHeight(56);

    // Open a sub-editor for the surface mesh vis element.
    createParamUI<SubObjectParameterUI>(PROPERTY_FIELD(ConstructSurfaceModifier::surfaceMeshVis), rolloutParams.after(rollout).setTitle(tr("Surface display")));
}

}   // End of namespace
