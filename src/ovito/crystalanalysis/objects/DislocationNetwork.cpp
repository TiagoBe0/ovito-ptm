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

#include <ovito/crystalanalysis/CrystalAnalysis.h>
#include "DislocationNetwork.h"
#include "DislocationVis.h"

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(DislocationNetwork);
OVITO_CLASSINFO(DislocationNetwork, "ClassNameAlias", "DislocationNetworkObject");  // For backward compatibility with OVITO 3.10
OVITO_CLASSINFO(DislocationNetwork, "DisplayName", "Dislocations");
DEFINE_VECTOR_REFERENCE_FIELD(DislocationNetwork, crystalStructures);
DEFINE_REFERENCE_FIELD(DislocationNetwork, clusterGraph);
SET_PROPERTY_FIELD_LABEL(DislocationNetwork, crystalStructures, "Crystal structures");
SET_PROPERTY_FIELD_LABEL(DislocationNetwork, clusterGraph, "Cluster graph");

/******************************************************************************
* Constructor.
******************************************************************************/
void DislocationNetwork::initializeObject(ObjectInitializationFlags flags)
{
    PeriodicDomainObject::initializeObject(flags);

    if(!flags.testFlag(ObjectInitializationFlag::DontInitializeObject)) {

        if(!flags.testFlag(ObjectInitializationFlag::DontCreateVisElement)) {
            // Attach a visualization element for rendering the dislocation lines.
            setVisElement(OORef<DislocationVis>::create(flags));
        }

        // Create the "unidentified" structure.
        if(crystalStructures().empty()) {
            DataOORef<MicrostructurePhase> defaultStructure = DataOORef<MicrostructurePhase>::create(flags);
            defaultStructure->setName(tr("Unidentified structure"));
            defaultStructure->setColor(Color(1,1,1));
            defaultStructure->createBurgersVectorFamily();
            addCrystalStructure(std::move(defaultStructure));
        }

        // Create an empty cluster graph.
        setClusterGraph(DataOORef<ClusterGraph>::create(flags));
    }
}

/******************************************************************************
* Creates a copy of the object.
******************************************************************************/
OORef<RefTarget> DislocationNetwork::clone(bool deepCopy, CloneHelper& cloneHelper) const
{
    // Let the base class create an instance of this class.
    OORef<DislocationNetwork> clone = static_object_cast<DislocationNetwork>(PeriodicDomainObject::clone(deepCopy, cloneHelper));

    for(int segmentIndex = 0; segmentIndex < segments().size(); segmentIndex++) {
        DislocationSegment* oldSegment = segments()[segmentIndex];
        OVITO_ASSERT(oldSegment->replacedWith == nullptr);
        OVITO_ASSERT(oldSegment->id == segmentIndex);
        DislocationSegment* newSegment = clone->createSegment(oldSegment->burgersVector);
        newSegment->line = oldSegment->line;
        newSegment->coreSize = oldSegment->coreSize;
        newSegment->customColor = oldSegment->customColor;
        newSegment->trackId = oldSegment->trackId;
        OVITO_ASSERT(newSegment->id == oldSegment->id);
    }

    for(int segmentIndex = 0; segmentIndex < segments().size(); segmentIndex++) {
        DislocationSegment* oldSegment = segments()[segmentIndex];
        DislocationSegment* newSegment = clone->segments()[segmentIndex];
        for(int nodeIndex = 0; nodeIndex < 2; nodeIndex++) {
            DislocationNode* oldNode = oldSegment->nodes[nodeIndex];
            if(oldNode->isDangling())
                continue;
            DislocationNode* oldSecondNode = oldNode->junctionRing;
            DislocationNode* newNode = newSegment->nodes[nodeIndex];
            newNode->junctionRing = clone->segments()[oldNode->junctionRing->segment->id]->nodes[oldSecondNode->isForwardNode() ? 0 : 1];
        }
    }

#ifdef OVITO_DEBUG
    for(int segmentIndex = 0; segmentIndex < segments().size(); segmentIndex++) {
        DislocationSegment* oldSegment = segments()[segmentIndex];
        DislocationSegment* newSegment = clone->segments()[segmentIndex];
        for(int nodeIndex = 0; nodeIndex < 2; nodeIndex++) {
            OVITO_ASSERT(oldSegment->nodes[nodeIndex]->countJunctionArms() == newSegment->nodes[nodeIndex]->countJunctionArms());
        }
    }
#endif

    return clone;
}

