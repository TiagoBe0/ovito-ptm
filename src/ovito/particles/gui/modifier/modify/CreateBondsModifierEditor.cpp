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
#include <ovito/particles/modifier/modify/CreateBondsModifier.h>
#include <ovito/particles/objects/ParticleType.h>
#include <ovito/gui/desktop/properties/IntegerRadioButtonParameterUI.h>
#include <ovito/gui/desktop/properties/FloatParameterUI.h>
#include <ovito/gui/desktop/properties/BooleanParameterUI.h>
#include <ovito/gui/desktop/properties/SubObjectParameterUI.h>
#include <ovito/gui/desktop/properties/ObjectStatusDisplay.h>
#include "CreateBondsModifierEditor.h"

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(CreateBondsModifierEditor);
SET_OVITO_OBJECT_EDITOR(CreateBondsModifier, CreateBondsModifierEditor);

/******************************************************************************
* Sets up the UI widgets of the editor.
******************************************************************************/
void CreateBondsModifierEditor::createUI(const RolloutInsertionParameters& rolloutParams)
{
    // Create a rollout.
    QWidget* rollout = createRollout(tr("Create bonds"), rolloutParams, "manual:particles.modifiers.create_bonds");

    // Create the rollout contents.
    QVBoxLayout* layout1 = new QVBoxLayout(rollout);
    layout1->setContentsMargins(4,4,4,4);
    layout1->setSpacing(6);

    IntegerRadioButtonParameterUI* cutoffModePUI = createParamUI<IntegerRadioButtonParameterUI>(PROPERTY_FIELD(CreateBondsModifier::cutoffMode));

    // Uniform cutoff parameter.
    QGridLayout* gridlayout = new QGridLayout();
    gridlayout->setContentsMargins(0,0,0,0);
    gridlayout->setColumnStretch(1, 1);
    QRadioButton* uniformCutoffModeBtn = cutoffModePUI->addRadioButton(CreateBondsModifier::UniformCutoff, tr("Uniform cutoff distance:"));
    FloatParameterUI* uniformCutoffPUI = createParamUI<FloatParameterUI>(PROPERTY_FIELD(CreateBondsModifier::uniformCutoff));
    gridlayout->addWidget(uniformCutoffModeBtn, 0, 0);
    gridlayout->addLayout(uniformCutoffPUI->createFieldLayout(), 0, 1);
    uniformCutoffPUI->setEnabled(false);
    connect(uniformCutoffModeBtn, &QRadioButton::toggled, uniformCutoffPUI, &FloatParameterUI::setEnabled);
    layout1->addLayout(gridlayout);

    // Covalent radius mode (no settings).
    layout1->addWidget(cutoffModePUI->addRadioButton(CreateBondsModifier::CovalentRadiusCutoff, tr("Covalent radii")));

    // Van der Waals mode.
    QRadioButton* typeRadiusModeBtn = cutoffModePUI->addRadioButton(CreateBondsModifier::TypeRadiusCutoff, tr("Van der Waals radii:"));
    layout1->addWidget(typeRadiusModeBtn);
    QVBoxLayout* sublayout = new QVBoxLayout();
    sublayout->setContentsMargins(26,0,0,0);
    BooleanParameterUI* skipHydrogenHydrogenBondsUI = createParamUI<BooleanParameterUI>(PROPERTY_FIELD(CreateBondsModifier::skipHydrogenHydrogenBonds));
    sublayout->addWidget(skipHydrogenHydrogenBondsUI->checkBox());
    skipHydrogenHydrogenBondsUI->setEnabled(false);
    connect(typeRadiusModeBtn, &QRadioButton::toggled, skipHydrogenHydrogenBondsUI, &ParameterUI::setEnabled);

    _vdwTable = new QTableWidget();
    _vdwTable->verticalHeader()->setVisible(false);
    _vdwTable->setEnabled(false);
    _vdwTable->setShowGrid(false);
    _vdwTable->setColumnCount(2);
    _vdwTable->setHorizontalHeaderLabels(QStringList() << tr("Particle type") << tr("VdW radius"));
    _vdwTable->verticalHeader()->setDefaultSectionSize(_vdwTable->verticalHeader()->minimumSectionSize());
    _vdwTable->horizontalHeader()->setStretchLastSection(true);
    connect(typeRadiusModeBtn, &QRadioButton::toggled, _vdwTable, &QTableView::setEnabled);
    sublayout->addWidget(_vdwTable);
    layout1->addLayout(sublayout);

    // Pair-wise cutoff mode.
    QRadioButton* pairCutoffModeBtn = cutoffModePUI->addRadioButton(CreateBondsModifier::PairCutoff, tr("Pair-wise cutoffs:"));
    layout1->addWidget(pairCutoffModeBtn);
    sublayout = new QVBoxLayout();
    sublayout->setContentsMargins(26,0,0,0);

    _pairCutoffTable = new QTableView();
    _pairCutoffTable->verticalHeader()->setVisible(false);
    _pairCutoffTable->setEnabled(false);
    _pairCutoffTableModel = new PairCutoffTableModel(this);
    _pairCutoffTable->setModel(_pairCutoffTableModel);
    _pairCutoffTable->verticalHeader()->setDefaultSectionSize(_pairCutoffTable->verticalHeader()->minimumSectionSize());
    _pairCutoffTable->horizontalHeader()->setStretchLastSection(true);
    connect(pairCutoffModeBtn, &QRadioButton::toggled, _pairCutoffTable, &QTableView::setEnabled);
    sublayout->addWidget(_pairCutoffTable);
    layout1->addLayout(sublayout);

    BooleanParameterUI* discardExistingBondsUI = createParamUI<BooleanParameterUI>(PROPERTY_FIELD(CreateBondsModifier::discardExistingBonds));
    layout1->addWidget(discardExistingBondsUI->checkBox());

    BooleanParameterUI* onlyIntraMoleculeBondsUI = createParamUI<BooleanParameterUI>(PROPERTY_FIELD(CreateBondsModifier::onlyIntraMoleculeBonds));
    layout1->addWidget(onlyIntraMoleculeBondsUI->checkBox());

    // Lower cutoff parameter.
    gridlayout = new QGridLayout();
    gridlayout->setContentsMargins(0,0,0,0);
    gridlayout->setColumnStretch(1, 1);
    FloatParameterUI* minCutoffPUI = createParamUI<FloatParameterUI>(PROPERTY_FIELD(CreateBondsModifier::minimumCutoff));
    gridlayout->addWidget(minCutoffPUI->label(), 0, 0);
    gridlayout->addLayout(minCutoffPUI->createFieldLayout(), 0, 1);
    layout1->addLayout(gridlayout);

    // Status label.
    layout1->addSpacing(10);
    layout1->addWidget(createParamUI<ObjectStatusDisplay>()->statusWidget());

    // Open a sub-editor for the bonds vis element.
    createParamUI<SubObjectParameterUI>(PROPERTY_FIELD(CreateBondsModifier::bondsVis), rolloutParams.after(rollout));

    // Open a sub-editor for the bond type.
    createParamUI<SubObjectParameterUI>(PROPERTY_FIELD(CreateBondsModifier::bondType), rolloutParams.after(rollout).collapse().setTitle(tr("New bond type")));

    // Update pair-wise cutoff table whenever a modifier has been loaded into the editor.
    connect(this, &CreateBondsModifierEditor::contentsReplaced, this, &CreateBondsModifierEditor::updatePairCutoffList);
    connect(this, &CreateBondsModifierEditor::contentsChanged, this, &CreateBondsModifierEditor::updatePairCutoffListValues);

    // Update van der Waals radius list.
    connect(this, &CreateBondsModifierEditor::contentsReplaced, this, &CreateBondsModifierEditor::updateVanDerWaalsList);
}

