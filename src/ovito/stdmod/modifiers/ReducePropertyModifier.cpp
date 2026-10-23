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

#include <ovito/stdmod/StdMod.h>
#include <ovito/stdobj/properties/Property.h>
#include <ovito/stdobj/properties/PropertyContainer.h>
#include <ovito/core/dataset/pipeline/ModificationNode.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include "ReducePropertyModifier.h"

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(ReducePropertyModifier);
OVITO_CLASSINFO(ReducePropertyModifier, "DisplayName", "Reduce property");
OVITO_CLASSINFO(ReducePropertyModifier, "Description", "Reduce a property to a single value (min, max, sum, mean, median, ...) and output it as a global attribute.");
OVITO_CLASSINFO(ReducePropertyModifier, "ModifierCategory", "Analysis");
DEFINE_PROPERTY_FIELD(ReducePropertyModifier, sourceProperty);
DEFINE_PROPERTY_FIELD(ReducePropertyModifier, operation);
DEFINE_PROPERTY_FIELD(ReducePropertyModifier, onlySelectedElements);
SET_PROPERTY_FIELD_LABEL(ReducePropertyModifier, sourceProperty, "Input property");
SET_PROPERTY_FIELD_LABEL(ReducePropertyModifier, operation, "Reduction operation");
SET_PROPERTY_FIELD_LABEL(ReducePropertyModifier, onlySelectedElements, "Use only selected elements");

/******************************************************************************
* Constructor.
******************************************************************************/
void ReducePropertyModifier::initializeObject(ObjectInitializationFlags flags)
{
    GenericPropertyModifier::initializeObject(flags);

    // Operate on particle properties by default.
    setDefaultSubject(QStringLiteral("Particles"), QStringLiteral("Particles"));
}

/******************************************************************************
* This method is called by the system when the modifier has been inserted
* into a pipeline.
******************************************************************************/
void ReducePropertyModifier::initializeModifier(const ModifierInitializationRequest& request)
{
    GenericPropertyModifier::initializeModifier(request);

    // Use the last available property from the input state as data source when the modifier is newly created.
    if(!sourceProperty() && subject() && this_task::isInteractive()) {
        const PipelineFlowState& input = request.modificationNode()->evaluateInput(request).blockForResult();
        if(const PropertyContainer* container = input.getLeafObject(subject())) {
            PropertyReference bestProperty;
            for(const Property* property : container->properties())
                bestProperty = PropertyReference(property);
            setSourceProperty(bestProperty);
        }
    }
}

/******************************************************************************
* Returns the short name of an operation.
******************************************************************************/
QString ReducePropertyModifier::operationKey(ReductionOperation op)
{
    switch(op) {
    case Min: return QStringLiteral("min");
    case Max: return QStringLiteral("max");
    case Sum: return QStringLiteral("sum");
    case NonzeroCount: return QStringLiteral("nonzero_count");
    case Mean: return QStringLiteral("mean");
    case Median: return QStringLiteral("median");
    case Variance: return QStringLiteral("var");
    case StdDev: return QStringLiteral("std");
    case Argmin: return QStringLiteral("argmin");
    case Argmax: return QStringLiteral("argmax");
    }
    return {};
}

/******************************************************************************
* Applies a reduction operation to a list of (index, value) samples.
******************************************************************************/
double ReducePropertyModifier::reduce(ReductionOperation op, std::vector<std::pair<size_t, double>> samples)
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const size_t n = samples.size();
    auto byValue = [](const auto& a, const auto& b) { return a.second < b.second; };

    // NaN values break the ordering required by the comparison-based operations (undefined behavior in
    // std::nth_element). Like NumPy, min/max/median become NaN and argmin/argmax point to the first NaN.
    if(op == Min || op == Max || op == Median || op == Argmin || op == Argmax) {
        auto firstNaN = std::find_if(samples.begin(), samples.end(), [](const auto& s) { return std::isnan(s.second); });
        if(firstNaN != samples.end())
            return (op == Argmin || op == Argmax) ? (double)firstNaN->first : nan;
    }

    switch(op) {
    case Sum:
    case Mean: {
        double sum = 0;
        for(const auto& s : samples) sum += s.second;
        return (op == Sum) ? sum : (n ? sum / n : nan);
    }
    case NonzeroCount:
        return (double)std::count_if(samples.begin(), samples.end(), [](const auto& s) { return s.second != 0; });
    case Min:
        return n ? std::min_element(samples.begin(), samples.end(), byValue)->second : nan;
    case Max:
        return n ? std::max_element(samples.begin(), samples.end(), byValue)->second : nan;
    case Argmin:
        return n ? (double)std::min_element(samples.begin(), samples.end(), byValue)->first : nan;
    case Argmax:
        return n ? (double)std::max_element(samples.begin(), samples.end(), byValue)->first : nan;
    case Median: {
        if(!n) return nan;
        auto mid = samples.begin() + n / 2;
        std::nth_element(samples.begin(), mid, samples.end(), byValue);
        if(n % 2) return mid->second;
        // Even count: average of the two middle values (the lower one is the max of the lower half).
        double upper = mid->second;
        double lower = std::max_element(samples.begin(), mid, byValue)->second;
        return (lower + upper) / 2;
    }
    case Variance:
    case StdDev: {
        if(n < 2) return nan;
        // Two-pass algorithm for numerical stability.
        double mean = 0;
        for(const auto& s : samples) mean += s.second;
        mean /= n;
        double sq = 0;
        for(const auto& s : samples) sq += (s.second - mean) * (s.second - mean);
        double var = sq / (n - 1);
        return (op == Variance) ? var : std::sqrt(var);
    }
    }
    return nan;
}

