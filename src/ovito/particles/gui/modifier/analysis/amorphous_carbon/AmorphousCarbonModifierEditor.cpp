////////////////////////////////////////////////////////////////////////////////////////
//
//  Copyright 2026 OVITO GmbH, Germany
//
////////////////////////////////////////////////////////////////////////////////////////

#include <ovito/particles/gui/ParticlesGui.h>
#include <ovito/particles/modifier/analysis/amorphous_carbon/AmorphousCarbonModifier.h>
#include <ovito/gui/desktop/properties/BooleanParameterUI.h>
#include <ovito/gui/desktop/properties/FloatParameterUI.h>
#include <ovito/gui/desktop/properties/IntegerParameterUI.h>
#include <ovito/gui/desktop/properties/IntegerRadioButtonParameterUI.h>
#include <ovito/gui/desktop/properties/ObjectStatusDisplay.h>
#include "AmorphousCarbonModifierEditor.h"

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(AmorphousCarbonModifierEditor);
SET_OVITO_OBJECT_EDITOR(AmorphousCarbonModifier, AmorphousCarbonModifierEditor);

void AmorphousCarbonModifierEditor::createUI(const RolloutInsertionParameters& rolloutParams)
{
    QWidget* rollout = createRollout(tr("Amorphous carbon cluster analysis"), rolloutParams);
    QVBoxLayout* layout = new QVBoxLayout(rollout);
    layout->setContentsMargins(4,4,4,4);
    layout->setSpacing(6);

    QGroupBox* analysisBox = new QGroupBox(tr("Bonding and hybridization"));
    layout->addWidget(analysisBox);
    QGridLayout* grid = new QGridLayout(analysisBox);
    grid->setColumnStretch(1, 1);

    FloatParameterUI* coordinationCutoffUI = createParamUI<FloatParameterUI>(
        PROPERTY_FIELD(AmorphousCarbonModifier::coordinationCutoff));
    grid->addWidget(coordinationCutoffUI->label(), 0, 0);
    grid->addLayout(coordinationCutoffUI->createFieldLayout(), 0, 1);

    FloatParameterUI* bondCutoffUI = createParamUI<FloatParameterUI>(
        PROPERTY_FIELD(AmorphousCarbonModifier::bondCutoff));
    grid->addWidget(bondCutoffUI->label(), 1, 0);
    grid->addLayout(bondCutoffUI->createFieldLayout(), 1, 1);

    IntegerParameterUI* minimumSizeUI = createParamUI<IntegerParameterUI>(
        PROPERTY_FIELD(AmorphousCarbonModifier::minimumClusterSize));
    grid->addWidget(minimumSizeUI->label(), 2, 0);
    grid->addLayout(minimumSizeUI->createFieldLayout(), 2, 1);

    IntegerParameterUI* minSamePhaseUI = createParamUI<IntegerParameterUI>(
        PROPERTY_FIELD(AmorphousCarbonModifier::minSamePhaseNeighbors));
    minSamePhaseUI->label()->setToolTip(
        tr("Cluster only atoms having at least this many neighbors of the same phase.\n"
           "0 clusters the whole phase. In a network above the percolation threshold that\n"
           "is degenerate: the largest cluster stays at ~99% of the phase however much\n"
           "damage accumulates. 3 clusters the well-embedded core instead, which does\n"
           "fragment and can lose percolation."));
    grid->addWidget(minSamePhaseUI->label(), 3, 0);
    grid->addLayout(minSamePhaseUI->createFieldLayout(), 3, 1);

    QGroupBox* speciesBox = new QGroupBox(tr("Connected domains"));
    layout->addWidget(speciesBox);
    QVBoxLayout* speciesLayout = new QVBoxLayout(speciesBox);
    IntegerRadioButtonParameterUI* speciesUI =
        createParamUI<IntegerRadioButtonParameterUI>(
            PROPERTY_FIELD(AmorphousCarbonModifier::clusterSpecies));
    speciesLayout->addWidget(speciesUI->addRadioButton(
        AmorphousCarbonModifier::ClusterSp2, tr("Cluster sp2 atoms")));
    speciesLayout->addWidget(speciesUI->addRadioButton(
        AmorphousCarbonModifier::ClusterSp3, tr("Cluster sp3 atoms")));

    QGroupBox* ringBox = new QGroupBox(tr("Ring statistics"));
    layout->addWidget(ringBox);
    QGridLayout* ringGrid = new QGridLayout(ringBox);
    ringGrid->setColumnStretch(1, 1);

    BooleanParameterUI* ringEnabledUI = createParamUI<BooleanParameterUI>(
        PROPERTY_FIELD(AmorphousCarbonModifier::ringAnalysisEnabled));
    ringEnabledUI->checkBox()->setToolTip(
        tr("Finds closed primitive (shortest-path) rings in the selected phase's bond graph --\n"
           "the Franzblau criterion used for ring statistics in amorphous carbon and silica.\n"
           "Costs meaningfully more than clustering alone on a large percolating network,\n"
           "since it runs a bounded search from every bond, so it is off by default."));
    ringGrid->addWidget(ringEnabledUI->checkBox(), 0, 0, 1, 2);

    IntegerParameterUI* minRingSizeUI = createParamUI<IntegerParameterUI>(
        PROPERTY_FIELD(AmorphousCarbonModifier::minRingSize));
    minRingSizeUI->label()->setToolTip(
        tr("Smallest ring size (in atoms) the search reports. The smallest ring possible at\n"
           "all is a 3-membered one (a triangle); 1- and 2-atom \"rings\" cannot occur in a\n"
           "simple bond graph. Raise this to exclude small rings (e.g. triangles) that you\n"
           "consider noise rather than real closed carbon rings."));
    ringGrid->addWidget(minRingSizeUI->label(), 1, 0);
    ringGrid->addLayout(minRingSizeUI->createFieldLayout(), 1, 1);

    IntegerParameterUI* maxRingSizeUI = createParamUI<IntegerParameterUI>(
        PROPERTY_FIELD(AmorphousCarbonModifier::maxRingSize));
    maxRingSizeUI->label()->setToolTip(
        tr("Largest ring size (in atoms) the search looks for. Bounds the search depth, so\n"
           "this is the main cost control: real rings in amorphous carbon are almost always\n"
           "well under 10 atoms, and raising this mainly spends time on bonds that never\n"
           "close into a small ring."));
    ringGrid->addWidget(maxRingSizeUI->label(), 2, 0);
    ringGrid->addLayout(maxRingSizeUI->createFieldLayout(), 2, 1);

    QLabel* ringLegend = new QLabel(tr(
        "Adds \"Ring Size\", \"Smallest Ring Identifier\" and \"Ring Membership Count\" particle "
        "properties, plus per-frame ring counts by size. Select a single ring with Expression "
        "Selection (e.g. SmallestRingIdentifier==42) to isolate and count its atoms."));
    ringLegend->setWordWrap(true);
    ringGrid->addWidget(ringLegend, 3, 0, 1, 2);

    QGroupBox* colorBox = new QGroupBox(tr("Visualization"));
    layout->addWidget(colorBox);
    QVBoxLayout* colorLayout = new QVBoxLayout(colorBox);
    IntegerRadioButtonParameterUI* colorUI =
        createParamUI<IntegerRadioButtonParameterUI>(
            PROPERTY_FIELD(AmorphousCarbonModifier::colorMode));
    colorLayout->addWidget(colorUI->addRadioButton(
        AmorphousCarbonModifier::NoColoring, tr("Preserve existing colors")));
    colorLayout->addWidget(colorUI->addRadioButton(
        AmorphousCarbonModifier::ColorByHybridization, tr("Color by hybridization")));
    colorLayout->addWidget(colorUI->addRadioButton(
        AmorphousCarbonModifier::ColorByCluster, tr("Color by cluster identifier")));
    colorLayout->addWidget(colorUI->addRadioButton(
        AmorphousCarbonModifier::ColorByClusterSize, tr("Color by cluster size")));
    QRadioButton* samePhaseButton = colorUI->addRadioButton(
        AmorphousCarbonModifier::ColorBySamePhaseCoordination, tr("Color by same-phase coordination"));
    samePhaseButton->setToolTip(
        tr("Dark where an atom keeps all four same-phase neighbors, light where it sits\n"
           "at the edge of its phase. Shows where the network erodes in space, which the\n"
           "cluster picture cannot while the phase still percolates."));
    colorLayout->addWidget(samePhaseButton);
    QRadioButton* ringSizeButton = colorUI->addRadioButton(
        AmorphousCarbonModifier::ColorByRingSize, tr("Color by ring size"));
    ringSizeButton->setToolTip(
        tr("Light = small rings, dark = large rings (up to the maximum ring size above);\n"
           "atoms outside any found ring stay graphite. Selecting this automatically runs\n"
           "the ring search even if \"Detect closed rings\" above is left unchecked."));
    colorLayout->addWidget(ringSizeButton);

    QLabel* legend = new QLabel(tr(
        "Hybridization colors: sp = indigo, sp2 = amber, sp3 = sky blue, other = graphite."));
    legend->setWordWrap(true);
    layout->addWidget(legend);
    layout->addWidget(createParamUI<ObjectStatusDisplay>()->statusWidget());
}

} // End of namespace
