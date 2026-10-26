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
#include <ovito/gui/desktop/properties/SubObjectParameterUI.h>
#include <ovito/gui/desktop/properties/ColorParameterUI.h>
#include <ovito/gui/desktop/properties/IntegerParameterUI.h>
#include <ovito/gui/desktop/properties/StringParameterUI.h>
#include <ovito/gui/desktop/properties/BooleanParameterUI.h>
#include <ovito/gui/desktop/properties/IntegerRadioButtonParameterUI.h>
#include <ovito/gui/desktop/properties/BooleanRadioButtonParameterUI.h>
#include <ovito/gui/desktop/dialogs/SaveImageFileDialog.h>
#include <ovito/gui/desktop/mainwin/MainWindow.h>
#include <ovito/gui/desktop/mainwin/ViewportsPanel.h>
#include <ovito/gui/desktop/widgets/general/HtmlListWidget.h>
#include <ovito/gui/base/actions/ActionManager.h>
#include <ovito/core/rendering/RenderSettings.h>
#include <ovito/core/rendering/SceneRenderer.h>
#include <ovito/core/app/PluginManager.h>
#include "RenderSettingsEditor.h"

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(RenderSettingsEditor);
DEFINE_REFERENCE_FIELD(RenderSettingsEditor, activeViewport);
SET_OVITO_OBJECT_EDITOR(RenderSettings, RenderSettingsEditor);

// Predefined output image dimensions.
static const int imageSizePresets[][2] = {
        { 320, 240 },
        { 640, 480 },
        { 600, 600 },
        { 800, 600 },
        { 1024, 768 },
        { 1000, 1000 },
        { 1600, 1200 },
};

