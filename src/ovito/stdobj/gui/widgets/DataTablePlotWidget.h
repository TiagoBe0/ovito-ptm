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


#include <ovito/stdobj/gui/StdObjGui.h>
#include <ovito/stdobj/table/DataTable.h>
#include <ovito/core/oo/RefTargetListener.h>

#include <qwt/qwt_plot.h>
#include <qwt/qwt_scale_draw.h>
#include <qwt/qwt_text.h>

class QwtPlotCurve;
class QwtPlotLegendItem;
class QwtPlotBarChart;
class QwtPlotSpectroCurve;
class QwtPlotZoomer;
class QwtPlotMagnifier;
class QwtPlotPanner;

namespace Ovito {

/**
 * \brief A widget that plots the data of a DataTable.
 */
class OVITO_STDOBJGUI_EXPORT DataTablePlotWidget : public QwtPlot
{
    Q_OBJECT

public:

    /// Constructor.
    explicit DataTablePlotWidget(QWidget* parent = nullptr);

    /// Returns the data table object currently being plotted.
    const DataOORef<const DataTable>& table() const { return _table; }

    /// Sets the data table to be plotted.
    void setTable(DataOORef<const DataTable> table, bool forceUpdate = false);

    /// Returns whether the plot widget accepts and handles mouse navigation input.
    bool mouseNavigationEnabled() const { return _mouseNavigationEnabled; }

    /// Controls whether the plot widget accepts and handles mouse navigation input.
    void setMouseNavigationEnabled(bool on) {
        _mouseNavigationEnabled = on;
    }

    void setAxisAutoScale(QwtAxisId axisId, bool on = true) {
        if(isAxisValid(axisId)) {
            _axisAutoscaleEnabled[axisId] = on;
            QwtPlot::setAxisAutoScale(axisId, on);
        }
    }

    void setAxisScale(QwtAxisId axisId, double min, double max, double stepSize = 0) {
        if(isAxisValid(axisId)) {
            _axisAutoscaleEnabled[axisId] = false;
            QwtPlot::setAxisScale(axisId, min, max, stepSize);
        }
    }

private Q_SLOTS:

    /// Regenerates the plot.
    /// This function is called whenever a new data table has been loaded into widget or if the current table data changes.
    void updateDataPlot();

private:

    /// A custom scale draw implementation for drawing the axis labels of a bar chart.
    class BarChartScaleDraw : public QwtScaleDraw
    {
    public:

        /// Constructor.
        using QwtScaleDraw::QwtScaleDraw;

        /// Sets the texts of the labels.
        void setLabels(QStringList labels) {
            _labels = std::move(labels);
            invalidateCache();
        }

        /// Returns the label text for the given axis position. Words go on separate lines so that the labels of
        /// neighboring bars do not overlap in a narrow plot.
        virtual QwtText label(double value) const override {
            QwtText lbl;
            int index = qRound(value);
            if(index >= 0 && index < _labels.size() && std::abs(value - (double)index) < 1e-1)
                lbl = QString(_labels[index]).replace(QChar(' '), QChar('\n'));
            return lbl;
        }

    private:

        QStringList _labels;
    };

private:

    /// Reference to the current data table shown in the plot widget.
    DataOORef<const DataTable> _table;

    /// The plot item(s) for standard line charts.
    std::vector<QwtPlotCurve*> _curves;

    /// The plot item(s) for scatter plots.
    std::vector<QwtPlotSpectroCurve*> _spectroCurves;

    /// The plot item for bar charts.
    QwtPlotBarChart* _barChart = nullptr;

    /// The scale draw used when plotting a bar chart.
    BarChartScaleDraw* _barChartScaleDraw = nullptr;

    /// The plot legend.
    QwtPlotLegendItem* _legend = nullptr;

    /// Controls whether the plot widget accepts and handles mouse navigation input.
    bool _mouseNavigationEnabled = true;

    /// Zoom interaction handler.
    QwtPlotZoomer* _zoomer = nullptr;

    /// Magnification interaction handler.
    QwtPlotMagnifier* _magnifier = nullptr;

    /// Panning interaction handler.
    QwtPlotPanner* _panner = nullptr;

    /// Flags controlling the automatic range of plot axes.
    std::array<bool, QwtPlot::axisCnt> _axisAutoscaleEnabled{{true, true, true, true}};
};

}   // End of namespace