/******************************************************************************
* Is called when the value of a reference field of this RefMaker changes.
******************************************************************************/
void DislocationNetwork::referenceReplaced(const PropertyFieldDescriptor* field, RefTarget* oldTarget, RefTarget* newTarget, int listIndex)
{
    if(field == PROPERTY_FIELD(clusterGraph)) {
        // If the cluster graph gets replaced with a new copy, update our cluster references
        // by remapping them to the new graph.
        if(oldTarget && newTarget) {
            ClusterGraph* newGraph = static_object_cast<ClusterGraph>(newTarget);
            for(DislocationSegment* segment : segments()) {
                if(segment->burgersVector.cluster()) {
                    OVITO_ASSERT(static_object_cast<ClusterGraph>(oldTarget)->findCluster(segment->burgersVector.cluster()->id));
                    segment->burgersVector = ClusterVector(segment->burgersVector.localVec(), newGraph->findCluster(segment->burgersVector.cluster()->id));
                }
            }
        }
    }

    PeriodicDomainObject::referenceReplaced(field, oldTarget, newTarget, listIndex);
}

/******************************************************************************
* Creates an editable proxy object for this DataObject and synchronizes its parameters.
******************************************************************************/
void DislocationNetwork::updateEditableProxies(PipelineFlowState& state, ConstDataObjectPath& dataPath, bool forceProxyReplacement) const
{
    PeriodicDomainObject::updateEditableProxies(state, dataPath, forceProxyReplacement);

    // Note: 'this' may no longer exist at this point, because the base method implementation may
    // have already replaced it with a mutable copy.
    const DislocationNetwork* self = static_object_cast<DislocationNetwork>(dataPath.back());

    if(self->editableProxy() && !forceProxyReplacement) {
        DislocationNetwork* proxy = static_object_cast<DislocationNetwork>(self->editableProxy());
        // Synchronize the actual data object with the editable proxy object.

        // Add the proxies of newly created microstructure phases to the proxy object.
        for(const MicrostructurePhase* phase : self->crystalStructures()) {
            MicrostructurePhase* proxyPhase = static_object_cast<MicrostructurePhase>(phase->editableProxy());
            OVITO_ASSERT(proxyPhase != nullptr);
            if(!proxy->crystalStructures().contains(proxyPhase))
                proxy->addCrystalStructure(proxyPhase);
        }

        // Add microstructure phases that are non-existing in the actual data object.
        // Note: Currently this should never happen, because file parser never
        // remove element types.
#ifdef OVITO_DEBUG
        for(const MicrostructurePhase* proxyPhase : proxy->crystalStructures()) {
            OVITO_ASSERT(std::any_of(self->crystalStructures().begin(), self->crystalStructures().end(), [proxyPhase](const MicrostructurePhase* phase) { return phase->editableProxy() == proxyPhase; }));
        }
#endif
    }
    else {
        // Create and initialize a new proxy object.
        // Note: We avoid copying the actual dislocation data here by constructing the proxy DislocationNetwork from scratch instead of cloning the original data object.
        OORef<DislocationNetwork> newProxy = OORef<DislocationNetwork>::create(ObjectInitializationFlag::DontCreateVisElement);
        newProxy->setTitle(self->title());
        while(!newProxy->crystalStructures().empty())
            newProxy->removeCrystalStructure(0);

        // Adopt the proxy objects for the microstructure phase types, which have already been created by
        // the recursive method.
        for(const MicrostructurePhase* phase : self->crystalStructures()) {
            OVITO_ASSERT(phase->editableProxy() != nullptr);
            newProxy->addCrystalStructure(static_object_cast<MicrostructurePhase>(phase->editableProxy()));
        }

        // Make this data object mutable and attach the proxy object to it.
        state.makeMutableInplace(dataPath)->setEditableProxy(std::move(newProxy));
    }
}

