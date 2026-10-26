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
#include <ovito/gui/desktop/widgets/general/EnterLineEdit.h>
#include <ovito/gui/base/actions/ActionManager.h>
#include <ovito/core/dataset/pipeline/ModificationNode.h>
#include <ovito/core/dataset/scene/Pipeline.h>
#include <ovito/core/oo/CloneHelper.h>
#include "ClonePipelineDialog.h"

namespace Ovito {

/******************************************************************************
* Constructor.
******************************************************************************/
ClonePipelineDialog::ClonePipelineDialog(MainWindowUI& ui, SceneNode* sceneNode, QWidget* parent) :
    QDialog(parent),
    UserInterfaceComponent<MainWindowUI>(ui),
    _originalSceneNode(sceneNode),
    _originalPipeline(sceneNode->pipeline())
{
    setWindowTitle(tr("Clone pipeline"));

    initializeGraphicsScene();

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    _pipelineView = new QGraphicsView(&_pipelineScene, this);
    _pipelineView->setSceneRect(_pipelineView->sceneRect().marginsAdded(QMarginsF(15,15,15,15)));
    _pipelineView->setRenderHint(QPainter::Antialiasing);
    mainLayout->addWidget(_pipelineView, 1);

    QGroupBox* displacementBox = new QGroupBox(tr("Positioning of cloned pipeline"));
    mainLayout->addWidget(displacementBox);
    QHBoxLayout* sublayout2 = new QHBoxLayout(displacementBox);
    QToolBar* displacementToolBar = new QToolBar(displacementBox);
    displacementToolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    displacementToolBar->setIconSize(QSize(64,64));
    displacementToolBar->setStyleSheet("QToolBar { padding: 0px; margin: 0px; border: 0px none black; spacing: 8px; } QToolButton { padding: 0px; margin: 0px; }");
    sublayout2->addWidget(displacementToolBar);
    _displacementDirectionGroup = new QActionGroup(this);
    _displacementDirectionGroup->setExclusive(true);
    QAction* displacementNoneAction = displacementToolBar->addAction(QIcon::fromTheme("edit_clone_displace_mode_none"), tr("Do not displace clone"));
    QAction* displacementXAction = displacementToolBar->addAction(QIcon::fromTheme("edit_clone_displace_mode_x"), tr("Displace clone along X axis"));
    QAction* displacementYAction = displacementToolBar->addAction(QIcon::fromTheme("edit_clone_displace_mode_y"), tr("Displace clone along Y axis"));
    QAction* displacementZAction = displacementToolBar->addAction(QIcon::fromTheme("edit_clone_displace_mode_z"), tr("Displace clone along Z axis"));
    sublayout2->addStretch(1);
    displacementNoneAction->setCheckable(true);
    displacementXAction->setCheckable(true);
    displacementYAction->setCheckable(true);
    displacementZAction->setCheckable(true);
    displacementXAction->setChecked(true);
    displacementNoneAction->setData(-1);
    displacementXAction->setData(0);
    displacementYAction->setData(1);
    displacementZAction->setData(2);
    _displacementDirectionGroup->addAction(displacementNoneAction);
    _displacementDirectionGroup->addAction(displacementXAction);
    _displacementDirectionGroup->addAction(displacementYAction);
    _displacementDirectionGroup->addAction(displacementZAction);

    QGroupBox* nameBox = new QGroupBox(tr("Pipeline names"));
    mainLayout->addWidget(nameBox);
    sublayout2 = new QHBoxLayout(nameBox);
    sublayout2->setSpacing(2);
    _originalNameEdit = new EnterLineEdit(nameBox);
    _cloneNameEdit = new EnterLineEdit(nameBox);
    sublayout2->addWidget(new QLabel(tr("Original:")));
    sublayout2->addWidget(_originalNameEdit, 1);
    sublayout2->addSpacing(10);
    sublayout2->addWidget(new QLabel(tr("Clone:")));
    sublayout2->addWidget(_cloneNameEdit, 1);
    _originalNameEdit->setPlaceholderText(sceneNode->objectTitle());
    _cloneNameEdit->setPlaceholderText(sceneNode->objectTitle());

    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Help, Qt::Horizontal, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &ClonePipelineDialog::onAccept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &ClonePipelineDialog::reject);
    connect(buttonBox, &QDialogButtonBox::helpRequested, this, [this]() {
        actionManager()->openHelpTopic("manual:clone_pipeline");
    });
    mainLayout->addWidget(buttonBox);

#ifdef OVITO_BUILD_BASIC
    buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
    QLabel* noticeWidget = new QLabel(tr("The <i>Clone pipeline</i> function is only available in OVITO Pro &mdash; the complete version of this software. Please visit <a href=\"https://www.ovito.org/#proFeatures\">www.ovito.org</a> for more information."), this);
    noticeWidget->setMargin(4);
    noticeWidget->setTextFormat(Qt::RichText);
    noticeWidget->setTextInteractionFlags(Qt::TextBrowserInteraction);
    noticeWidget->setOpenExternalLinks(true);
    noticeWidget->setWordWrap(true);
    noticeWidget->setAutoFillBackground(true);
    noticeWidget->setStyleSheet("QLabel { "
                            "  background-color: rgb(230,180,180); "
                            "}");
    mainLayout->insertWidget(0, noticeWidget);
#else
    buttonBox->button(QDialogButtonBox::Ok)->setDefault(true);
#endif

