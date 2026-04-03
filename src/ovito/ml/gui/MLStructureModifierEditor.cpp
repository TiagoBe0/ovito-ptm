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
#include <ovito/core/app/undo/UndoableTransaction.h>
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
    // Input mode
    // -----------------------------------------------------------------------
    {
        QGroupBox* box = new QGroupBox(tr("Input features"), rollout);
        QVBoxLayout* lay = new QVBoxLayout(box);
        lay->setContentsMargins(4, 4, 4, 4);
        lay->setSpacing(2);

        QButtonGroup* btnGroup = new QButtonGroup(box);
        QRadioButton* rbNeigh  = new QRadioButton(tr("Neighbour distances (sorted, normalised)"), box);
        QRadioButton* rbProp   = new QRadioButton(tr("Particle property columns"), box);
        btnGroup->addButton(rbNeigh, static_cast<int>(MLStructureModifier::InputMode::NeighborDistances));
        btnGroup->addButton(rbProp,  static_cast<int>(MLStructureModifier::InputMode::ParticleProperties));
        lay->addWidget(rbNeigh);
        lay->addWidget(rbProp);
        mainLayout->addWidget(box);

        connect(btnGroup, &QButtonGroup::idClicked, this, [this](int id) {
            if(auto* mod = static_cast<MLStructureModifier*>(editObject())) {
                UndoableTransaction t;
                t.begin(ui(), tr("Change input mode"));
                mod->setInputMode(static_cast<MLStructureModifier::InputMode>(id));
                t.commit();
            }
        });

        connect(this, &PropertiesEditor::contentsChanged, this, [btnGroup, this]() {
            if(auto* mod = static_cast<MLStructureModifier*>(editObject())) {
                if(auto* btn = btnGroup->button(static_cast<int>(mod->inputMode())))
                    btn->setChecked(true);
            }
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
            tr("Select columns used as input features.\n"
               "Order matters — must match the training data."), _propSelectBox));

        _propListWidget = new QListWidget(_propSelectBox);
        _propListWidget->setSelectionMode(QAbstractItemView::NoSelection);
        _propListWidget->setMinimumHeight(120);
        lay->addWidget(_propListWidget);

        lay->addWidget(new QLabel(
            tr("<small>Apply upstream modifiers (e.g. Voronoi Analysis) first "
               "to see their output columns here.</small>"), _propSelectBox));

        mainLayout->addWidget(_propSelectBox);

        connect(this, &PropertiesEditor::pipelineInputChanged,
                this, &MLStructureModifierEditor::updatePropertyList);
        connect(_propListWidget, &QListWidget::itemChanged,
                this, &MLStructureModifierEditor::onPropertyItemChanged);
    }

    // -----------------------------------------------------------------------
    // Output mode
    // -----------------------------------------------------------------------
    {
        QGroupBox* box = new QGroupBox(tr("Output"), rollout);
        QGridLayout* grid = new QGridLayout(box);
        grid->setContentsMargins(4, 4, 4, 4);
        grid->setColumnStretch(1, 1);
        int row = 0;

        // Radio buttons for output mode.
        QButtonGroup* outGroup  = new QButtonGroup(box);
        QRadioButton* rbClass   = new QRadioButton(tr("Classification  (argmax → integer class index)"), box);
        QRadioButton* rbReg     = new QRadioButton(tr("Regression  (raw float values)"), box);
        outGroup->addButton(rbClass, static_cast<int>(MLStructureModifier::OutputMode::Classification));
        outGroup->addButton(rbReg,   static_cast<int>(MLStructureModifier::OutputMode::Regression));
        grid->addWidget(rbClass, row, 0, 1, 2); ++row;
        grid->addWidget(rbReg,   row, 0, 1, 2); ++row;

        // Output property name.
        grid->addWidget(new QLabel(tr("Output property name:"), box), row, 0);
        _outPropNameEdit = new QLineEdit(box);
        _outPropNameEdit->setPlaceholderText("ML_Structure");
        grid->addWidget(_outPropNameEdit, row, 1);
        ++row;

        mainLayout->addWidget(box);

        // Sync output-mode radio → modifier.
        connect(outGroup, &QButtonGroup::idClicked, this, [this](int id) {
            if(auto* mod = static_cast<MLStructureModifier*>(editObject())) {
                UndoableTransaction t;
                t.begin(ui(), tr("Change output mode"));
                mod->setOutputMode(static_cast<MLStructureModifier::OutputMode>(id));
                t.commit();
            }
            onOutputModeChanged();
        });

        // Sync modifier → radio buttons.
        connect(this, &PropertiesEditor::contentsChanged, this, [outGroup, this]() {
            if(auto* mod = static_cast<MLStructureModifier*>(editObject())) {
                if(auto* btn = outGroup->button(static_cast<int>(mod->outputMode())))
                    btn->setChecked(true);
                _outPropNameEdit->blockSignals(true);
                _outPropNameEdit->setText(mod->outputPropertyName());
                _outPropNameEdit->blockSignals(false);
            }
            onOutputModeChanged();
        });

        // Sync property name line edit → modifier (on editing finished).
        connect(_outPropNameEdit, &QLineEdit::editingFinished, this, [this]() {
            if(auto* mod = static_cast<MLStructureModifier*>(editObject())) {
                const QString name = _outPropNameEdit->text().trimmed();
                if(name == mod->outputPropertyName()) return;
                UndoableTransaction t;
                t.begin(ui(), tr("Change output property name"));
                mod->setOutputPropertyName(name.isEmpty() ? QStringLiteral("ML_Structure") : name);
                t.commit();
            }
        });
    }

    // -----------------------------------------------------------------------
    // Classification key (shown only in Classification mode)
    // -----------------------------------------------------------------------
    {
        _classInfoBox = new QGroupBox(tr("Class index key"), rollout);
        QVBoxLayout* lay = new QVBoxLayout(_classInfoBox);
        lay->setContentsMargins(4, 4, 4, 4);

        lay->addWidget(new QLabel(
            tr("Integer written per particle (depends on your training labels):\n"
               "  0, 1, 2, … → class indices as defined in your dataset"),
            _classInfoBox));

        mainLayout->addWidget(_classInfoBox);
    }

    // -----------------------------------------------------------------------
    // Status display
    // -----------------------------------------------------------------------
    mainLayout->addSpacing(4);
    mainLayout->addWidget(createParamUI<ObjectStatusDisplay>()->statusWidget());

    onInputModeChanged();
    onOutputModeChanged();
    updatePropertyList();
}

