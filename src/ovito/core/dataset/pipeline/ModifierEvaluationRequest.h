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


#include <ovito/core/Core.h>
#include <ovito/core/dataset/pipeline/PipelineEvaluationRequest.h>
#include <ovito/core/dataset/pipeline/ModificationNode.h>

namespace Ovito {

/**
 * Data structure representing the evaluation of a modification pipeline node.
 */
class ModifierEvaluationRequest : public PipelineEvaluationRequest
{
public:

    /// Constructor.
    ModifierEvaluationRequest(const PipelineEvaluationRequest& pipelineRequest, const ModificationNode* node) :
        PipelineEvaluationRequest(pipelineRequest), _modificationNode(const_cast<ModificationNode*>(node)) {}

    /// Constructor.
    ModifierEvaluationRequest(AnimationTime time, bool throwOnError, bool interactiveMode, const ModificationNode* node) :
        PipelineEvaluationRequest(time, throwOnError, interactiveMode), _modificationNode(const_cast<ModificationNode*>(node)) {}

    /// Returns the modification node being evaluated.
    ModificationNode* modificationNode() const { return _modificationNode; }

    /// Returns a weak reference to the modification node being evaluated.
    OOWeakRef<const PipelineNode> modificationNodeWeak() const { return _modificationNode; }

    /// Returns the modifier being evaluated.
    Modifier* modifier() const;

private:

    /// The modification pipeline node being evaluated.
    OORef<ModificationNode> _modificationNode;
};

// Data structure passed to Modifier::initializeModifier():
using ModifierInitializationRequest = ModifierEvaluationRequest;

}   // End of namespace

namespace Ovito {

/// Returns the modifier being evaluated.
inline Modifier* ModifierEvaluationRequest::modifier() const {
    return modificationNode()->modifier();
}

}   // End of namespace