/******************************************************************************
* Allocates a new dislocation segment terminated by two nodes.
******************************************************************************/
DislocationSegment* DislocationNetwork::createSegment(const ClusterVector& burgersVector)
{
    DislocationNode* forwardNode  = _nodePool.construct();
    DislocationNode* backwardNode = _nodePool.construct();

    DislocationSegment* segment = _segmentPool.construct(burgersVector, forwardNode, backwardNode);
    segment->id = _segments.size();
    _segments.push_back(segment);

    return segment;
}

/******************************************************************************
* Removes a segment from the list of segments.
******************************************************************************/
void DislocationNetwork::discardSegment(DislocationSegment* segment)
{
    OVITO_ASSERT(segment != nullptr);
    auto i = std::find(_segments.begin(), _segments.end(), segment);
    OVITO_ASSERT(i != _segments.end());
    _segments.erase(i);
}

/******************************************************************************
* Smoothens and coarsens the dislocation lines.
******************************************************************************/
void DislocationNetwork::smoothDislocationLines(int lineSmoothingLevel, FloatType linePointInterval, TaskProgress& progress)
{
    progress.setMaximum(segments().size());

    for(DislocationSegment* segment : segments()) {
        progress.incrementValue();
        if(segment->coreSize.empty())
            continue;
        std::deque<Point3> line;
        std::deque<int> coreSize;
        coarsenDislocationLine(linePointInterval, segment->line, segment->coreSize, line, coreSize, segment->isClosedLoop(), segment->isInfiniteLine());
        smoothDislocationLine(lineSmoothingLevel, line, segment->isClosedLoop());
        segment->line = std::move(line);
        segment->coreSize.clear();
    }
}

/******************************************************************************
* Removes some of the sampling points from a dislocation line.
******************************************************************************/
void DislocationNetwork::coarsenDislocationLine(FloatType linePointInterval, const std::deque<Point3>& input, const std::deque<int>& coreSize, std::deque<Point3>& output, std::deque<int>& outputCoreSize, bool isClosedLoop, bool isInfiniteLine)
{
    OVITO_ASSERT(input.size() >= 2);
    OVITO_ASSERT(input.size() == coreSize.size());

    if(linePointInterval <= 0) {
        output = input;
        outputCoreSize = coreSize;
        return;
    }

    // Special handling for infinite lines.
    if(isInfiniteLine && input.size() >= 3) {
        int coreSizeSum = std::accumulate(coreSize.cbegin(), coreSize.cend() - 1, 0);
        int count = input.size() - 1;
        if(coreSizeSum * linePointInterval > count * count) {
            // Make it a straight line.
            Vector3 com = Vector3::Zero();
            for(auto p = input.cbegin(); p != input.cend() - 1; ++p)
                com += *p - input.front();
            output.push_back(input.front() + com / count);
            outputCoreSize.push_back(coreSizeSum / count);
            output.push_back(input.back() + com / count);
            outputCoreSize.push_back(coreSizeSum / count);
            return;
        }
    }

    // Special handling for very short segments.
    if(input.size() < 4) {
        output = input;
        outputCoreSize = coreSize;
        return;
    }

    // Always keep the end points of linear segments fixed to not break junctions.
    if(!isClosedLoop) {
        output.push_back(input.front());
        outputCoreSize.push_back(coreSize.front());
    }

    // Resulting line must contain at least two points (the end points).
    int minNumPoints = 2;

    // If the dislocation forms a loop, keep at least four points, because two points do not make a proper loop.
    if(input.front().equals(input.back(), CA_ATOM_VECTOR_EPSILON))
        minNumPoints = 4;

    auto inputPtr = input.cbegin();
    auto inputCoreSizePtr = coreSize.cbegin();

    int sum = 0;
    int count = 0;

    // Average over a half interval, starting from the beginning of the segment.
    Vector3 com = Vector3::Zero();
    do {
        sum += *inputCoreSizePtr;
        com += *inputPtr - input.front();
        count++;
        ++inputPtr;
        ++inputCoreSizePtr;
    }
    while(2*count*count < (int)(linePointInterval * sum) && count+1 < input.size()/minNumPoints/2);

    // Average over a half interval, starting from the end of the segment.
    auto inputPtrEnd = input.cend() - 1;
    auto inputCoreSizePtrEnd = coreSize.cend() - 1;
    OVITO_ASSERT(inputPtr < inputPtrEnd);
    while(count*count < (int)(linePointInterval * sum) && count < input.size()/minNumPoints) {
        sum += *inputCoreSizePtrEnd;
        com += *inputPtrEnd - input.back();
        count++;
        --inputPtrEnd;
        --inputCoreSizePtrEnd;
    }
    OVITO_ASSERT(inputPtr < inputPtrEnd);

    if(isClosedLoop) {
        output.push_back(input.front() + com / count);
        outputCoreSize.push_back(sum / count);
    }

    while(inputPtr < inputPtrEnd)
    {
        int sum = 0;
        int count = 0;
        Vector3 com = Vector3::Zero();
        do {
            sum += *inputCoreSizePtr++;
            com.x() += inputPtr->x();
            com.y() += inputPtr->y();
            com.z() += inputPtr->z();
            count++;
            ++inputPtr;
        }
        while(count*count < (int)(linePointInterval * sum) && count+1 < input.size()/minNumPoints && inputPtr != inputPtrEnd);
        output.push_back(Point3::Origin() + com / count);
        outputCoreSize.push_back(sum / count);
    }

    if(!isClosedLoop) {
        // Always keep the end points of linear segments to not break junctions.
        output.push_back(input.back());
        outputCoreSize.push_back(coreSize.back());
    }
    else {
        output.push_back(input.back() + com / count);
        outputCoreSize.push_back(sum / count);
    }

    OVITO_ASSERT(output.size() >= minNumPoints);
    OVITO_ASSERT(!isClosedLoop || isInfiniteLine || output.size() >= 3);
}

