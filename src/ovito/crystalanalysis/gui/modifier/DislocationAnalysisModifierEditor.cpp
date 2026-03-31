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

#include <ovito/crystalanalysis/CrystalAnalysis.h>
#include <ovito/crystalanalysis/modifier/dxa/DislocationAnalysisModifier.h>
#include <ovito/particles/gui/modifier/analysis/StructureListParameterUI.h>
#include <ovito/gui/desktop/properties/BooleanParameterUI.h>
#include <ovito/gui/desktop/properties/BooleanGroupBoxParameterUI.h>
#include <ovito/gui/desktop/properties/IntegerParameterUI.h>
#include <ovito/gui/desktop/properties/FloatParameterUI.h>
#include <ovito/gui/desktop/properties/VariantComboBoxParameterUI.h>
#include <ovito/gui/desktop/properties/SubObjectParameterUI.h>
#include <ovito/gui/desktop/properties/ObjectStatusDisplay.h>
#include <ovito/core/dataset/pipeline/ModificationNode.h>
#include "DislocationAnalysisModifierEditor.h"

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(DislocationAnalysisModifierEditor);
SET_OVITO_OBJECT_EDITOR(DislocationAnalysisModifier, DislocationAnalysisModifierEditor);

IMPLEMENT_ABSTRACT_OVITO_CLASS(DislocationTypeListParameterUI);