    resize(sizeHint());
}

/******************************************************************************
* Builds the initial Qt graphics scene to visualize the pipeline layout.
******************************************************************************/
void ClonePipelineDialog::initializeGraphicsScene()
{
    // Obtain the list of node that form the current pipeline.
    PipelineNode* pnode = _originalPipeline->head();
    while(pnode) {
        PipelineItemStruct s;
        s.pipelineNodes.push_back(pnode);
        if(ModificationNode* modNode = dynamic_object_cast<ModificationNode>(pnode)) {
            if(modNode->modifierGroup()) {
                if(!_pipelineItems.empty() && _pipelineItems.back().modNodes.back()->modifierGroup() == modNode->modifierGroup()) {
                    _pipelineItems.back().pipelineNodes.push_back(pnode);
                    _pipelineItems.back().modNodes.push_back(modNode);
                    pnode = modNode->input();
                    continue;
                }
                s.title = modNode->modifierGroup()->objectTitle();
            }
            else {
                s.title = modNode->modifier()->objectTitle();
            }
            s.modNodes.push_back(modNode);
            pnode = modNode->input();
        }
        else {
            s.title = tr("Source: ") + pnode->objectTitle();
            pnode = nullptr;
        }
        _pipelineItems.push_back(std::move(s));
    }

    QPen borderPen(palette().color(QPalette::WindowText));
    borderPen.setWidth(0);
    QPen thickBorderPen(palette().color(QPalette::WindowText));
    thickBorderPen.setWidth(2);
    QBrush nodeBrush(QColor(200, 200, 255));
    QBrush modifierBrush(QColor(200, 255, 200));
    QBrush sourceBrush(QColor(200, 200, 200));
    QBrush modAppBrush(palette().color(QPalette::Base).darker());
    qreal textBoxWidth = 160;
    qreal textBoxHeight = 25;
    qreal modAppRadius = 5;
    qreal objBoxIndent = textBoxWidth/2 + 20;
    qreal lineHeight = 50;
    _pipelineSeparation = 420;
    QFontMetrics fontMetrics(_pipelineScene.font());
    QFont smallFont = _pipelineScene.font();
    smallFont.setPointSizeF(smallFont.pointSizeF() * 3 / 4);

    QGraphicsSimpleTextItem* textItem;

    auto addShadowEffect = [](QGraphicsItem* item) {
#if 0   // Shadows diabled to work around Qt bug https://bugreports.qt.io/browse/QTBUG-65035
        QGraphicsDropShadowEffect* effect = new QGraphicsDropShadowEffect(this);
        effect->setOffset(3);
        effect->setBlurRadius(2);
        item->setGraphicsEffect(effect);
#endif
    };

    // Create the boxes for the two pipeline heads.
    QGraphicsRectItem* nodeItem1 = _pipelineScene.addRect(-textBoxWidth/2, -textBoxHeight/2, textBoxWidth, textBoxHeight, thickBorderPen, nodeBrush);
    nodeItem1->setZValue(1);
    textItem = _pipelineScene.addSimpleText(tr("Original pipeline"));
    textItem->setParentItem(nodeItem1);
    textItem->setPos(-textItem->boundingRect().center());
    nodeItem1->moveBy(textBoxWidth * 0.25, 0);
    addShadowEffect(nodeItem1);
    QGraphicsRectItem* nodeItem2 = _pipelineScene.addRect(-textBoxWidth/2, -textBoxHeight/2, textBoxWidth, textBoxHeight, thickBorderPen, nodeBrush);
    nodeItem2->setZValue(1);
    nodeItem2->setPos(_pipelineSeparation, 0);
    nodeItem2->moveBy(-textBoxWidth * 0.25, 0);
    addShadowEffect(nodeItem2);
    textItem = _pipelineScene.addSimpleText(tr("Cloned pipeline"));
    textItem->setParentItem(nodeItem2);
    textItem->setPos(-textItem->boundingRect().center());

    _pipelineScene.addLine(0, 0, 0, lineHeight/2, borderPen)->moveBy(0,0);
    _pipelineScene.addLine(0, 0, 0, lineHeight/2, borderPen)->moveBy(_pipelineSeparation,0);
    _joinLine = _pipelineScene.addLine(0, -lineHeight/2, _pipelineSeparation, -lineHeight/2, borderPen);
    textItem = _pipelineScene.addSimpleText(tr(" Pipeline branch "), smallFont);
    textItem->setBrush(palette().text());
    QGraphicsRectItem* boxItem = _pipelineScene.addRect(textItem->boundingRect(), borderPen, palette().base());
    boxItem->setPos(-textItem->boundingRect().center());
    boxItem->moveBy(_pipelineSeparation/2, -lineHeight/2);
    boxItem->setParentItem(_joinLine);
    textItem->setParentItem(boxItem);

    QSignalMapper* unifiedMapper = new QSignalMapper(this);
    QSignalMapper* nonunifiedMapper = new QSignalMapper(this);

    int line = 1;
    for(PipelineItemStruct& s : _pipelineItems) {
        qreal y = line * lineHeight;

        // Create vertical connector lines.
        s.connector1 = _pipelineScene.addLine(0, -lineHeight/2, 0, s.isModifier() ? lineHeight/2 : 0, borderPen);
        s.connector1->moveBy(0, y);
        s.connector2 = _pipelineScene.addLine(0, -lineHeight/2, 0, s.isModifier() ? lineHeight/2 : 0, borderPen);
        s.connector2->moveBy(_pipelineSeparation, y);
        s.connector3 = _pipelineScene.addLine(0, -lineHeight/2, 0, s.isModifier() ? lineHeight/2 : 0, borderPen);
        s.connector3->moveBy(_pipelineSeparation / 2 - objBoxIndent, y);

        // Create a circle for each modifier application:
        if(!s.isModifier()) modAppRadius = 0;
        s.modAppItem1 = _pipelineScene.addEllipse(-modAppRadius, -modAppRadius, modAppRadius*2, modAppRadius*2, borderPen, modAppBrush);
        s.modAppItem1->setParentItem(s.connector1);
        s.modAppItem2 = _pipelineScene.addEllipse(-modAppRadius, -modAppRadius, modAppRadius*2, modAppRadius*2, borderPen, modAppBrush);
        s.modAppItem2->setParentItem(s.connector2);
        s.modAppItem3 = _pipelineScene.addEllipse(-modAppRadius, -modAppRadius, modAppRadius*2, modAppRadius*2, borderPen, modAppBrush);
        s.modAppItem3->setParentItem(s.connector3);

        // Create horizontal connector lines.
        QGraphicsLineItem* horizontalConnector1 = _pipelineScene.addLine(modAppRadius, 0, (_pipelineSeparation - textBoxWidth) / 2, 0, borderPen);
        horizontalConnector1->setParentItem(s.modAppItem1);
        QGraphicsLineItem* horizontalConnector2 = _pipelineScene.addLine(-modAppRadius, 0, -(_pipelineSeparation - textBoxWidth) / 2, 0, borderPen);
        horizontalConnector2->setParentItem(s.modAppItem2);
        QGraphicsLineItem* horizontalConnector3 = _pipelineScene.addLine(modAppRadius, 0, objBoxIndent, 0, borderPen);
        horizontalConnector3->setParentItem(s.modAppItem3);

        // Create the boxes for the pipeline objects.
        QString elidedText = fontMetrics.elidedText(s.title, Qt::ElideRight, (int)textBoxWidth);
        s.objItem1 = _pipelineScene.addRect(-textBoxWidth/2, -textBoxHeight/2, textBoxWidth, textBoxHeight, borderPen, s.isModifier() ? modifierBrush : sourceBrush);
        textItem = _pipelineScene.addSimpleText(elidedText);
        textItem->setParentItem(s.objItem1);
        textItem->setPos(-textItem->boundingRect().center());
        s.objItem1->setPos(objBoxIndent, y);
        addShadowEffect(s.objItem1);
        s.objItem2 = _pipelineScene.addRect(-textBoxWidth/2, -textBoxHeight/2, textBoxWidth, textBoxHeight, borderPen, s.isModifier() ? modifierBrush : sourceBrush);
        s.objItem2->setPos(_pipelineSeparation - objBoxIndent, y);
        addShadowEffect(s.objItem2);
        textItem = _pipelineScene.addSimpleText(elidedText);
        textItem->setParentItem(s.objItem2);
        textItem->setPos(-textItem->boundingRect().center());
        s.objItem3 = _pipelineScene.addRect(-textBoxWidth/2, -textBoxHeight/2, textBoxWidth, textBoxHeight, borderPen, s.isModifier() ? modifierBrush : sourceBrush);
        textItem = _pipelineScene.addSimpleText(elidedText);
        textItem->setParentItem(s.objItem3);
        textItem->setPos(-textItem->boundingRect().center());
        s.objItem3->setPos(_pipelineSeparation / 2, y);
        addShadowEffect(s.objItem3);

        QToolBar* modeSelectorBar = new QToolBar();
        modeSelectorBar->setStyleSheet(
            "QToolBar { "
            "   padding: 4px; margin: 0px; border: 0px none black; spacing: 4px; "
            "   background: none; "
            "} "
            "QToolButton { "
            "   padding: 4px; "
            "   border-radius: 2px; "
            "   border: 1px outset #8f8f91; "
            "   background-color: rgb(220,220,220); "
            "   color: rgb(0,0,0); "
            "} "
            "QToolButton:pressed { "
            "   border-style: inset; "
            "   background-color: rgb(240,240,240); "
            "} "
            "QToolButton:checked { "
            "   border-style: inset; "
            "   background-color: rgb(180,180,220); "
            "}");
        QAction* copyAction = modeSelectorBar->addAction(tr("Copy"));
        QAction* joinAction = modeSelectorBar->addAction(tr("Join"));
        unifiedMapper->setMapping(joinAction, line-1);
        connect(joinAction, &QAction::triggered, unifiedMapper, (void (QSignalMapper::*)())&QSignalMapper::map);
        nonunifiedMapper->setMapping(copyAction, line-1);
        connect(copyAction, &QAction::triggered, nonunifiedMapper, (void (QSignalMapper::*)())&QSignalMapper::map);
        QAction* shareAction = nullptr;
        QAction* skipAction = nullptr;
        if(s.isModifier()) {
            shareAction = modeSelectorBar->addAction(tr("Share"));
            skipAction = modeSelectorBar->addAction(tr("Skip"));
            nonunifiedMapper->setMapping(shareAction, line-1);
            connect(shareAction, &QAction::triggered, nonunifiedMapper, (void (QSignalMapper::*)())&QSignalMapper::map);
            nonunifiedMapper->setMapping(skipAction, line-1);
            connect(skipAction, &QAction::triggered, nonunifiedMapper, (void (QSignalMapper::*)())&QSignalMapper::map);
        }
        copyAction->setCheckable(true);
        joinAction->setCheckable(true);
        if(shareAction) shareAction->setCheckable(true);
        if(skipAction) skipAction->setCheckable(true);
        s.actionGroup = new QActionGroup(this);
        s.actionGroup->setExclusive(true);
        s.actionGroup->addAction(copyAction);
        s.actionGroup->addAction(joinAction);
        if(shareAction) s.actionGroup->addAction(shareAction);
        if(skipAction) s.actionGroup->addAction(skipAction);
        copyAction->setData((int)CloneMode::Copy);
        joinAction->setData((int)CloneMode::Join);
        if(shareAction) shareAction->setData((int)CloneMode::Share);
        if(skipAction) skipAction->setData((int)CloneMode::Skip);
        connect(s.actionGroup, &QActionGroup::triggered, this, &ClonePipelineDialog::updateGraphicsScene);
        modeSelectorBar->setToolButtonStyle(Qt::ToolButtonTextOnly);
        QGraphicsProxyWidget* selectorItem = _pipelineScene.addWidget(modeSelectorBar);
        selectorItem->setPos(0, -selectorItem->boundingRect().center().y());
        selectorItem->moveBy(_pipelineSeparation + 40, y);
        if(s.isModifier())
            copyAction->setChecked(true);
        else
            joinAction->setChecked(true);

        if(line == 1) {
            textItem = _pipelineScene.addSimpleText(tr("Cloning mode:"));
            textItem->setBrush(palette().text());
            textItem->setPos(-textItem->boundingRect().center() + selectorItem->boundingRect().center());
            textItem->moveBy(_pipelineSeparation + 40, 0);
        }

        line++;
    }

    // When the user switches an entry to 'join', then all following entries must automatically be set to 'join' too.
    connect(unifiedMapper, &QSignalMapper::mappedInt, this, [this](int index) {
        for(; index < _pipelineItems.size(); index++) {
            _pipelineItems[index].setCloneMode(CloneMode::Join);
        }
    });

    // When the user switches to an entry other than 'join', then all preceding entries must automatically be set to something other too.
    connect(nonunifiedMapper, &QSignalMapper::mappedInt, this, [this](int index) {
        for(index--; index >= 0; index--) {
            if(_pipelineItems[index].cloneMode() == CloneMode::Join)
                _pipelineItems[index].setCloneMode(CloneMode::Copy);
        }
    });

    updateGraphicsScene();
}

