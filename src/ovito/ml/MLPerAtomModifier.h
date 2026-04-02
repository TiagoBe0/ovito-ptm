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

#include <ovito/core/dataset/pipeline/Modifier.h>
#include <ovito/core/dataset/pipeline/PipelineFlowState.h>
#include <ovito/particles/Particles.h>

namespace Ovito {

/**
 * \brief General per-atom ML modifier.
 *
 * Supports two basic task modes:
 *   0 = classification (writes Int32 labels)
 *   1 = regression     (writes Float32 scalar)
 */
class OVITO_MLPLUGIN_EXPORT MLPerAtomModifier : public Modifier
{
    class OOMetaClass : public Modifier::OOMetaClass
    {
    public:
        using Modifier::OOMetaClass::OOMetaClass;
        virtual bool isApplicableTo(const DataCollection& input) const override;
    };

    OVITO_CLASS_META(MLPerAtomModifier, OOMetaClass)

public:

    virtual QString objectTitle() const override { return tr("ML Per-Atom Modifier"); }

    virtual Future<PipelineFlowState> evaluateModifier(
        const ModifierEvaluationRequest& request,
        PipelineFlowState&& input) override;

private:

    /// Path to TorchScript model (.pt).
    DECLARE_MODIFIABLE_PROPERTY_FIELD(QString{}, modelPath, setModelPath);

    /// Descriptor cutoff radius [Å].
    DECLARE_MODIFIABLE_PROPERTY_FIELD(FloatType{5.0}, cutoffRadius, setCutoffRadius);

    /// Number of sorted neighbor distances in descriptor.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(int{16}, numNeighbors, setNumNeighbors);

    /// Name of output property written by this modifier.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(QString{QStringLiteral("ML_Output")}, outputPropertyName, setOutputPropertyName);

    /// Task mode: 0=classification, 1=regression.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(int{0}, taskType, setTaskType);
};

}  // namespace Ovito