/******************************************************************************
* Sets up the UI widgets of the editor.
******************************************************************************/
void DislocationAnalysisModifierEditor::createUI(const RolloutInsertionParameters& rolloutParams)
{
    // Create the rollout.
    QWidget* rollout = createRollout(tr("Dislocation analysis"), rolloutParams, "manual:particles.modifiers.dislocation_analysis");

    QVBoxLayout* layout = new QVBoxLayout(rollout);
    layout->setContentsMargins(4,4,4,4);
    layout->setSpacing(6);

    QGroupBox* structureBox = new QGroupBox(tr("Input crystal type"));
    layout->addWidget(structureBox);
    QVBoxLayout* sublayout1 = new QVBoxLayout(structureBox);
    sublayout1->setContentsMargins(4,4,4,4);
    VariantComboBoxParameterUI* crystalStructureUI = createParamUI<VariantComboBoxParameterUI>(PROPERTY_FIELD(DislocationAnalysisModifier::inputCrystalStructure));
    crystalStructureUI->comboBox()->addItem(tr("Face-centered cubic (FCC)"), QVariant::fromValue((int)StructureAnalysis::LATTICE_FCC));
    crystalStructureUI->comboBox()->addItem(tr("Hexagonal close-packed (HCP)"), QVariant::fromValue((int)StructureAnalysis::LATTICE_HCP));
    crystalStructureUI->comboBox()->addItem(tr("Body-centered cubic (BCC)"), QVariant::fromValue((int)StructureAnalysis::LATTICE_BCC));
    crystalStructureUI->comboBox()->addItem(tr("Diamond cubic / Zinc blende"), QVariant::fromValue((int)StructureAnalysis::LATTICE_CUBIC_DIAMOND));
    crystalStructureUI->comboBox()->addItem(tr("Diamond hexagonal / Wurtzite"), QVariant::fromValue((int)StructureAnalysis::LATTICE_HEX_DIAMOND));
    sublayout1->addWidget(crystalStructureUI->comboBox());

    QGroupBox* dxaParamsBox = new QGroupBox(tr("DXA parameters"));
    layout->addWidget(dxaParamsBox);
    QGridLayout* sublayout = new QGridLayout(dxaParamsBox);
    sublayout->setContentsMargins(4,4,4,4);
    sublayout->setSpacing(4);
    sublayout->setColumnStretch(1, 1);

    IntegerParameterUI* maxTrialCircuitSizeUI = createParamUI<IntegerParameterUI>(PROPERTY_FIELD(DislocationAnalysisModifier::maxTrialCircuitSize));
    sublayout->addWidget(maxTrialCircuitSizeUI->label(), 0, 0);
    sublayout->addLayout(maxTrialCircuitSizeUI->createFieldLayout(), 0, 1);

    IntegerParameterUI* circuitStretchabilityUI = createParamUI<IntegerParameterUI>(PROPERTY_FIELD(DislocationAnalysisModifier::circuitStretchability));
    sublayout->addWidget(circuitStretchabilityUI->label(), 1, 0);
    sublayout->addLayout(circuitStretchabilityUI->createFieldLayout(), 1, 1);

    QGroupBox* advancedParamsBox = new QGroupBox(tr("Advanced options"));
    layout->addWidget(advancedParamsBox);
    sublayout = new QGridLayout(advancedParamsBox);
    sublayout->setContentsMargins(4,4,4,4);
    sublayout->setSpacing(4);
    sublayout->setColumnStretch(0, 1);

    // Color by type
    int row = 0;
    BooleanParameterUI* colorByTypeUI = createParamUI<BooleanParameterUI>(PROPERTY_FIELD(StructureIdentificationModifier::colorByType));
    sublayout->addWidget(colorByTypeUI->checkBox(), row++, 0);

    BooleanParameterUI* onlySelectedParticlesUI = createParamUI<BooleanParameterUI>(PROPERTY_FIELD(StructureIdentificationModifier::onlySelectedParticles));
    sublayout->addWidget(onlySelectedParticlesUI->checkBox(), row++, 0);

    BooleanParameterUI* markCoreAtomsUI = createParamUI<BooleanParameterUI>(PROPERTY_FIELD(DislocationAnalysisModifier::markCoreAtoms));
#ifdef OVITO_BUILD_PROFESSIONAL
    markCoreAtomsUI->checkBox()->setText(PROPERTY_FIELD(DislocationAnalysisModifier::markCoreAtoms)->displayName());
#else
    markCoreAtomsUI->checkBox()->setText(PROPERTY_FIELD(DislocationAnalysisModifier::markCoreAtoms)->displayName() +
                                         tr(" (requires OVITO Pro)"));
    markCoreAtomsUI->setEnabled(false);
#endif
    sublayout->addWidget(markCoreAtomsUI->checkBox(), row++, 0);

    BooleanParameterUI* outputInterfaceMeshUI = createParamUI<BooleanParameterUI>(PROPERTY_FIELD(DislocationAnalysisModifier::outputInterfaceMesh));
    sublayout->addWidget(outputInterfaceMeshUI->checkBox(), row++, 0);

    BooleanParameterUI* onlyPerfectDislocationsUI = createParamUI<BooleanParameterUI>(PROPERTY_FIELD(DislocationAnalysisModifier::onlyPerfectDislocations));
    sublayout->addWidget(onlyPerfectDislocationsUI->checkBox(), row++, 0);

    BooleanParameterUI* usePTMUI = createParamUI<BooleanParameterUI>(PROPERTY_FIELD(DislocationAnalysisModifier::usePTM));
    sublayout->addWidget(usePTMUI->checkBox(), row++, 0);

    QGroupBox* postprocessingBox = new QGroupBox(tr("Post-processing"));
    layout->addWidget(postprocessingBox);
    sublayout = new QGridLayout(postprocessingBox);
    sublayout->setContentsMargins(4,4,4,4);
    sublayout->setSpacing(4);
     sublayout->setColumnStretch(1, 1);

    BooleanParameterUI* lineSmoothingEnabledUI = createParamUI<BooleanParameterUI>(PROPERTY_FIELD(DislocationAnalysisModifier::lineSmoothingEnabled));
    lineSmoothingEnabledUI->checkBox()->setText(tr("Line smoothing:"));
    IntegerParameterUI* lineSmoothingLevelUI = createParamUI<IntegerParameterUI>(PROPERTY_FIELD(DislocationAnalysisModifier::lineSmoothingLevel));
    sublayout->addWidget(lineSmoothingEnabledUI->checkBox(), 0, 0);
    sublayout->addLayout(lineSmoothingLevelUI->createFieldLayout(), 0, 1);
    lineSmoothingLevelUI->setEnabled(false);
    connect(lineSmoothingEnabledUI->checkBox(), &QCheckBox::toggled, lineSmoothingLevelUI, &IntegerParameterUI::setEnabled);

    BooleanParameterUI* lineCoarseningEnabledUI = createParamUI<BooleanParameterUI>(PROPERTY_FIELD(DislocationAnalysisModifier::lineCoarseningEnabled));
    lineCoarseningEnabledUI->checkBox()->setText(tr("Line coarsening:"));
    FloatParameterUI* linePointIntervalUI = createParamUI<FloatParameterUI>(PROPERTY_FIELD(DislocationAnalysisModifier::linePointInterval));
    sublayout->addWidget(lineCoarseningEnabledUI->checkBox(), 1, 0);
    sublayout->addLayout(linePointIntervalUI->createFieldLayout(), 1, 1);
    linePointIntervalUI->setEnabled(false);
    connect(lineCoarseningEnabledUI->checkBox(), &QCheckBox::toggled, linePointIntervalUI, &IntegerParameterUI::setEnabled);

    IntegerParameterUI* defectMeshSmoothingLevelUI = createParamUI<IntegerParameterUI>(PROPERTY_FIELD(DislocationAnalysisModifier::defectMeshSmoothingLevel));
    sublayout->addWidget(defectMeshSmoothingLevelUI->label(), 2, 0);
    sublayout->addLayout(defectMeshSmoothingLevelUI->createFieldLayout(), 2, 1);

    // Status label.
    layout->addWidget(createParamUI<ObjectStatusDisplay>()->statusWidget());

    // Structure list.
    StructureListParameterUI* structureTypesPUI = createParamUI<StructureListParameterUI>();
    layout->addSpacing(10);
    layout->addWidget(new QLabel(tr("Structure analysis results:")));
    layout->addWidget(structureTypesPUI->tableWidget());

    // Burgers vector list.
    DislocationTypeListParameterUI* burgersFamilyListUI = createParamUI<DislocationTypeListParameterUI>();
    layout->addSpacing(10);
    layout->addWidget(new QLabel(tr("Dislocation analysis results:")));
    layout->addWidget(burgersFamilyListUI->tableWidget());
    connect(this, &PropertiesEditor::pipelineOutputChanged, burgersFamilyListUI, [this,burgersFamilyListUI]() {
        // Get the current data pipeline output generated by the modifier.
        burgersFamilyListUI->updateDislocationCounts(getPipelineOutput(), modificationNode());
    });
}