/******************************************************************************
* Updates the display of the pipeline layout.
******************************************************************************/
void ClonePipelineDialog::updateGraphicsScene()
{
    _joinLine->hide();
    for(size_t i = 0; i < _pipelineItems.size(); i++) {
        const PipelineItemStruct& s = _pipelineItems[i];

        switch(s.cloneMode()) {
        case CloneMode::Copy:
            s.objItem1->show();
            s.objItem2->show();
            s.objItem3->hide();
            s.connector1->show();
            s.connector2->show();
            s.connector3->hide();
            s.modAppItem1->show();
            s.modAppItem2->show();
            s.modAppItem3->hide();
            break;
        case CloneMode::Share:
            s.objItem1->hide();
            s.objItem2->hide();
            s.objItem3->show();
            s.connector1->show();
            s.connector2->show();
            s.connector3->hide();
            s.modAppItem1->show();
            s.modAppItem2->show();
            s.modAppItem3->hide();
            break;
        case CloneMode::Join:
            s.objItem1->hide();
            s.objItem2->hide();
            s.objItem3->show();
            s.connector1->hide();
            s.connector2->hide();
            s.connector3->show();
            s.modAppItem1->hide();
            s.modAppItem2->hide();
            s.modAppItem3->show();
            if(!_joinLine->isVisible()) {
                _joinLine->setPos(0, s.objItem1->y());
                _joinLine->show();
            }
            break;
        case CloneMode::Skip:
            s.objItem1->show();
            s.objItem2->hide();
            s.objItem3->hide();
            s.connector1->show();
            s.connector2->show();
            s.connector3->hide();
            s.modAppItem1->show();
            s.modAppItem2->hide();
            s.modAppItem3->hide();
            break;
        }
    }
}

