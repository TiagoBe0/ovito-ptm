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
#include <ovito/ml/MLStructureModifier.h>
#include <ovito/stdobj/properties/PropertyReference.h>
#include <ovito/particles/objects/Particles.h>
#include <ovito/core/dataset/pipeline/PipelineFlowState.h>
#include <ovito/gui/desktop/properties/FilenameParameterUI.h>
#include <ovito/gui/desktop/properties/FloatParameterUI.h>
#include <ovito/gui/desktop/properties/IntegerParameterUI.h>
#include <ovito/gui/desktop/properties/ObjectStatusDisplay.h>
#include <QRadioButton>
#include <QButtonGroup>
#include "MLStructureModifierEditor.h"

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(MLStructureModifierEditor);
SET_OVITO_OBJECT_EDITOR(MLStructureModifier, MLStructureModifierEditor);

/******************************************************************************
* Builds the rollout widget shown in the Properties panel.
******************************************************************************/
void MLStructureModifierEditor::createUI(const RolloutInsertionParameters& rolloutParams)
{
    QWidget* rollout = createRollout(tr("ML Structure Modifier"), rolloutParams);
    QVBoxLayout* mainLayout = new QVBoxLayout(rollout);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(6);

    // -----------------------------------------------------------------------
    // Model file
    // -----------------------------------------------------------------------
    {
        QGroupBox* box = new QGroupBox(tr("Model"), rollout);
        QVBoxLayout* lay = new QVBoxLayout(box);
        lay->setContentsMargins(4, 4, 4, 4);
        lay->setSpacing(4);

        lay->addWidget(new QLabel(tr("TorchScript model file (<tt>.pt</tt>):"), box));

        QStringList ptFilter = { tr("TorchScript models (*.pt)"), tr("All files (*)") };
        FilenameParameterUI* modelPathUI = createParamUI<FilenameParameterUI>(
            PROPERTY_FIELD(MLStructureModifier::modelPath), ptFilter, /*existingFile=*/true);
        lay->addWidget(modelPathUI->selectorWidget());

        mainLayout->addWidget(box);
    }

    // -----------------------------------------------------------------------
    // Input mode radio buttons
    // -----------------------------------------------------------------------
    {
        QGroupBox* box = new QGroupBox(tr("Input features"), rollout);
        QVBoxLayout* lay = new QVBoxLayout(box);
        lay->setContentsMargins(4, 4, 4, 4);
        lay->setSpacing(2);

        QButtonGroup* btnGroup = new QButtonGroup(box);

        QRadioButton* rbNeigh = new QRadioButton(
            tr("Neighbour distances (sorted, normalised)"), box);
        QRadioButton* rbProp  = new QRadioButton(
            tr("Particle property columns"), box);

        btnGroup->addButton(rbNeigh, static_cast<int>(MLStructureModifier::InputMode::NeighborDistances));
        btnGroup->addButton(rbProp,  static_cast<int>(MLStructureModifier::InputMode::ParticleProperties));

        lay->addWidget(rbNeigh);
        lay->addWidget(rbProp);
        mainLayout->addWidget(box);

        // Sync radio buttons → modifier field.
        connect(btnGroup, &QButtonGroup::idClicked, this, [this](int id) {
            if(auto* mod = static_cast<MLStructureModifier*>(editObject())) {
                undoableTransaction(tr("Change input mode"), [mod, id]() {
                    mod->setInputMode(static_cast<MLStructureModifier::InputMode>(id));
                });
            }
        });

        // Sync modifier field → radio buttons.
        connect(this, &PropertiesEditor::contentsChanged, this, [btnGroup, this]() {
            if(auto* mod = static_cast<MLStructureModifier*>(editObject()))
                btnGroup->button(static_cast<int>(mod->inputMode()))->setChecked(true);
            onInputModeChanged();
        });
    }

    // -----------------------------------------------------------------------
    // NeighborDistances parameters
    // -----------------------------------------------------------------------
    {
        _descParamsBox = new QGroupBox(tr("Descriptor parameters"), rollout);
        QGridLayout* grid = new QGridLayout(_descParamsBox);
        grid->setContentsMargins(4, 4, 4, 4);
        grid->setColumnStretch(1, 1);
        int row = 0;

        FloatParameterUI* cutoffUI = createParamUI<FloatParameterUI>(
            PROPERTY_FIELD(MLStructureModifier::cutoffRadius));
        grid->addWidget(cutoffUI->label(), row, 0);
        grid->addLayout(cutoffUI->createFieldLayout(), row, 1);
        ++row;

        IntegerParameterUI* numNeighUI = createParamUI<IntegerParameterUI>(
            PROPERTY_FIELD(MLStructureModifier::numNeighbors));
        grid->addWidget(numNeighUI->label(), row, 0);
        grid->addLayout(numNeighUI->createFieldLayout(), row, 1);
        ++row;

        grid->addWidget(new QLabel(
            tr("<small>Must match the values used when training the model.</small>"),
            _descParamsBox), row, 0, 1, 2);

        mainLayout->addWidget(_descParamsBox);
    }

    // -----------------------------------------------------------------------
    // ParticleProperties selection
    // -----------------------------------------------------------------------
    {
        _propSelectBox = new QGroupBox(tr("Property columns"), rollout);
        QVBoxLayout* lay = new QVBoxLayout(_propSelectBox);
        lay->setContentsMargins(4, 4, 4, 4);
        lay->setSpacing(4);

        lay->addWidget(new QLabel(
            tr("Select the columns to use as input features.\n"
               "Order matters — must match the training data."),
            _propSelectBox));

        _propListWidget = new QListWidget(_propSelectBox);
        _propListWidget->setSelectionMode(QAbstractItemView::NoSelection);
        _propListWidget->setMinimumHeight(120);
        lay->addWidget(_propListWidget);

        lay->addWidget(new QLabel(
            tr("<small>Run an upstream modifier first (e.g. Voronoi Analysis) "
               "to see its output columns here.</small>"),
            _propSelectBox));

        mainLayout->addWidget(_propSelectBox);

        // Repopulate list when pipeline input changes.
        connect(this, &PropertiesEditor::pipelineInputChanged,
                this, &MLStructureModifierEditor::updatePropertyList);

        // React to item check/uncheck.
        connect(_propListWidget, &QListWidget::itemChanged,
                this, &MLStructureModifierEditor::onPropertyItemChanged);
    }

    // -----------------------------------------------------------------------
    // Status display
    // -----------------------------------------------------------------------
    mainLayout->addSpacing(4);
    mainLayout->addWidget(createParamUI<ObjectStatusDisplay>()->statusWidget());

    // Initial visibility pass (will be repeated via contentsChanged signal).
    onInputModeChanged();
    updatePropertyList();
}

