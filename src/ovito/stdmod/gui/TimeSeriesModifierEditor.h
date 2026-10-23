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

#pragma once


#include <ovito/stdmod/gui/StdModGui.h>
#include <ovito/gui/desktop/properties/PropertiesEditor.h>
#include <ovito/stdobj/gui/widgets/DataTablePlotWidget.h>

namespace Ovito {

/**
 * A properties editor for the TimeSeriesModifier class.
 */
class TimeSeriesModifierEditor : public PropertiesEditor
{
    OVITO_CLASS(TimeSeriesModifierEditor)
    Q_OBJECT

protected:

    /// Creates the user interface controls for the editor.
    virtual void createUI(const RolloutInsertionParameters& rolloutParams) override;

protected Q_SLOTS:

    /// Rebuilds the list of available input attributes and the time axis selector.
    void updateAttributeLists();

    /// Is called when the user checks/unchecks an item in the attributes list.
    void onSourceAttributesChanged();

    /// Is called when the user picks a different time axis attribute.
    void onTimeAttributeChanged();

    /// Replots the time series computed by the modifier.
    void plotTimeSeries();

private:

    /// Lists the global attributes that can be sampled.
    QListWidget* _attributesList;

    /// Selects the attribute serving as time axis.
    QComboBox* _timeAttributeBox;

    /// The graph widget to display the plot.
    DataTablePlotWidget* _plotWidget;
};

}   // End of namespace