/******************************************************************************
* Updates the contents of the pair-wise cutoff table.
******************************************************************************/
void CreateBondsModifierEditor::updatePairCutoffList()
{
    CreateBondsModifier* mod = static_object_cast<CreateBondsModifier>(editObject());
    if(!mod) return;

    // Obtain the list of particle types in the modifier's input.
    PairCutoffTableModel::ContentType pairCutoffs;
    const PipelineFlowState& inputState = getPipelineInput();
    if(const Particles* particles = inputState.getObject<Particles>()) {
        if(const Property* typeProperty = particles->getProperty(Particles::TypeProperty)) {
            for(auto ptype1 = typeProperty->elementTypes().constBegin(); ptype1 != typeProperty->elementTypes().constEnd(); ++ptype1) {
                for(auto ptype2 = ptype1; ptype2 != typeProperty->elementTypes().constEnd(); ++ptype2) {
                    pairCutoffs.emplace_back(OORef<const ElementType>(*ptype1), OORef<const ElementType>(*ptype2));
                }
            }
        }
    }
    bool isEmpty = pairCutoffs.empty();
    _pairCutoffTableModel->setContent(mod, std::move(pairCutoffs));
    _pairCutoffTable->resizeColumnToContents(isEmpty ? 0 : 2);
}

/******************************************************************************
* Updates the cutoff values in the pair-wise cutoff table.
******************************************************************************/
void CreateBondsModifierEditor::updatePairCutoffListValues()
{
    _pairCutoffTableModel->updateContent();
}