/******************************************************************************
* Shows/hides the two parameter sections based on the active input mode.
******************************************************************************/
void MLStructureModifierEditor::onInputModeChanged()
{
    auto* mod = static_cast<MLStructureModifier*>(editObject());
    bool useProps = mod &&
        mod->inputMode() == MLStructureModifier::InputMode::ParticleProperties;

    if(_descParamsBox)  _descParamsBox->setVisible(!useProps);
    if(_propSelectBox)  _propSelectBox->setVisible(useProps);
}

/******************************************************************************
* Repopulates the property list from the current upstream pipeline state.
******************************************************************************/
void MLStructureModifierEditor::updatePropertyList()
{
    if(!_propListWidget) return;

    _updatingPropertyList = true;

    // Collect the currently selected properties from the modifier.
    QStringList selected;
    if(auto* mod = static_cast<MLStructureModifier*>(editObject()))
        selected = mod->inputProperties();

    _propListWidget->clear();

    // Walk all PipelineFlowState inputs and collect particle property columns.
    for(const PipelineFlowState& state : getPipelineInputs()) {
        const Particles* particles = state.getObject<Particles>();
        if(!particles) continue;

        for(const Property* prop : particles->properties()) {
            // Skip properties whose data type is not numeric.
            int dt = prop->dataType();
            if(dt != QMetaType::Float   && dt != QMetaType::Double &&
               dt != QMetaType::Int     && dt != QMetaType::LongLong)
                continue;

            const size_t nComp = prop->componentCount();
            const QStringList& compNames = prop->componentNames();

            if(nComp == 1) {
                // Scalar property: single entry.
                PropertyReference ref(prop, -1);
                QString key = ref.nameWithComponent();
                auto* item = new QListWidgetItem(prop->name(), _propListWidget);
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                item->setCheckState(selected.contains(key) ? Qt::Checked : Qt::Unchecked);
                item->setData(Qt::UserRole, key);
            } else {
                // Vector property: one entry per component.
                for(size_t c = 0; c < nComp; ++c) {
                    PropertyReference ref(prop, static_cast<int>(c));
                    QString key = ref.nameWithComponent();
                    QString label = (c < (size_t)compNames.size())
                        ? QStringLiteral("%1.%2").arg(prop->name(), compNames[c])
                        : QStringLiteral("%1.%2").arg(prop->name()).arg(c);
                    auto* item = new QListWidgetItem(label, _propListWidget);
                    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                    item->setCheckState(selected.contains(key) ? Qt::Checked : Qt::Unchecked);
                    item->setData(Qt::UserRole, key);
                }
            }
        }
        break; // use only the first (immediate upstream) state
    }

    _updatingPropertyList = false;
}

/******************************************************************************
* Commits the current checkbox state to the modifier's inputProperties field.
******************************************************************************/
void MLStructureModifierEditor::onPropertyItemChanged(QListWidgetItem* /*item*/)
{
    if(_updatingPropertyList) return;
    auto* mod = static_cast<MLStructureModifier*>(editObject());
    if(!mod) return;

    QStringList newList;
    for(int i = 0; i < _propListWidget->count(); ++i) {
        QListWidgetItem* it = _propListWidget->item(i);
        if(it->checkState() == Qt::Checked)
            newList << it->data(Qt::UserRole).toString();
    }

    undoableTransaction(tr("Change input properties"), [mod, &newList]() {
        mod->setInputProperties(newList);
    });
}

}  // namespace Ovito