/******************************************************************************
* Constructor that creates the UI controls for the editor.
******************************************************************************/
void RenderSettingsEditor::createUI(const RolloutInsertionParameters& rolloutParams)
{
    // Create the rollout.
    QWidget* rollout = createRollout(tr("Render settings"), rolloutParams, "manual:core.render_settings");

    QVBoxLayout* layout = new QVBoxLayout(rollout);
    layout->setContentsMargins(4,4,4,4);

    // Rendering range
    {
        QGroupBox* groupBox = new QGroupBox(tr("Rendering range"));
        layout->addWidget(groupBox);

        QVBoxLayout* layout2 = new QVBoxLayout(groupBox);
        layout2->setContentsMargins(4,4,4,4);
        layout2->setSpacing(2);
        QGridLayout* layout2c = new QGridLayout();
        layout2c->setContentsMargins(0,0,0,0);
        layout2c->setSpacing(2);
        layout2->addLayout(layout2c);

        IntegerRadioButtonParameterUI* renderingRangeTypeUI =
            createParamUI<IntegerRadioButtonParameterUI>(PROPERTY_FIELD(RenderSettings::renderingRangeType));

        QRadioButton* currentFrameButton = renderingRangeTypeUI->addRadioButton(RenderSettings::CURRENT_FRAME, tr("Single frame"));
        layout2c->addWidget(currentFrameButton, 0, 0, 1, 5);

        QRadioButton* animationIntervalButton = renderingRangeTypeUI->addRadioButton(RenderSettings::ANIMATION_INTERVAL, tr("Complete animation"));
        layout2c->addWidget(animationIntervalButton, 1, 0, 1, 5);

        QRadioButton* customIntervalButton = renderingRangeTypeUI->addRadioButton(RenderSettings::CUSTOM_INTERVAL, tr("Range:"));
        layout2c->addWidget(customIntervalButton, 2, 0, 1, 5);

        IntegerParameterUI* customRangeStartUI = createParamUI<IntegerParameterUI>(PROPERTY_FIELD(RenderSettings::customRangeStart));
        customRangeStartUI->setEnabled(false);
        layout2c->addLayout(customRangeStartUI->createFieldLayout(), 3, 1);
        layout2c->addWidget(new QLabel(tr("to")), 3, 2);
        IntegerParameterUI* customRangeEndUI = createParamUI<IntegerParameterUI>(PROPERTY_FIELD(RenderSettings::customRangeEnd));
        customRangeEndUI->setEnabled(false);
        layout2c->addLayout(customRangeEndUI->createFieldLayout(), 3, 3);
        layout2c->setColumnMinimumWidth(0, 30);
        layout2c->setColumnStretch(4, 1);
        connect(customIntervalButton, &QRadioButton::toggled, customRangeStartUI, &IntegerParameterUI::setEnabled);
        connect(customIntervalButton, &QRadioButton::toggled, customRangeEndUI, &IntegerParameterUI::setEnabled);

        QGridLayout* layout2a = new QGridLayout();
        layout2a->setContentsMargins(0,6,0,0);
        layout2a->setSpacing(2);
        layout2->addLayout(layout2a);
        IntegerParameterUI* everyNthFrameUI = createParamUI<IntegerParameterUI>(PROPERTY_FIELD(RenderSettings::everyNthFrame));
        layout2a->addWidget(everyNthFrameUI->label(), 0, 0);
        layout2a->addLayout(everyNthFrameUI->createFieldLayout(), 0, 1);
        IntegerParameterUI* fileNumberBaseUI = createParamUI<IntegerParameterUI>(PROPERTY_FIELD(RenderSettings::fileNumberBase));
        layout2a->addWidget(fileNumberBaseUI->label(), 1, 0);
        layout2a->addLayout(fileNumberBaseUI->createFieldLayout(), 1, 1);
        layout2a->setColumnStretch(2, 1);
        connect(currentFrameButton, &QRadioButton::toggled, everyNthFrameUI, &IntegerParameterUI::setDisabled);
        connect(currentFrameButton, &QRadioButton::toggled, fileNumberBaseUI, &IntegerParameterUI::setDisabled);

        QPushButton* animSettingsBtn = new QPushButton(tr("Animation settings..."));
        layout2->addWidget(animSettingsBtn);
        connect(animSettingsBtn, &QPushButton::clicked, ui().actionManager()->getAction(ACTION_ANIMATION_SETTINGS), &QAction::trigger);
    }

    // Output size
    BooleanParameterUI* renderAllViewportsUI;
    {
        QGroupBox* groupBox = new QGroupBox(tr("Output image size"));
        layout->addWidget(groupBox);
        QGridLayout* layout2 = new QGridLayout(groupBox);
        layout2->setContentsMargins(4,4,4,4);
        layout2->setSpacing(2);
        layout2->setColumnStretch(1, 1);

        // Image width parameter.
        IntegerParameterUI* imageWidthUI = createParamUI<IntegerParameterUI>(PROPERTY_FIELD(RenderSettings::outputImageWidth));
        layout2->addWidget(imageWidthUI->label(), 0, 0);
        layout2->addLayout(imageWidthUI->createFieldLayout(), 0, 1);

        // Image height parameter.
        IntegerParameterUI* imageHeightUI = createParamUI<IntegerParameterUI>(PROPERTY_FIELD(RenderSettings::outputImageHeight));
        layout2->addWidget(imageHeightUI->label(), 1, 0);
        layout2->addLayout(imageHeightUI->createFieldLayout(), 1, 1);

        _sizePresetsBox = new QComboBox(groupBox);
        _sizePresetsBox->addItem(tr("Presets..."));
        _sizePresetsBox->insertSeparator(1);
        for(size_t i = 0; i < std::size(imageSizePresets); i++)
            _sizePresetsBox->addItem(tr("%1 x %2").arg(imageSizePresets[i][0]).arg(imageSizePresets[i][1]));
        connect(_sizePresetsBox, qOverload<int>(&QComboBox::activated), this, &RenderSettingsEditor::onSizePresetActivated);
        layout2->addWidget(_sizePresetsBox, 0, 2);

        QVBoxLayout* sublayout = new QVBoxLayout();
        sublayout->setContentsMargins(0,2,0,0);
        layout2->addLayout(sublayout, 2, 0, 1, 3);

        _viewportPreviewModeBox = new QCheckBox(tr("Preview visible region"));
        sublayout->addWidget(_viewportPreviewModeBox);
        connect(&datasetContainer(), &DataSetContainer::activeViewportChanged, this, &RenderSettingsEditor::onActiveViewportChanged);
        connect(_viewportPreviewModeBox, &QCheckBox::clicked, this, &RenderSettingsEditor::onViewportPreviewModeToggled);
        onActiveViewportChanged(activeViewport());

        renderAllViewportsUI = createParamUI<BooleanParameterUI>(PROPERTY_FIELD(RenderSettings::renderAllViewports));
        sublayout->addWidget(renderAllViewportsUI->checkBox());
#ifdef OVITO_BUILD_BASIC
        renderAllViewportsUI->setEnabled(false);
        renderAllViewportsUI->checkBox()->setText(tr("%1 (OVITO Pro)").arg(renderAllViewportsUI->checkBox()->text()));
#endif
    }

    // Render output
    {
        QGroupBox* groupBox = new QGroupBox(tr("Render output"));
        layout->addWidget(groupBox);
        QGridLayout* layout2 = new QGridLayout(groupBox);
        layout2->setContentsMargins(4,4,4,4);
        layout2->setSpacing(2);
        layout2->setColumnStretch(0, 1);

        BooleanParameterUI* saveFileUI = createParamUI<BooleanParameterUI>(PROPERTY_FIELD(RenderSettings::saveToFile));
        layout2->addWidget(saveFileUI->checkBox(), 0, 0);

        QPushButton* chooseFilenameBtn = new QPushButton(tr("Choose..."), rollout);
        connect(chooseFilenameBtn, &QPushButton::clicked, this, &RenderSettingsEditor::onChooseImageFilename);
        layout2->addWidget(chooseFilenameBtn, 0, 1);

        // Output filename parameter.
        StringParameterUI* imageFilenameUI = createParamUI<StringParameterUI>(PROPERTY_FIELD(RenderSettings::imageFilename));
        imageFilenameUI->setEnabled(false);
        layout2->addWidget(imageFilenameUI->textBox(), 1, 0, 1, 2);
    }

    // Background
    {
        QGroupBox* groupBox = new QGroupBox(tr("Background"));
        layout->addWidget(groupBox);
        QGridLayout* layout2 = new QGridLayout(groupBox);
        layout2->setContentsMargins(4,4,4,4);
        layout2->setSpacing(2);

        // Background color parameter.
        ColorParameterUI* backgroundColorPUI = createParamUI<ColorParameterUI>(PROPERTY_FIELD(RenderSettings::backgroundColorController));
        layout2->addWidget(backgroundColorPUI->colorPicker(), 0, 1, 1, 2);

        // Alpha channel.
        BooleanRadioButtonParameterUI* generateAlphaUI = createParamUI<BooleanRadioButtonParameterUI>(PROPERTY_FIELD(RenderSettings::generateAlphaChannel));
        layout2->addWidget(generateAlphaUI->buttonFalse(), 0, 0, 1, 1);
        layout2->addWidget(generateAlphaUI->buttonTrue(), 1, 0, 1, 3);
        generateAlphaUI->buttonFalse()->setText(tr("Color:"));
        generateAlphaUI->buttonTrue()->setText(tr("Transparent"));
    }

#ifndef Q_OS_MACOS
    QHBoxLayout* sublayout = new QHBoxLayout();
    sublayout->setContentsMargins(4,4,4,4);
    sublayout->setSpacing(4);
#else
    QHBoxLayout* sublayout = new QHBoxLayout();
    sublayout->setContentsMargins(0,0,0,0);
    sublayout->setSpacing(4);
#endif
    layout->addLayout(sublayout);

    // Create render button.
    QPushButton* renderButton = new QPushButton();
    renderButton->setAutoDefault(true);
    QAction* renderAction = ui().actionManager()->getAction(ACTION_RENDER_ACTIVE_VIEWPORT);
    renderButton->setText(tr("Render active viewport"));
    renderButton->setIcon(renderAction->icon());
    connect(renderButton, &QPushButton::clicked, renderAction, &QAction::trigger);
    connect(renderAllViewportsUI->checkBox(), &QAbstractButton::toggled, this, [=](bool checked) {
        renderButton->setText(checked ? tr("Render all viewports") : tr("Render active viewport"));
    });
    sublayout->addWidget(renderButton, 3);

    // Create 'Switch renderer' button.
    QPushButton* switchRendererButton = new QPushButton(tr("Switch renderer..."));
    connect(switchRendererButton, &QPushButton::clicked, this, &RenderSettingsEditor::onSwitchRenderer);
#ifndef Q_OS_MACOS
    sublayout->addWidget(switchRendererButton, 1);
#else
    switchRendererButton->setToolTip(switchRendererButton->text());
    switchRendererButton->setText({});
    switchRendererButton->setIcon(QIcon::fromTheme("application_preferences"));
    sublayout->addWidget(switchRendererButton, 1);
#endif

    // Open a sub-editor for the renderer.
    createParamUI<SubObjectParameterUI>(PROPERTY_FIELD(RenderSettings::renderer), rolloutParams.after(rollout));
}