/******************************************************************************
* Is called when the user has pressed the 'Ok' button
******************************************************************************/
void ClonePipelineDialog::onAccept()
{
    setFocus(); // Remove focus from child widgets to commit newly entered values in text widgets etc.

    performTransaction(tr("Clone pipeline"), [this]() {
        if(_pipelineItems.empty())
            return;

        // Do not create any animation keys during cloning.
        AnimationSuspender animSuspender(ui());

        // Clone the scene node.
        CloneHelper cloneHelper;
        OORef<SceneNode> clonedSceneNode = cloneHelper.cloneObject(_originalSceneNode, false);
        OORef<Pipeline> clonedPipeline = cloneHelper.cloneObject(_originalPipeline, false);
        OVITO_ASSERT(clonedPipeline->head() == _originalPipeline->head());

        // The scene we are working in.
        Scene* scene = _originalSceneNode->scene();

        // Clone the pipeline nodes.
        OORef<PipelineNode> precedingNode;
        for(auto s = _pipelineItems.crbegin(); s != _pipelineItems.crend(); ++s) {
            if(s->cloneMode() == CloneMode::Join) {
                precedingNode = s->pipelineNodes.front();
            }
            else if(s->cloneMode() == CloneMode::Copy) {
                for(auto pnode = s->pipelineNodes.crbegin(); pnode != s->pipelineNodes.crend(); ++pnode) {
                    OORef<PipelineNode> clonedNode = cloneHelper.cloneObject(*pnode, false);
                    if(ModificationNode* clonedModNode = dynamic_object_cast<ModificationNode>(clonedNode.get())) {
                        clonedModNode->setInput(precedingNode);
                        clonedModNode->setModifier(cloneHelper.cloneObject(clonedModNode->modifier(), true));
                    }
                    precedingNode = std::move(clonedNode);
                }
            }
            else if(s->cloneMode() == CloneMode::Share) {
                OVITO_ASSERT(s->isModifier() && s->modNodes.size() == s->pipelineNodes.size());
                for(auto modNode = s->modNodes.crbegin(); modNode != s->modNodes.crend(); ++modNode) {
                    OORef<ModificationNode> clonedModNode = cloneHelper.cloneObject(*modNode, false);
                    clonedModNode->setInput(precedingNode);
                    precedingNode = std::move(clonedModNode);
                }
            }
            else if(s->cloneMode() == CloneMode::Skip) {
                continue;
            }
        }
        clonedPipeline->setHead(precedingNode);
        clonedSceneNode->setPipeline(clonedPipeline);

        // Give the cloned scene node the user-defined name.
        QString nodeName = _cloneNameEdit->text().trimmed();
        if(nodeName.isEmpty() == false)
            clonedSceneNode->setSceneNodeName(nodeName);

        // Give the original scene node the new user-defined name.
        nodeName = _originalNameEdit->text().trimmed();
        if(nodeName.isEmpty() == false)
            _originalSceneNode->setSceneNodeName(nodeName);

        // Translate cloned node in scene.
        int displacementMode = _displacementDirectionGroup->checkedAction()->data().toInt();
        if(displacementMode != -1) {
            AnimationTime time = scene ? scene->animationSettings()->currentTime() : AnimationTime(0);
            const Box3& bbox = _originalSceneNode->worldBoundingBox(time);
            Vector3 translation = Vector3::Zero();
            translation[displacementMode] = bbox.size(displacementMode) + FloatType(0.2) * bbox.size().length();
            clonedSceneNode->transformationController()->translate(time, translation, AffineTransformation::Identity());
        }

        // Insert cloned scene node into scene.
        if(SceneNode* parentNode = _originalSceneNode->parentNode())
            parentNode->addChildNode(clonedSceneNode);

        // Select cloned scene node.
        if(scene)
            scene->selection()->setNode(clonedSceneNode);
    });
    accept();
}

}   // End of namespace