/******************************************************************************
* Constructor.
******************************************************************************/
void DislocationTypeListParameterUI::initializeObject(PropertiesEditor* parent)
{
    RefTargetListParameterUI::initializeObject(parent, PROPERTY_FIELD(MicrostructurePhase::burgersVectorFamilies));

    connect(tableWidget(220), &QTableWidget::doubleClicked, this, &DislocationTypeListParameterUI::onDoubleClickDislocationType);
    tableWidget()->setAutoScroll(false);
}

/******************************************************************************
* Obtains the current statistics from the pipeline.
******************************************************************************/
void DislocationTypeListParameterUI::updateDislocationCounts(const PipelineFlowState& state, ModificationNode* node)
{
    // Access the data table in the pipeline state containing the dislocation counts and lengths.
    _dislocationCounts = node ? state.getObjectBy<DataTable>(node, QStringLiteral("disloc-counts")) : nullptr;
    _dislocationLengths = node ? state.getObjectBy<DataTable>(node, QStringLiteral("disloc-lengths")) : nullptr;
    setEditObject(editor()->editObject());
}

/******************************************************************************
* Returns a data item from the list data model.
******************************************************************************/
QVariant DislocationTypeListParameterUI::getItemData(RefTarget* target, const QModelIndex& index, int role)
{
    if(const BurgersVectorFamily* family = dynamic_object_cast<BurgersVectorFamily>(target)) {
        if(role == Qt::DisplayRole) {
            if(index.column() == 1) {
                return family->name();
            }
            else if(index.column() == 2 && _dislocationCounts) {
                if(const Property* yprop = _dislocationCounts->y()) {
                    if(yprop->size() > family->numericId() && yprop->dataType() == DataBuffer::Int32)
                        return BufferReadAccess<int32_t>(yprop)[family->numericId()];
                }
            }
            else if(index.column() == 3 && _dislocationLengths) {
                if(const Property* yprop = _dislocationLengths->y()) {
                    if(yprop->size() > family->numericId() && yprop->dataType() == Property::FloatDefault)
                        return QString::number(BufferReadAccess<FloatType>(yprop)[family->numericId()]);
                }
            }
        }
        else if(role == Qt::DecorationRole) {
            if(index.column() == 0)
                return (QColor)family->color();
        }
    }
    return QVariant();
}

/******************************************************************************
* Is called when the user has double-clicked on one of the dislocation
* types in the list widget.
******************************************************************************/
void DislocationTypeListParameterUI::onDoubleClickDislocationType(const QModelIndex& index)
{
    // Let the user select a color for the structure type.
    BurgersVectorFamily* family = static_object_cast<BurgersVectorFamily>(selectedObject());
    if(!family) return;

    QColor oldColor = (QColor)family->color();
    QColor newColor = QColorDialog::getColor(oldColor, _viewWidget);
    if(!newColor.isValid() || newColor == oldColor) return;

    performTransaction(tr("Change dislocation type color"), [family, newColor]() {
        family->setColor(Color(newColor));
    });
}

}   // End of namespace