/******************************************************************************
* Lets the user choose a filename for the output image.
******************************************************************************/
void RenderSettingsEditor::onChooseImageFilename()
{
    RenderSettings* settings = static_object_cast<RenderSettings>(editObject());
    if(!settings) return;

    SaveImageFileDialog fileDialog(container(), tr("Output image file"), true, settings->imageInfo());
    if(fileDialog.exec()) {
        performTransaction(tr("Change output file"), [settings, &fileDialog]() {
            settings->setImageInfo(fileDialog.imageInfo());
            settings->setSaveToFile(true);
        });
    }
}

/******************************************************************************
* Is called when the user selects an output size preset from the drop-down list.
******************************************************************************/
void RenderSettingsEditor::onSizePresetActivated(int index)
{
    RenderSettings* settings = static_object_cast<RenderSettings>(editObject());
    if(settings && index >= 2 && index < 2 + std::size(imageSizePresets)) {
        performTransaction(tr("Change output dimensions"), [settings, index]() {
            settings->setOutputImageWidth(imageSizePresets[index-2][0]);
            settings->setOutputImageHeight(imageSizePresets[index-2][1]);
            PROPERTY_FIELD(RenderSettings::outputImageWidth)->memorizeDefaultValue(settings);
            PROPERTY_FIELD(RenderSettings::outputImageHeight)->memorizeDefaultValue(settings);
        });
    }
    _sizePresetsBox->setCurrentIndex(0);
}