/******************************************************************************
* Returns data from the pair-cutoff table model.
******************************************************************************/
QVariant CreateBondsModifierEditor::PairCutoffTableModel::data(const QModelIndex& index, int role) const
{
    if(_data.empty()) {
        if(role == Qt::DisplayRole && index.column() == 0) return tr("No particle types defined");
        else return {};
    }
    if(role == Qt::DisplayRole || role == Qt::EditRole) {
        if(index.column() == 0) {
            return _data[index.row()].first->nameOrNumericId();
        }
        else if(index.column() == 1) {
            return _data[index.row()].second->nameOrNumericId();
        }
        else if(index.column() == 2) {
            const auto& type1 = _data[index.row()].first;
            const auto& type2 = _data[index.row()].second;
            FloatType cutoffRadius = _modifier->getPairwiseCutoff(
                type1->name().isEmpty() ? QVariant::fromValue(type1->numericId()) : QVariant::fromValue(type1->name()),
                type2->name().isEmpty() ? QVariant::fromValue(type2->numericId()) : QVariant::fromValue(type2->name()));
            if(cutoffRadius > 0)
                return QString("%1").arg(cutoffRadius);
        }
    }
    else if(role == Qt::DecorationRole) {
        if(index.column() == 0) return (QColor)_data[index.row()].first->color();
        else if(index.column() == 1) return (QColor)_data[index.row()].second->color();
    }
    return {};
}

/******************************************************************************
* Sets data in the pair-cutoff table model.
******************************************************************************/
bool CreateBondsModifierEditor::PairCutoffTableModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if(role == Qt::EditRole && index.column() == 2) {
        bool ok;
        FloatType cutoff = (FloatType)value.toDouble(&ok);
        if(!ok) cutoff = 0;
        editor()->performTransaction(tr("Change cutoff"), [this, &index, cutoff]() {
            const auto& type1 = _data[index.row()].first;
            const auto& type2 = _data[index.row()].second;
            _modifier->setPairwiseCutoff(
                type1->name().isEmpty() ? QVariant::fromValue(type1->numericId()) : QVariant::fromValue(type1->name()),
                type2->name().isEmpty() ? QVariant::fromValue(type2->numericId()) : QVariant::fromValue(type2->name()),
                cutoff);
        });
        return true;
    }
    return false;
}

/******************************************************************************
* Updates the list of van der Waals radii.
******************************************************************************/
void CreateBondsModifierEditor::updateVanDerWaalsList()
{
    _vdwTable->clearContents();

    CreateBondsModifier* mod = static_object_cast<CreateBondsModifier>(editObject());
    if(!mod) return;

    int row = 0;

    // Obtain the list of particle types and their van der Waals radii from the modifier's input.
    const PipelineFlowState& inputState = getPipelineInput();
    if(const Particles* particles = inputState.getObject<Particles>()) {
        if(const Property* typeProperty = particles->getProperty(Particles::TypeProperty)) {
            // Count number of table entries.
            for(const ElementType* type : typeProperty->elementTypes()) {
                if(const ParticleType* ptype = dynamic_object_cast<ParticleType>(type))
                    row++;
            }
            // Create table entries.
            _vdwTable->setRowCount(row);
            row = 0;
            for(const ElementType* type : typeProperty->elementTypes()) {
                if(const ParticleType* ptype = dynamic_object_cast<ParticleType>(type)) {
                    QTableWidgetItem* nameItem = new QTableWidgetItem(ptype->nameOrNumericId());
                    nameItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemNeverHasChildren);
                    _vdwTable->setItem(row, 0, nameItem);
                    QTableWidgetItem* radiusItem = new QTableWidgetItem(
                        (ptype->vdwRadius() > 0.0) ? QString::number(ptype->vdwRadius())
                        : tr("‹unspecified›"));
                    radiusItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemNeverHasChildren);
                    _vdwTable->setItem(row, 1, radiusItem);
                    row++;
                }
            }
            OVITO_ASSERT(row == _vdwTable->rowCount());
        }
    }
    if(row == 0) {
        _vdwTable->setRowCount(1);
        QTableWidgetItem* emptyItem = new QTableWidgetItem(tr("No particle types defined"));
        emptyItem->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemNeverHasChildren);
        _vdwTable->setItem(0, 0, emptyItem);
    }
    _vdwTable->resizeColumnToContents(0);
}

}   // End of namespace
