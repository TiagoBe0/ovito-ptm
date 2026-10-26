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

#include <ovito/gui/desktop/GUI.h>
#include <ovito/gui/base/actions/ActionManager.h>
#include <ovito/gui/desktop/mainwin/MainWindowUI.h>
#include <ovito/core/dataset/pipeline/ModificationNode.h>
#include <ovito/core/dataset/scene/Pipeline.h>
#include <ovito/core/dataset/DataSetContainer.h>
#include <ovito/core/oo/CloneHelper.h>
#include "CopyPipelineItemDialog.h"

namespace Ovito {

/******************************************************************************
* Constructor.
******************************************************************************/
CopyPipelineItemDialog::CopyPipelineItemDialog(MainWindowUI& ui, QWidget* parent, Pipeline* sourcePipeline, std::vector<OORef<PipelineNode>> pipelineNodes) :
    QDialog(parent), UserInterfaceComponent<MainWindowUI>(ui), _sourcePipeline(sourcePipeline), _pipelineNodes(std::move(pipelineNodes))
{
    setWindowTitle(tr("Copy Pipeline Items"));

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    QGridLayout* gridLayout = new QGridLayout();
    mainLayout->addLayout(gridLayout, 1);
    gridLayout->setColumnStretch(1, 1);

    _destinationPipelineList = new QComboBox(this);
    gridLayout->addWidget(new QLabel(tr("Copy to pipeline:")), 0, 0);
    gridLayout->addWidget(_destinationPipelineList, 0, 1);

    // Build the list of scene pipelines.
    if(Scene* scene = datasetContainer().activeScene()) {
        scene->visitPipelines([&](SceneNode* sceneNode) -> bool {
            QString itemLabel = sceneNode->objectTitle();
            if(sceneNode->pipeline() == sourcePipeline)
                itemLabel += tr(" (source pipeline)");
            QVariant data = QVariant::fromValue(OORef<OvitoObject>(sceneNode->pipeline()));
            // Skip duplicate pipelines (in case a pipeline is reference by multiple scene nodes).
            if(_destinationPipelineList->findData(data) != -1)
                return true;
            _destinationPipelineList->addItem(std::move(itemLabel), std::move(data));
            if(sceneNode->pipeline() == sourcePipeline)
                _destinationPipelineList->setCurrentIndex(_destinationPipelineList->count() - 1);
            else {
#ifdef OVITO_BUILD_BASIC
                QStandardItem* item = static_cast<QStandardItemModel*>(_destinationPipelineList->model())->item(_destinationPipelineList->count() - 1);
                item->setEnabled(false);
                item->setText(item->text() + " (requires OVITO Pro)");
#endif
            }
            return true;
        });
    }

    gridLayout->addWidget(new QLabel(tr("Insert at:")), 1, 0);
    QButtonGroup* insertionPositionGroup = new QButtonGroup(this);
    _insertAtEndBtn = new QRadioButton(tr("End of pipeline (top)"));
    _insertAtStartBtn = new QRadioButton(tr("Beginning of pipeline (bottom)"));
    insertionPositionGroup->addButton(_insertAtEndBtn);
    insertionPositionGroup->addButton(_insertAtStartBtn);
    gridLayout->addWidget(_insertAtEndBtn, 1, 1);
    gridLayout->addWidget(_insertAtStartBtn, 2, 1);
    _insertAtEndBtn->setChecked(true);

    // Only allow insertion at beginning of pipeline if it's a pipeline source
    // that is being cloned.
    if(!boost::algorithm::all_of(_pipelineNodes, [](const auto& item) {
        return ModificationNode::OOClass().isMember(item);
    })) {
        _insertAtStartBtn->setChecked(true);
        _insertAtEndBtn->setEnabled(false);
    }

    _shareBetweenPipelinesBox = new QCheckBox(tr("Share with source pipeline (do not duplicate)"));
    gridLayout->addWidget(_shareBetweenPipelinesBox, 3, 0, 1, 2);

    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Help, Qt::Horizontal, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &CopyPipelineItemDialog::onAccept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &CopyPipelineItemDialog::reject);
    connect(buttonBox, &QDialogButtonBox::helpRequested, this, [this]() {
        actionManager()->openHelpTopic("manual:clone_pipeline.copy_pipeline_items_dialog");
    });
    mainLayout->addWidget(buttonBox);
}

/******************************************************************************
* Is called when the user presses the 'Ok' button
******************************************************************************/
void CopyPipelineItemDialog::onAccept()
{
    setFocus(); // Remove focus from child widgets to commit newly entered values in text widgets etc.

    performTransaction(tr("Copy pipeline item"), [this]() {
        OORef<Pipeline> destinationPipeline = static_object_cast<Pipeline>(_destinationPipelineList->currentData().value<OORef<OvitoObject>>());
        CloneHelper cloneHelper;

        // Do not create any animation keys during cloning.
        AnimationSuspender animSuspender(ui());

        OORef<PipelineNode> precedingNode;
        for(auto node = _pipelineNodes.crbegin(); node != _pipelineNodes.crend(); ++node) {
            if(ModificationNode* modNode = dynamic_object_cast<ModificationNode>(node->get())) {
                // Copy modification node.
                OORef<ModificationNode> clonedModNode = cloneHelper.cloneObject(modNode, false);
                clonedModNode->setInput(nullptr); // avoid cyclic reference errors
                if(!_shareBetweenPipelinesBox->isChecked()) {
                    clonedModNode->setModifier(cloneHelper.cloneObject(clonedModNode->modifier(), true));
                }
                if(!precedingNode) {
                    if(_insertAtEndBtn->isChecked()) {
                        // Append copied modifiers at end of current pipeline.
                        precedingNode = destinationPipeline->head();
                    }
                    else {
                        // Prepend copied modifiers at beginning of current pipeline, right after the existing pipeline source.
                        precedingNode = destinationPipeline->source();
                    }
                }
                clonedModNode->setInput(precedingNode);
                precedingNode = clonedModNode;
            }
            else {
                // Copy or clone pipeline source.
                precedingNode = _shareBetweenPipelinesBox->isChecked() ? *node : cloneHelper.cloneObject(*node, false);
            }
        }

        if(_insertAtEndBtn->isChecked()) {
            // Append copied modifiers at end of current pipeline.
            destinationPipeline->setHead(precedingNode);
        }
        else {
            // Prepend copied modifiers at beginning of current pipeline, right after the existing pipeline source.
            destinationPipeline->setSource(precedingNode);
        }
    });
    accept();
}

}   // End of namespace