/******************************************************************************
* Lets the user choose a different plug-in rendering engine.
******************************************************************************/
void RenderSettingsEditor::onSwitchRenderer()
{
    RenderSettings* settings = static_object_cast<RenderSettings>(editObject());
    if(!settings) return;

    std::vector<OvitoClassPtr> rendererClasses = PluginManager::instance().listClasses(SceneRenderer::OOClass());

    // Filter out internal renderer implementations, which should not be visible to the user.
    // Internal renderer implementation have no UI description string.
    std::erase_if(rendererClasses, [](OvitoClassPtr clazz) { return clazz->descriptionString().isEmpty(); });

    // Preferred ordering of renderers:
    const QStringList displayOrdering = {
        "OpenGLRenderer",
        "TachyonRenderer",
        "OSPRayRenderer",
        "OffscreenAnariRenderer"
    };
    std::sort(rendererClasses.begin(), rendererClasses.end(), [&displayOrdering](OvitoClassPtr a, OvitoClassPtr b) {
        int ia = displayOrdering.indexOf(a->name());
        int ib = displayOrdering.indexOf(b->name());
        if(ia == -1 && ib == -1) return a->displayName() < b->displayName();
        else if(ia == -1) return false;
        else if(ib == -1) return true;
        else return ia < ib;
    });

    QDialog dlg(container());
    dlg.setWindowTitle(tr("Switch renderer"));
    QGridLayout* layout = new QGridLayout(&dlg);

    QLabel* label = new QLabel(tr("Select the rendering engine to be used for generating output images and movies."));
    label->setWordWrap(true);
    layout->addWidget(label, 0, 0, 1, 2);

    QListWidget* rendererListWidget = new HtmlListWidget(&dlg);
    for(OvitoClassPtr clazz : rendererClasses) {
        QString text = QStringLiteral("<p style=\"font-weight: bold;\">") + clazz->displayName() + QStringLiteral("</p>");
        QString description = clazz->descriptionString();
        if(!description.isEmpty())
            text += QStringLiteral("<p style=\"font-size: small;\">") + description + QStringLiteral("</p>");
        QListWidgetItem* item = new QListWidgetItem(text, rendererListWidget);
        if(settings->renderer() && &settings->renderer()->getOOClass() == clazz)
            rendererListWidget->setCurrentItem(item);
    }
    layout->addWidget(rendererListWidget, 1, 0, 1, 2);
    layout->setRowStretch(1, 1);
    layout->setColumnStretch(1, 1);

    QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Help);
    connect(buttonBox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(buttonBox, &QDialogButtonBox::helpRequested, this, [&]() {
        ui().actionManager()->openHelpTopic("manual:usage.rendering");
    });
    connect(rendererListWidget, &QListWidget::itemDoubleClicked, &dlg, &QDialog::accept);
    layout->addWidget(buttonBox, 2, 1, Qt::AlignRight);

    if(dlg.exec() != QDialog::Accepted)
        return;

    QList<QListWidgetItem*> selItems = rendererListWidget->selectedItems();
    if(selItems.empty()) return;

    int newIndex = rendererListWidget->row(selItems.front());
    if(!settings->renderer() || &settings->renderer()->getOOClass() != rendererClasses[newIndex]) {
        performTransaction(tr("Switch renderer"), [settings, newIndex, &rendererClasses]() {
            OORef<SceneRenderer> renderer = static_object_cast<SceneRenderer>(rendererClasses[newIndex]->createInstance());
            settings->setRenderer(std::move(renderer));
        });
    }
}