/******************************************************************************
* Shows/hides input-related sections based on active input mode.
******************************************************************************/
void MLStructureModifierEditor::onInputModeChanged()
{
    auto* mod = static_cast<MLStructureModifier*>(editObject());
    bool useProps = mod &&
        mod->inputMode() == MLStructureModifier::InputMode::ParticleProperties;

    if(_descParamsBox) _descParamsBox->setVisible(!useProps);
    if(_propSelectBox) _propSelectBox->setVisible(useProps);
}

/******************************************************************************
* Shows/hides output-related sections based on active output mode.
******************************************************************************/
void MLStructureModifierEditor::onOutputModeChanged()
{
    auto* mod = static_cast<MLStructureModifier*>(editObject());
    bool isRegression = mod &&
        mod->outputMode() == MLStructureModifier::OutputMode::Regression;

    if(_classInfoBox) _classInfoBox->setVisible(!isRegression);
}

/******************************************************************************
* Repopulates the property list from the upstream pipeline state.
******************************************************************************/
void MLStructureModifierEditor::updatePropertyList()
{
    if(!_propListWidget) return;

    _updatingPropertyList = true;

    QStringList selected;
    if(auto* mod = static_cast<MLStructureModifier*>(editObject()))
        selected = mod->inputProperties();

    _propListWidget->clear();

    for(const PipelineFlowState& state : getPipelineInputs()) {
        const Particles* particles = state.getObject<Particles>();
        if(!particles) continue;

        for(const Property* prop : particles->properties()) {
            int dt = prop->dataType();
            if(dt != QMetaType::Float   && dt != QMetaType::Double &&
               dt != QMetaType::Int     && dt != QMetaType::LongLong)
                continue;

            const size_t nComp = prop->componentCount();
            const QStringList& compNames = prop->componentNames();

            if(nComp == 1) {
                PropertyReference ref(prop, -1);
                QString key = ref.nameWithComponent();
                auto* item = new QListWidgetItem(prop->name(), _propListWidget);
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                item->setCheckState(selected.contains(key) ? Qt::Checked : Qt::Unchecked);
                item->setData(Qt::UserRole, key);
            } else {
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
        break;
    }

    _updatingPropertyList = false;
}

/******************************************************************************
* Commits checked items to the modifier's inputProperties field.
******************************************************************************/
void MLStructureModifierEditor::onPropertyItemChanged(QListWidgetItem*)
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

    UndoableTransaction t;
    t.begin(ui(), tr("Change input properties"));
    mod->setInputProperties(newList);
    t.commit();
}

}  // namespace Ovito
