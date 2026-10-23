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
#include <ovito/particles/modifier/analysis/binning/SpatialBinningModifier.h>
#include <ovito/particles/objects/Particles.h>
#include <ovito/stdobj/gui/widgets/PropertyReferenceParameterUI.h>
#include <ovito/gui/desktop/properties/BooleanParameterUI.h>
#include <ovito/gui/desktop/properties/IntegerParameterUI.h>
#include <ovito/gui/desktop/properties/FloatParameterUI.h>
#include <ovito/gui/desktop/properties/VariantComboBoxParameterUI.h>
#include <ovito/gui/desktop/properties/ObjectStatusDisplay.h>
#include <ovito/gui/desktop/properties/OpenDataInspectorButton.h>
#include <ovito/gui/desktop/properties/SubObjectParameterUI.h>
#include "SpatialBinningModifierEditor.h"

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(SpatialBinningModifierEditor);
SET_OVITO_OBJECT_EDITOR(SpatialBinningModifier, SpatialBinningModifierEditor);

/******************************************************************************
* Sets up the UI widgets of the editor.
******************************************************************************/
void SpatialBinningModifierEditor::createUI(const RolloutInsertionParameters& rolloutParams)
{
    // Create a rollout.
    QWidget* rollout = createRollout(tr("Spatial binning"), rolloutParams, "manual:particles.modifiers.bin_and_reduce");

    // Create the rollout contents.
    QVBoxLayout* layout = new QVBoxLayout(rollout);
    layout->setContentsMargins(4,4,4,4);
    layout->setSpacing(4);

    // Vector properties can be selected as a whole, in which case every component is binned.
    PropertyReferenceParameterUI* sourcePropertyUI = createParamUI<PropertyReferenceParameterUI>(PROPERTY_FIELD(SpatialBinningModifier::sourceProperty),
        &Particles::OOClass(), PropertyReferenceParameterUI::ShowComponentsAndVectorProperties);
    layout->addWidget(new QLabel(tr("Input property:")));
    layout->addWidget(sourcePropertyUI->comboBox());

    // Without an input property every particle contributes 1 (counting / number density).
    QPushButton* countButton = new QPushButton(tr("Count particles (no input property)"));
    layout->addWidget(countButton);
    connect(countButton, &QPushButton::clicked, this, [this]() {
        if(SpatialBinningModifier* mod = static_object_cast<SpatialBinningModifier>(editObject())) {
            performTransaction(tr("Count particles"), [mod]() {
                mod->setSourceProperty({});
            });
        }
    });

    BooleanParameterUI* onlySelectedUI = createParamUI<BooleanParameterUI>(PROPERTY_FIELD(SpatialBinningModifier::onlySelectedElements));
    layout->addWidget(onlySelectedUI->checkBox());

    VariantComboBoxParameterUI* directionUI = createParamUI<VariantComboBoxParameterUI>(PROPERTY_FIELD(SpatialBinningModifier::binDirection));
    directionUI->comboBox()->addItem(tr("1D: X (1st cell vector)"), QVariant::fromValue((int)SpatialBinningModifier::X));
    directionUI->comboBox()->addItem(tr("1D: Y (2nd cell vector)"), QVariant::fromValue((int)SpatialBinningModifier::Y));
    directionUI->comboBox()->addItem(tr("1D: Z (3rd cell vector)"), QVariant::fromValue((int)SpatialBinningModifier::Z));
    directionUI->comboBox()->addItem(tr("2D: XY"), QVariant::fromValue((int)SpatialBinningModifier::XY));
    directionUI->comboBox()->addItem(tr("2D: XZ"), QVariant::fromValue((int)SpatialBinningModifier::XZ));
    directionUI->comboBox()->addItem(tr("2D: YZ"), QVariant::fromValue((int)SpatialBinningModifier::YZ));
    directionUI->comboBox()->addItem(tr("3D: XYZ"), QVariant::fromValue((int)SpatialBinningModifier::XYZ));
    layout->addWidget(new QLabel(tr("Binning direction(s):")));
    layout->addWidget(directionUI->comboBox());

    QGridLayout* binsLayout = new QGridLayout();
    binsLayout->setContentsMargins(0,0,0,0);
    binsLayout->setColumnStretch(1, 1);
    IntegerParameterUI* binsXUI = createParamUI<IntegerParameterUI>(PROPERTY_FIELD(SpatialBinningModifier::numberOfBinsX));
    IntegerParameterUI* binsYUI = createParamUI<IntegerParameterUI>(PROPERTY_FIELD(SpatialBinningModifier::numberOfBinsY));
    IntegerParameterUI* binsZUI = createParamUI<IntegerParameterUI>(PROPERTY_FIELD(SpatialBinningModifier::numberOfBinsZ));
    binsLayout->addWidget(binsXUI->label(), 0, 0);
    binsLayout->addLayout(binsXUI->createFieldLayout(), 0, 1);
    binsLayout->addWidget(binsYUI->label(), 1, 0);
    binsLayout->addLayout(binsYUI->createFieldLayout(), 1, 1);
    binsLayout->addWidget(binsZUI->label(), 2, 0);
    binsLayout->addLayout(binsZUI->createFieldLayout(), 2, 1);
    layout->addLayout(binsLayout);

    VariantComboBoxParameterUI* operationUI = createParamUI<VariantComboBoxParameterUI>(PROPERTY_FIELD(SpatialBinningModifier::reductionOperation));
    operationUI->comboBox()->addItem(tr("Mean"), QVariant::fromValue((int)SpatialBinningModifier::Mean));
    operationUI->comboBox()->addItem(tr("Sum"), QVariant::fromValue((int)SpatialBinningModifier::Sum));
    operationUI->comboBox()->addItem(tr("Sum divided by bin volume"), QVariant::fromValue((int)SpatialBinningModifier::SumVol));
    operationUI->comboBox()->addItem(tr("Minimum"), QVariant::fromValue((int)SpatialBinningModifier::Min));
    operationUI->comboBox()->addItem(tr("Maximum"), QVariant::fromValue((int)SpatialBinningModifier::Max));
    layout->addWidget(new QLabel(tr("Reduction operation:")));
    layout->addWidget(operationUI->comboBox());

    BooleanParameterUI* derivativeUI = createParamUI<BooleanParameterUI>(PROPERTY_FIELD(SpatialBinningModifier::firstDerivative));
    layout->addWidget(derivativeUI->checkBox());

    // Color range of the 2D/3D voxel grid.
    BooleanParameterUI* fixRangeUI = createParamUI<BooleanParameterUI>(PROPERTY_FIELD(SpatialBinningModifier::fixPropertyAxisRange));
    layout->addWidget(fixRangeUI->checkBox());
    QHBoxLayout* rangeLayout = new QHBoxLayout();
    rangeLayout->setContentsMargins(0,0,0,0);
    FloatParameterUI* rangeStartUI = createParamUI<FloatParameterUI>(PROPERTY_FIELD(SpatialBinningModifier::propertyAxisRangeStart));
    FloatParameterUI* rangeEndUI = createParamUI<FloatParameterUI>(PROPERTY_FIELD(SpatialBinningModifier::propertyAxisRangeEnd));
    rangeLayout->addWidget(new QLabel(tr("From:")));
    rangeLayout->addLayout(rangeStartUI->createFieldLayout());
    rangeLayout->addSpacing(12);
    rangeLayout->addWidget(new QLabel(tr("To:")));
    rangeLayout->addLayout(rangeEndUI->createFieldLayout());
    layout->addLayout(rangeLayout);
    rangeStartUI->setEnabled(false);
    rangeEndUI->setEnabled(false);
    connect(fixRangeUI->checkBox(), &QCheckBox::toggled, rangeStartUI, &FloatParameterUI::setEnabled);
    connect(fixRangeUI->checkBox(), &QCheckBox::toggled, rangeEndUI, &FloatParameterUI::setEnabled);

    // Enable only the controls that apply to the current binning direction.
    auto updateEnabled = [=](RefTarget* editObject) {
        SpatialBinningModifier* mod = static_object_cast<SpatialBinningModifier>(editObject);
        if(!mod) return;
        binsXUI->setEnabled(SpatialBinningModifier::isBinned(mod->binDirection(), 0));
        binsYUI->setEnabled(SpatialBinningModifier::isBinned(mod->binDirection(), 1));
        binsZUI->setEnabled(SpatialBinningModifier::isBinned(mod->binDirection(), 2));
        derivativeUI->setEnabled(SpatialBinningModifier::dimensionality(mod->binDirection()) == 1);
    };
    connect(this, &PropertiesEditor::contentsChanged, this, updateEnabled);
    connect(this, &PropertiesEditor::contentsReplaced, this, updateEnabled);

    OpenDataInspectorButton* openDataInspectorBtn = new OpenDataInspectorButton(this, tr("Show in data inspector"));
    layout->addWidget(openDataInspectorBtn);

    // Status label.
    layout->addSpacing(6);
    layout->addWidget(createParamUI<ObjectStatusDisplay>()->statusWidget());

    // Open a sub-editor for the voxel grid vis element.
    createParamUI<SubObjectParameterUI>(PROPERTY_FIELD(SpatialBinningModifier::voxelGridVis), rolloutParams.after(rollout));
}

}   // End of namespace