/******************************************************************************
* Smoothes the sampling points of a dislocation line.
******************************************************************************/
void DislocationNetwork::smoothDislocationLine(int smoothingLevel, std::deque<Point3>& line, bool isLoop)
{
    if(smoothingLevel <= 0)
        return; // Nothing to do.

    if(line.size() <= 2)
        return; // Nothing to do.

    if(line.size() <= 4 && line.front().equals(line.back(), CA_ATOM_VECTOR_EPSILON))
        return; // Do not smooth loops consisting of very few segments.

    // This is the 2d implementation of the mesh smoothing algorithm:
    //
    // Gabriel Taubin
    // A Signal Processing Approach To Fair Surface Design
    // In SIGGRAPH 95 Conference Proceedings, pages 351-358 (1995)

    FloatType k_PB = 0.1f;
    FloatType lambda = 0.5f;
    FloatType mu = 1.0f / (k_PB - 1.0f/lambda);
    const FloatType prefactors[2] = { lambda, mu };

    std::vector<Vector3> laplacians(line.size());
    for(int iteration = 0; iteration < smoothingLevel; iteration++) {

        for(int pass = 0; pass <= 1; pass++) {
            // Compute discrete Laplacian for each point.
            auto l = laplacians.begin();
            if(isLoop == false)
                (*l++).setZero();
            else
                (*l++) = ((*(line.end()-2) - *(line.end()-3)) + (*(line.begin()+1) - line.front())) * FloatType(0.5);

            auto p1 = line.cbegin();
            auto p2 = line.cbegin() + 1;
            for(;;) {
                auto p0 = p1;
                ++p1;
                ++p2;
                if(p2 == line.cend())
                    break;
                *l++ = ((*p0 - *p1) + (*p2 - *p1)) * FloatType(0.5);
            }

            *l++ = laplacians.front();
            OVITO_ASSERT(l == laplacians.end());

            auto lc = laplacians.cbegin();
            for(Point3& p : line) {
                p += prefactors[pass] * (*lc++);
            }
        }
    }
}

}   // End of namespace
