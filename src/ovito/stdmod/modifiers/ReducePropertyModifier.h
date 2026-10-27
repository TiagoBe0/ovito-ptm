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


#include <ovito/stdmod/StdMod.h>
#include <ovito/stdobj/properties/GenericPropertyModifier.h>
#include <ovito/stdobj/properties/PropertyReference.h>

namespace Ovito {

/**
 * \brief Reduces a property to a single value per vector component (min, max, sum, mean, ...)
 *        and outputs the result as a global attribute.
 *
 * The attribute is named like in OVITO Pro, "ReduceProperty.<container>.<Operation>.<property>", e.g.
 * "ReduceProperty.particles.Mean.Potential Energy" or, for each component of a vector property,
 * "ReduceProperty.particles.Max.Velocity.X".
 */
class OVITO_STDMOD_EXPORT ReducePropertyModifier : public GenericPropertyModifier
{
    OVITO_CLASS(ReducePropertyModifier)

public:

    /// The supported reduction operations.
    enum ReductionOperation {
        Min,
        Max,
        Sum,
        NonzeroCount,
        Mean,
        Median,
        Variance,       ///< Sample variance (n - 1 in the denominator).
        StdDev,         ///< Sample standard deviation.
        Argmin,         ///< Index of the lowest value in the full element list.
        Argmax          ///< Index of the highest value in the full element list.
    };
    Q_ENUM(ReductionOperation);

    /// Returns the short name of an operation, used in the output attribute name.
    static QString operationKey(ReductionOperation op);

    /// Returns the name of the output attribute for the given container path (e.g. "particles/bonds"), operation and property.
    static QString attributeName(const QString& containerPath, ReductionOperation op, const QString& propertyName);

    /// Applies a reduction operation to a list of (index, value) samples. Throws for an empty list
    /// and, for the variance and standard deviation, for fewer than two samples.
    static double reduce(ReductionOperation op, std::vector<std::pair<size_t, double>> samples);

    /// Constructor.
    void initializeObject(ObjectInitializationFlags flags);

    /// This method is called by the system after the modifier has been inserted into a data pipeline.
    virtual void initializeModifier(const ModifierInitializationRequest& request) override;

    /// Modifies the input data.
    virtual Future<PipelineFlowState> evaluateModifier(const ModifierEvaluationRequest& request, PipelineFlowState&& state) override;

    /// Returns a short piece of information to be displayed next to the modifier's title in the pipeline editor list.
    virtual QVariant getPipelineEditorShortInfo(Scene* scene, ModificationNode* node) const override {
        return tr("%1 of %2").arg(operationKey(operation())).arg(sourceProperty().nameWithComponent());
    }

private:

    /// The property to be reduced (if it has no component, all components are reduced individually).
    DECLARE_MODIFIABLE_PROPERTY_FIELD(PropertyReference{}, sourceProperty, setSourceProperty);

    /// The reduction operation.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(ReductionOperation{Mean}, operation, setOperation, PROPERTY_FIELD_MEMORIZE);

    /// Restricts the reduction to the currently selected elements.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(bool{false}, onlySelectedElements, setOnlySelectedElements);
};

}   // End of namespace