/******************************************************************************
* This is called when another viewport became active.
******************************************************************************/
void RenderSettingsEditor::onActiveViewportChanged(Viewport* activeViewport)
{
    _activeViewport.set(this, PROPERTY_FIELD(activeViewport), activeViewport);
}

/******************************************************************************
* This method is called when a referenced object has changed.
******************************************************************************/
bool RenderSettingsEditor::referenceEvent(RefTarget* source, const ReferenceEvent& event)
{
    if(source == activeViewport() && event.type() == ReferenceEvent::TargetChanged) {
        _viewportPreviewModeBox->setChecked(activeViewport()->renderPreviewMode());
    }
    return PropertiesEditor::referenceEvent(source, event);
}

/******************************************************************************
* Gets called when the data provider of the pipeline has been replaced.
******************************************************************************/
void RenderSettingsEditor::referenceReplaced(const PropertyFieldDescriptor* field, RefTarget* oldTarget, RefTarget* newTarget, int listIndex)
{
    if(field == PROPERTY_FIELD(activeViewport)) {
        _viewportPreviewModeBox->setEnabled(activeViewport() != nullptr);
        _viewportPreviewModeBox->setChecked(activeViewport() && activeViewport()->renderPreviewMode());
    }
    PropertiesEditor::referenceReplaced(field, oldTarget, newTarget, listIndex);
}

/******************************************************************************
* Is called when the user toggles the preview mode checkbox.
******************************************************************************/
void RenderSettingsEditor::onViewportPreviewModeToggled(bool checked)
{
    if(RenderSettings* rs = static_object_cast<RenderSettings>(editObject())) {
        performTransaction(tr("Toggle preview mode"), [&]() {
            if(!rs->renderAllViewports()) {
                if(activeViewport())
                    activeViewport()->setRenderPreviewMode(checked);
            }
            else {
                if(ViewportConfiguration* viewportConfig = datasetContainer().activeViewportConfig()) {
                    for(Viewport* vp : viewportConfig->viewports())
                        vp->setRenderPreviewMode(checked);
                }
            }
        });
    }
}

}   // End of namespace