/******************************************************************************
* Modifies the input data.
******************************************************************************/
Future<PipelineFlowState> ReducePropertyModifier::evaluateModifier(const ModifierEvaluationRequest& request, PipelineFlowState&& state)
{
    if(!subject())
        throw Exception(tr("No data element type set."));
    if(!sourceProperty())
        throw Exception(tr("No input property selected."));

    // Look up the property container object.
    const PropertyContainer* container = state.expectLeafObject(subject());
    container->verifyIntegrity();

    // Get the input property.
    QString errorDescription;
    ConstPropertyPtr property;
    int vecComponent;
    // Without an explicit component, a vector property is reduced component by component (vecComponent = -1).
    std::tie(property, vecComponent) = sourceProperty().findInContainerWithComponent(container, errorDescription, false);
    if(!property)
        throw Exception(std::move(errorDescription));

    // Get the input selection if filtering was enabled by the user.
    ConstPropertyPtr inputSelection;
    if(onlySelectedElements()) {
        if(!container->getOOMetaClass().isValidStandardPropertyId(Property::GenericSelectionProperty))
            throw Exception(tr("The selected data elements do not support selection."));
        inputSelection = container->expectProperty(Property::GenericSelectionProperty);
    }

    // The actual computation can be performed in a separate worker thread.
    return asyncLaunch([
            state = std::move(state),
            property = std::move(property),
            inputSelection = std::move(inputSelection),
            vecComponent,
            op = operation(),
            createdByNode = request.modificationNodeWeak()]() mutable
    {
        std::vector<SelectionIntType> selection;
        if(inputSelection) {
            selection.resize(inputSelection->size());
            inputSelection->copyTo(selection.begin());
        }

        // Reduce each requested component and output one attribute per component.
        std::vector<int> components;
        if(vecComponent >= 0)
            components.push_back(vecComponent);
        else
            for(int c = 0; c < (int)property->componentCount(); c++) components.push_back(c);

        size_t selectedCount = 0;
        for(int c : components) {
            std::vector<double> values(property->size());
            property->copyComponentTo(values.begin(), c);
            std::vector<std::pair<size_t, double>> samples;
            samples.reserve(values.size());
            for(size_t i = 0; i < values.size(); i++) {
                if(selection.empty() || selection[i])
                    samples.emplace_back(i, values[i]);
            }
            selectedCount = samples.size();
            double result = reduce(op, std::move(samples));
            QString name = property->nameWithComponent(property->componentCount() > 1 ? c : -1) + QChar('.') + operationKey(op);
            if(op == NonzeroCount || op == Argmin || op == Argmax) {
                if(std::isnan(result))
                    state.setAttribute(name, QVariant::fromValue(result), createdByNode);
                else
                    state.setAttribute(name, QVariant::fromValue((qlonglong)result), createdByNode);
            }
            else {
                state.setAttribute(name, QVariant::fromValue(result), createdByNode);
            }
        }

        if(inputSelection && selectedCount == 0)
            state.setStatus(PipelineStatus(PipelineStatus::Warning, tr("No elements are selected.")));
        else
            state.setStatus(PipelineStatus(PipelineStatus::Success, tr("Reduced %1 elements.").arg(inputSelection ? selectedCount : property->size())));
        return std::move(state);
    });
}

}   // End of namespace
