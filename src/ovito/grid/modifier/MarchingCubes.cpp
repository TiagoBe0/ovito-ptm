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

#include <ovito/grid/Grid.h>
#include <ovito/core/utilities/DisjointSet.h>
#include "MarchingCubes.h"
#include "MarchingCubesLookupTable.h"
#include "MarchingCubesRegionsLookupTable.h"
#include "MarchingCubesVolumeLookupTable.h"

namespace Ovito {

/******************************************************************************
 * Constructor.
 ******************************************************************************/
MarchingCubes::MarchingCubes(SurfaceMeshBuilder& outputMesh, int size_x, int size_y, int size_z, bool lowerIsSolid,
                             std::function<FloatType(int i, int j, int k)> field, bool infiniteDomain, bool outputCellCoordinates)
    : _outputMesh(outputMesh),
      _vertexGrower(outputMesh),
      _faceGrower(outputMesh),
      _pbcFlags(outputMesh.domain().pbcFlags()),
      _infiniteDomain(infiniteDomain),
      _outputCellCoordinates(outputCellCoordinates),
      _size_x(size_x + (_pbcFlags[0] ? 0 : 1)),
      _size_y(size_y + (_pbcFlags[1] ? 0 : 1)),
      _size_z(size_z + (_pbcFlags[2] ? 0 : 1)),
      getFieldValue(std::move(field)),
      _cubeVerts(_size_x * _size_y * _size_z * 3, SurfaceMesh::InvalidIndex),
      _lowerIsSolid(lowerIsSolid)
{
    OVITO_ASSERT(outputMesh.vertexCount() == 0);
    OVITO_ASSERT(outputMesh.faceCount() == 0);
    OVITO_ASSERT(outputMesh.regionCount() == 0);
    OVITO_ASSERT(outputMesh.spaceFillingRegion() == SurfaceMesh::InvalidIndex);
}

/******************************************************************************
 * Main method that constructs the isosurface mesh.
 ******************************************************************************/
void MarchingCubes::generateIsosurface(FloatType isolevel, TaskProgress& progress)
{
    _isolevel = isolevel;
    int size_x = _infiniteDomain ? (_size_x - 1) : _size_x;
    int size_y = _infiniteDomain ? (_size_y - 1) : _size_y;
    int size_z = _infiniteDomain ? (_size_z - 1) : _size_z;

    progress.setMaximum(size_z * 2);
    computeIntersectionPoints(progress);

    this_task::throwIfCanceled();

    if(_outputMesh.spaceFillingRegion() != SurfaceMesh::InvalidIndex) {
        handleSpaceFillingRegion();
        _vertexGrower.reset();
        _faceGrower.reset();
        return;
    }

    // Setup region calculation
    if(identifyRegions()) {
        _vertRegions.assign(_size_x * _size_y * _size_z, -1);
        _maxRegionIndex = 0;
    }

    for(int k = 0; k < size_z; k++, progress.incrementValue()) {
        for(int j = 0; j < size_y; j++) {
            for(int i = 0; i < size_x; i++) {
                _lut_entry = 0;
                for(int p = 0; p < 8; ++p) {
                    _cube[p] = getFieldValue(i + ((p ^ (p >> 1)) & 1), j + ((p >> 1) & 1), k + ((p >> 2) & 1)) - _isolevel;
                    if(std::abs(_cube[p]) < _epsilon)
                        _cube[p] = _epsilon;
                    if(_cube[p] > 0)
                        _lut_entry |= (1 << p);
                }
                processCube(i, j, k);
            }
        }
    }

    if(identifyRegions()) {
        if(_outputMesh.faceCount() != 0) {
            bool isClosed = _outputMesh.connectOppositeHalfedges();
            OVITO_ASSERT(isClosed);
            mergeIdentifiedRegions();
        }
        else {
            // Case: No surface, because box is completely empty -> output one empty mesh region.
            handleSpaceFillingRegion();
        }
    }

    _vertexGrower.reset();
    _faceGrower.reset();

    this_task::throwIfCanceled();
}

/******************************************************************************
 * Compute the intersection points with the isosurface along the cube edges.
 ******************************************************************************/
void MarchingCubes::computeIntersectionPoints(TaskProgress& progress)
{
    if(_pbcFlags[0] && _pbcFlags[1] && _pbcFlags[2])
        _outputMesh.setSpaceFillingRegion(0);

    for(int k = 0; k < _size_z; k++, progress.incrementValue()) {
        for(int j = 0; j < _size_y; j++) {
            for(int i = 0; i < _size_x; i++) {
                FloatType cube[8];
                cube[0] = getFieldValue(i, j, k) - _isolevel;
                cube[1] = getFieldValue(i + 1, j, k) - _isolevel;
                cube[3] = getFieldValue(i, j + 1, k) - _isolevel;
                cube[4] = getFieldValue(i, j, k + 1) - _isolevel;

                if(std::abs(cube[0]) < _epsilon) cube[0] = _epsilon;
                if(std::abs(cube[1]) < _epsilon) cube[1] = _epsilon;
                if(std::abs(cube[3]) < _epsilon) cube[3] = _epsilon;
                if(std::abs(cube[4]) < _epsilon) cube[4] = _epsilon;

                if(_lowerIsSolid) {
                    if(cube[0] > 0) _outputMesh.setSpaceFillingRegion(SurfaceMesh::InvalidIndex);
                }
                else {
                    if(cube[0] < 0) _outputMesh.setSpaceFillingRegion(SurfaceMesh::InvalidIndex);
                }
                if(cube[1] * cube[0] < 0) createEdgeVertexX(i, j, k, cube[0] / (cube[0] - cube[1]));
                if(cube[3] * cube[0] < 0) createEdgeVertexY(i, j, k, cube[0] / (cube[0] - cube[3]));
                if(cube[4] * cube[0] < 0) createEdgeVertexZ(i, j, k, cube[0] / (cube[0] - cube[4]));
            }
        }
    }
}

/******************************************************************************
 * Test a face.
 * if face>0 return true if the face contains a part of the surface
 ******************************************************************************/
bool MarchingCubes::testFace(signed char face)
{
    FloatType A, B, C, D;

    switch(face) {
    case -1:
    case 1:
        A = _cube[0];
        B = _cube[4];
        C = _cube[5];
        D = _cube[1];
        break;
    case -2:
    case 2:
        A = _cube[1];
        B = _cube[5];
        C = _cube[6];
        D = _cube[2];
        break;
    case -3:
    case 3:
        A = _cube[2];
        B = _cube[6];
        C = _cube[7];
        D = _cube[3];
        break;
    case -4:
    case 4:
        A = _cube[3];
        B = _cube[7];
        C = _cube[4];
        D = _cube[0];
        break;
    case -5:
    case 5:
        A = _cube[0];
        B = _cube[3];
        C = _cube[2];
        D = _cube[1];
        break;
    case -6:
    case 6:
        A = _cube[4];
        B = _cube[7];
        C = _cube[6];
        D = _cube[5];
        break;
    default:
        OVITO_ASSERT_MSG(false, "Marching cubes", "Invalid face code");
        return false;
    }

    if(std::abs(A * C - B * D) < _epsilon) return face >= 0;
    return face * A * (A * C - B * D) >= 0;  // face and A invert signs
}

/******************************************************************************
 * Test the interior of a cube.
 * if s == 7, return true  if the interior is empty
 * if s ==-7, return false if the interior is empty
 ******************************************************************************/
bool MarchingCubes::testInterior(signed char s)
{
    FloatType t, At = 0, Bt = 0, Ct = 0, Dt = 0, a, b;
    signed char test = 0;
    signed char edge = -1;  // reference edge of the triangulation

    switch(_case) {
        case 4:
        case 10:
            a = (_cube[4] - _cube[0]) * (_cube[6] - _cube[2]) - (_cube[7] - _cube[3]) * (_cube[5] - _cube[1]);
            b = _cube[2] * (_cube[4] - _cube[0]) + _cube[0] * (_cube[6] - _cube[2]) - _cube[1] * (_cube[7] - _cube[3]) -
                _cube[3] * (_cube[5] - _cube[1]);
            t = -b / (2 * a);
            if(t < 0 || t > 1) return s > 0;

            At = _cube[0] + (_cube[4] - _cube[0]) * t;
            Bt = _cube[3] + (_cube[7] - _cube[3]) * t;
            Ct = _cube[2] + (_cube[6] - _cube[2]) * t;
            Dt = _cube[1] + (_cube[5] - _cube[1]) * t;
            break;

        case 6:
        case 7:
        case 12:
        case 13:
            switch(_case) {
                case 6: edge = test6[_config][2]; break;
                case 7: edge = test7[_config][4]; break;
                case 12: edge = test12[_config][3]; break;
                case 13: edge = tiling13_5_1[_config][_subconfig][0]; break;
            }
            switch(edge) {
                case 0:
                    t = _cube[0] / (_cube[0] - _cube[1]);
                    At = 0;
                    Bt = _cube[3] + (_cube[2] - _cube[3]) * t;
                    Ct = _cube[7] + (_cube[6] - _cube[7]) * t;
                    Dt = _cube[4] + (_cube[5] - _cube[4]) * t;
                    break;
                case 1:
                    t = _cube[1] / (_cube[1] - _cube[2]);
                    At = 0;
                    Bt = _cube[0] + (_cube[3] - _cube[0]) * t;
                    Ct = _cube[4] + (_cube[7] - _cube[4]) * t;
                    Dt = _cube[5] + (_cube[6] - _cube[5]) * t;
                    break;
                case 2:
                    t = _cube[2] / (_cube[2] - _cube[3]);
                    At = 0;
                    Bt = _cube[1] + (_cube[0] - _cube[1]) * t;
                    Ct = _cube[5] + (_cube[4] - _cube[5]) * t;
                    Dt = _cube[6] + (_cube[7] - _cube[6]) * t;
                    break;
                case 3:
                    t = _cube[3] / (_cube[3] - _cube[0]);
                    At = 0;
                    Bt = _cube[2] + (_cube[1] - _cube[2]) * t;
                    Ct = _cube[6] + (_cube[5] - _cube[6]) * t;
                    Dt = _cube[7] + (_cube[4] - _cube[7]) * t;
                    break;
                case 4:
                    t = _cube[4] / (_cube[4] - _cube[5]);
                    At = 0;
                    Bt = _cube[7] + (_cube[6] - _cube[7]) * t;
                    Ct = _cube[3] + (_cube[2] - _cube[3]) * t;
                    Dt = _cube[0] + (_cube[1] - _cube[0]) * t;
                    break;
                case 5:
                    t = _cube[5] / (_cube[5] - _cube[6]);
                    At = 0;
                    Bt = _cube[4] + (_cube[7] - _cube[4]) * t;
                    Ct = _cube[0] + (_cube[3] - _cube[0]) * t;
                    Dt = _cube[1] + (_cube[2] - _cube[1]) * t;
                    break;
                case 6:
                    t = _cube[6] / (_cube[6] - _cube[7]);
                    At = 0;
                    Bt = _cube[5] + (_cube[4] - _cube[5]) * t;
                    Ct = _cube[1] + (_cube[0] - _cube[1]) * t;
                    Dt = _cube[2] + (_cube[3] - _cube[2]) * t;
                    break;
                case 7:
                    t = _cube[7] / (_cube[7] - _cube[4]);
                    At = 0;
                    Bt = _cube[6] + (_cube[5] - _cube[6]) * t;
                    Ct = _cube[2] + (_cube[1] - _cube[2]) * t;
                    Dt = _cube[3] + (_cube[0] - _cube[3]) * t;
                    break;
                case 8:
                    t = _cube[0] / (_cube[0] - _cube[4]);
                    At = 0;
                    Bt = _cube[3] + (_cube[7] - _cube[3]) * t;
                    Ct = _cube[2] + (_cube[6] - _cube[2]) * t;
                    Dt = _cube[1] + (_cube[5] - _cube[1]) * t;
                    break;
                case 9:
                    t = _cube[1] / (_cube[1] - _cube[5]);
                    At = 0;
                    Bt = _cube[0] + (_cube[4] - _cube[0]) * t;
                    Ct = _cube[3] + (_cube[7] - _cube[3]) * t;
                    Dt = _cube[2] + (_cube[6] - _cube[2]) * t;
                    break;
                case 10:
                    t = _cube[2] / (_cube[2] - _cube[6]);
                    At = 0;
                    Bt = _cube[1] + (_cube[5] - _cube[1]) * t;
                    Ct = _cube[0] + (_cube[4] - _cube[0]) * t;
                    Dt = _cube[3] + (_cube[7] - _cube[3]) * t;
                    break;
                case 11:
                    t = _cube[3] / (_cube[3] - _cube[7]);
                    At = 0;
                    Bt = _cube[2] + (_cube[6] - _cube[2]) * t;
                    Ct = _cube[1] + (_cube[5] - _cube[1]) * t;
                    Dt = _cube[0] + (_cube[4] - _cube[0]) * t;
                    break;
                default: OVITO_ASSERT_MSG(false, "Marching cubes", "Invalid edge"); break;
            }
            break;

        default: OVITO_ASSERT_MSG(false, "Marching cubes", "Invalid ambiguous case"); break;
    }

    if(At >= 0) test++;
    if(Bt >= 0) test += 2;
    if(Ct >= 0) test += 4;
    if(Dt >= 0) test += 8;
    switch(test) {
        case 0: return s > 0;
        case 1: return s > 0;
        case 2: return s > 0;
        case 3: return s > 0;
        case 4: return s > 0;
        case 5:
            if(At * Ct - Bt * Dt < _epsilon) return s > 0;
            break;
        case 6: return s > 0;
        case 7: return s < 0;
        case 8: return s > 0;
        case 9: return s > 0;
        case 10:
            if(At * Ct - Bt * Dt >= _epsilon) return s > 0;
            break;
        case 11: return s < 0;
        case 12: return s > 0;
        case 13: return s < 0;
        case 14: return s < 0;
        case 15: return s < 0;
    }

    return s < 0;
}

void MarchingCubes::handleSpaceFillingRegion()
{
    OVITO_ASSERT(_outputMesh.regionCount() == 0);
    _outputMesh.mutableRegions()->setElementCount(1);

    BufferWriteAccess<FloatType, access_mode::discard_write> volumeProperty =
        _outputMesh.createRegionProperty(DataBuffer::Uninitialized, SurfaceMeshRegions::VolumeProperty);
    BufferWriteAccess<SelectionIntType, access_mode::discard_write> isExteriorProperty =
        _outputMesh.createRegionProperty(DataBuffer::Uninitialized, SurfaceMeshRegions::IsExteriorProperty);
    BufferWriteAccess<SelectionIntType, access_mode::discard_write> isFilledProperty =
        _outputMesh.createRegionProperty(DataBuffer::Uninitialized, SurfaceMeshRegions::IsFilledProperty);

    volumeProperty[0] = _size_x * _size_y * _size_z;
    if(_outputMesh.spaceFillingRegion() != SurfaceMesh::InvalidIndex) {
        isExteriorProperty[0] = 0;
        isFilledProperty[0] = 1;
    }
    else {
        isExteriorProperty[0] = !(_outputMesh.hasPbc(0) && _outputMesh.hasPbc(1) && _outputMesh.hasPbc(2));
        isFilledProperty[0] = 0;
    }
    _outputMesh.setSpaceFillingRegion(0);
}

/******************************************************************************
 * Merge identified subregions of the iso surfaces using a disjoint set data structure
 * Sums the volume for each merged region
 * Sums the surface area for all triangle faces belonging to a each region
 ******************************************************************************/
void MarchingCubes::mergeIdentifiedRegions()
{
    // merge regions using disjoint set
    DisjointSet uf{static_cast<size_t>(_maxRegionIndex)};
    for(auto [r1, r2] : _regionsToMerge) {
        uf.merge(r1, r2);
    }

    // map newly defined regions from discontinous range(0,_regionVolumes.size()) to range(0,regionCount)
    std::map<SurfaceMesh::region_index, SurfaceMesh::region_index> regionMap;

    // Assign newly merged regions to each facet.
    for(SurfaceMesh::region_index& ridx : _faceGrower.faceRegions()) {
        SurfaceMesh::region_index newIndex = static_cast<SurfaceMesh::region_index>(uf.find(ridx));
        ridx = regionMap.emplace(newIndex, regionMap.size()).first->second;
    }
    _maxRegionIndex = regionMap.size();
    _outputMesh.mutableRegions()->setElementCount(_maxRegionIndex);

    BufferWriteAccess<FloatType, access_mode::read_write> volumeProperty{
        _outputMesh.createRegionProperty(DataBuffer::Initialized, SurfaceMeshRegions::VolumeProperty)};
    BufferWriteAccess<SelectionIntType, access_mode::read_write> isExteriorProperty{
        _outputMesh.createRegionProperty(DataBuffer::Initialized, SurfaceMeshRegions::IsExteriorProperty)};
    BufferWriteAccess<SelectionIntType, access_mode::read_write> isFilledProperty{
        _outputMesh.createRegionProperty(DataBuffer::Initialized, SurfaceMeshRegions::IsFilledProperty)};

    for(int i = 0; i < _regionVolumes.size(); i++) {
        int newIndex = static_cast<int>(uf.find(i));
        OVITO_ASSERT(regionMap.find(newIndex) != regionMap.end());
        auto ridx = regionMap[newIndex];
        volumeProperty[ridx] += _regionVolumes[i];
        isFilledProperty[ridx] = static_cast<SelectionIntType>(_regionFilled[i]);
        isExteriorProperty[ridx] |= static_cast<SelectionIntType>(_regionExterior[i]);
    }
    OVITO_ASSERT(std::abs(std::accumulate(volumeProperty.begin(), volumeProperty.end(), 0.0) - (_size_x * _size_y * _size_z)) < 1e-6);
}

/******************************************************************************
 * Processes a single cube and labels it regions.
 ******************************************************************************/
void MarchingCubes::processCube(int i, int j, int k)
{
    SurfaceMesh::vertex_index v12 = SurfaceMesh::InvalidIndex;
    _case = cases[_lut_entry][0];
    _config = cases[_lut_entry][1];
    _subconfig = 0;

    switch(_case) {
        case 0: processCase(i, j, k, nullptr, nullptr, vertexRegion0, nullptr, -1, -1); break;
        case 1:
            processCase(i, j, k, tiling1[_config], triangleRegion1[_config], vertexRegion1[_config], volumeRegion1[_config], 1,
                        2);
            break;

        case 2:
            processCase(i, j, k, tiling2[_config], triangleRegion2[_config], vertexRegion2[_config], volumeRegion2[_config], 2,
                        2);
            break;

        case 3:
            if(testFace(test3[_config]))
                processCase(i, j, k, tiling3_2[_config], triangleRegion3_2[_config], vertexRegion3_2[_config],
                            volumeRegion3_2[_config], 4, 2);
            else
                processCase(i, j, k, tiling3_1[_config], triangleRegion3_1[_config], vertexRegion3_1[_config],
                            volumeRegion3_1[_config], 2, 3);
            break;

        case 4:
            if(testInterior(test4[_config]))
                processCase(i, j, k, tiling4_1[_config], triangleRegion4_1[_config], vertexRegion4_1[_config],
                            volumeRegion4_1[_config], 2, 3);
            else
                processCase(i, j, k, tiling4_2[_config], triangleRegion4_2[_config], vertexRegion4_2[_config],
                            volumeRegion4_2[_config], 6, 2);
            break;

        case 5:
            processCase(i, j, k, tiling5[_config], triangleRegion5[_config], vertexRegion5[_config], volumeRegion5[_config], 3,
                        2);
            break;

        case 6:
            if(testFace(test6[_config][0]))
                processCase(i, j, k, tiling6_2[_config], triangleRegion6_2[_config], vertexRegion6_2[_config],
                            volumeRegion6_2[_config], 5, 2);
            else {
                if(testInterior(test6[_config][1]))
                    processCase(i, j, k, tiling6_1_1[_config], triangleRegion6_1_1[_config], vertexRegion6_1_1[_config],
                                volumeRegion6_1_1[_config], 3, 3);
                else {
                    v12 = createCenterVertex(i, j, k);
                    processCase(i, j, k, tiling6_1_2[_config], triangleRegion6_1_2[_config], vertexRegion6_1_2[_config],
                                volumeRegion6_1_2[_config], 9, 2, v12);
                }
            }
            break;

        case 7:
            if(testFace(test7[_config][0])) _subconfig += 1;
            if(testFace(test7[_config][1])) _subconfig += 2;
            if(testFace(test7[_config][2])) _subconfig += 4;
            switch(_subconfig) {
                case 0:
                    processCase(i, j, k, tiling7_1[_config], triangleRegion7_1[_config], vertexRegion7_1[_config],
                                volumeRegion7_1[_config], 3, 4);
                    break;
                case 1:
                    processCase(i, j, k, tiling7_2[_config][0], triangleRegion7_2[_config][0], vertexRegion7_2[_config][0],
                                volumeRegion7_2[_config][0], 5, 3);
                    break;
                case 2:
                    processCase(i, j, k, tiling7_2[_config][1], triangleRegion7_2[_config][1], vertexRegion7_2[_config][1],
                                volumeRegion7_2[_config][1], 5, 3);
                    break;
                case 3:
                    v12 = createCenterVertex(i, j, k);
                    processCase(i, j, k, tiling7_3[_config][0], triangleRegion7_3[_config][0], vertexRegion7_3[_config][0],
                                volumeRegion7_3[_config][0], 9, 2, v12);
                    break;
                case 4:
                    processCase(i, j, k, tiling7_2[_config][2], triangleRegion7_2[_config][2], vertexRegion7_2[_config][2],
                                volumeRegion7_2[_config][2], 5, 3);
                    break;
                case 5:
                    v12 = createCenterVertex(i, j, k);
                    processCase(i, j, k, tiling7_3[_config][1], triangleRegion7_3[_config][1], vertexRegion7_3[_config][1],
                                volumeRegion7_3[_config][1], 9, 2, v12);
                    break;
                case 6:
                    v12 = createCenterVertex(i, j, k);
                    processCase(i, j, k, tiling7_3[_config][2], triangleRegion7_3[_config][2], vertexRegion7_3[_config][2],
                                volumeRegion7_3[_config][2], 9, 2, v12);
                    break;
                case 7:
                    if(testInterior(test7[_config][3]))
                        processCase(i, j, k, tiling7_4_2[_config], triangleRegion7_4_2[_config], vertexRegion7_4_2[_config],
                                    volumeRegion7_4_2[_config], 9, 4);
                    else
                        processCase(i, j, k, tiling7_4_1[_config], triangleRegion7_4_1[_config], vertexRegion7_4_1[_config],
                                    volumeRegion7_4_1[_config], 5, 3);
                    break;
            };
            break;

        case 8:
            processCase(i, j, k, tiling8[_config], triangleRegion8[_config], vertexRegion8[_config], volumeRegion8[_config], 2,
                        2);
            break;

        case 9:
            processCase(i, j, k, tiling9[_config], triangleRegion9[_config], vertexRegion9[_config], volumeRegion9[_config], 4,
                        2);
            break;

        case 10:
            if(testFace(test10[_config][0])) {
                if(testFace(test10[_config][1])) {
                    processCase(i, j, k, tiling10_1_1_[_config], triangleRegion10_1_1_[_config], vertexRegion10_1_1_[_config],
                                volumeRegion10_1_1_[_config], 4, 3);
                }
                else {
                    v12 = createCenterVertex(i, j, k);
                    processCase(i, j, k, tiling10_2[_config], triangleRegion10_2[_config], vertexRegion10_2[_config],
                                volumeRegion10_2[_config], 8, 2, v12);
                }
            }
            else {
                if(testFace(test10[_config][1])) {
                    v12 = createCenterVertex(i, j, k);
                    processCase(i, j, k, tiling10_2_[_config], triangleRegion10_2_[_config], vertexRegion10_2_[_config],
                                volumeRegion10_2_[_config], 8, 2, v12);
                }
                else {
                    if(testInterior(test10[_config][2]))
                        processCase(i, j, k, tiling10_1_1[_config], triangleRegion10_1_1[_config], vertexRegion10_1_1[_config],
                                    volumeRegion10_1_1[_config], 4, 3);
                    else
                        processCase(i, j, k, tiling10_1_2[_config], triangleRegion10_1_2[_config], vertexRegion10_1_2[_config],
                                    volumeRegion10_1_2[_config], 8, 3);
                }
            }
            break;

        case 11:
            processCase(i, j, k, tiling11[_config], triangleRegion11[_config], vertexRegion11[_config], volumeRegion11[_config],
                        4, 2);
            break;

        case 12:
            if(testFace(test12[_config][0])) {
                if(testFace(test12[_config][1])) {
                    processCase(i, j, k, tiling12_1_1_[_config], triangleRegion12_1_1_[_config], vertexRegion12_1_1_[_config],
                                volumeRegion12_1_1_[_config], 4, 3);
                }
                else {
                    v12 = createCenterVertex(i, j, k);
                    processCase(i, j, k, tiling12_2[_config], triangleRegion12_2[_config], vertexRegion12_2[_config],
                                volumeRegion12_2[_config], 8, 2, v12);
                }
            }
            else {
                if(testFace(test12[_config][1])) {
                    v12 = createCenterVertex(i, j, k);
                    processCase(i, j, k, tiling12_2_[_config], triangleRegion12_2_[_config], vertexRegion12_2_[_config],
                                volumeRegion12_2_[_config], 8, 2, v12);
                }
                else {
                    if(testInterior(test12[_config][2]))
                        processCase(i, j, k, tiling12_1_1[_config], triangleRegion12_1_1[_config], vertexRegion12_1_1[_config],
                                    volumeRegion12_1_1[_config], 4, 3);
                    else
                        processCase(i, j, k, tiling12_1_2[_config], triangleRegion12_1_2[_config], vertexRegion12_1_2[_config],
                                    volumeRegion12_1_2[_config], 8, 3);
                }
            }
            break;

        case 13:
            if(testFace(test13[_config][0])) _subconfig += 1;
            if(testFace(test13[_config][1])) _subconfig += 2;
            if(testFace(test13[_config][2])) _subconfig += 4;
            if(testFace(test13[_config][3])) _subconfig += 8;
            if(testFace(test13[_config][4])) _subconfig += 16;
            if(testFace(test13[_config][5])) _subconfig += 32;
            switch(subconfig13[_subconfig]) {
                case 0:
                    processCase(i, j, k, tiling13_1[_config], triangleRegion13_1[_config], vertexRegion13_1[_config],
                                volumeRegion13_1[_config], 4, 5);
                    break;
                case 1:
                    processCase(i, j, k, tiling13_2[_config][0], triangleRegion13_2[_config][0], vertexRegion13_2[_config][0],
                                volumeRegion13_2[_config][0], 6, 4);
                    break;
                case 2:
                    processCase(i, j, k, tiling13_2[_config][1], triangleRegion13_2[_config][1], vertexRegion13_2[_config][1],
                                volumeRegion13_2[_config][1], 6, 4);
                    break;
                case 3:
                    processCase(i, j, k, tiling13_2[_config][2], triangleRegion13_2[_config][2], vertexRegion13_2[_config][2],
                                volumeRegion13_2[_config][2], 6, 4);
                    break;
                case 4:
                    processCase(i, j, k, tiling13_2[_config][3], triangleRegion13_2[_config][3], vertexRegion13_2[_config][3],
                                volumeRegion13_2[_config][3], 6, 4);
                    break;
                case 5:
                    processCase(i, j, k, tiling13_2[_config][4], triangleRegion13_2[_config][4], vertexRegion13_2[_config][4],
                                volumeRegion13_2[_config][4], 6, 4);
                    break;
                case 6:
                    processCase(i, j, k, tiling13_2[_config][5], triangleRegion13_2[_config][5], vertexRegion13_2[_config][5],
                                volumeRegion13_2[_config][5], 6, 4);
                    break;

                case 7: /* 13.3 */
                    v12 = createCenterVertex(i, j, k);
                    processCase(i, j, k, tiling13_3[_config][0], triangleRegion13_3[_config][0], vertexRegion13_3[_config][0],
                                volumeRegion13_3[_config][0], 10, 3, v12);
                    break;
                case 8: /* 13.3 */
                    v12 = createCenterVertex(i, j, k);
                    processCase(i, j, k, tiling13_3[_config][1], triangleRegion13_3[_config][1], vertexRegion13_3[_config][1],
                                volumeRegion13_3[_config][1], 10, 3, v12);
                    break;
                case 9: /* 13.3 */
                    v12 = createCenterVertex(i, j, k);
                    processCase(i, j, k, tiling13_3[_config][2], triangleRegion13_3[_config][2], vertexRegion13_3[_config][2],
                                volumeRegion13_3[_config][2], 10, 3, v12);
                    break;
                case 10: /* 13.3 */
                    v12 = createCenterVertex(i, j, k);
                    processCase(i, j, k, tiling13_3[_config][3], triangleRegion13_3[_config][3], vertexRegion13_3[_config][3],
                                volumeRegion13_3[_config][3], 10, 3, v12);
                    break;
                case 11: /* 13.3 */
                    v12 = createCenterVertex(i, j, k);
                    processCase(i, j, k, tiling13_3[_config][4], triangleRegion13_3[_config][4], vertexRegion13_3[_config][4],
                                volumeRegion13_3[_config][4], 10, 3, v12);
                    break;
                case 12: /* 13.3 */
                    v12 = createCenterVertex(i, j, k);
                    processCase(i, j, k, tiling13_3[_config][5], triangleRegion13_3[_config][5], vertexRegion13_3[_config][5],
                                volumeRegion13_3[_config][5], 10, 3, v12);
                    break;
                case 13: /* 13.3 */
                    v12 = createCenterVertex(i, j, k);
                    processCase(i, j, k, tiling13_3[_config][6], triangleRegion13_3[_config][6], vertexRegion13_3[_config][6],
                                volumeRegion13_3[_config][6], 10, 3, v12);
                    break;
                case 14: /* 13.3 */
                    v12 = createCenterVertex(i, j, k);
                    processCase(i, j, k, tiling13_3[_config][7], triangleRegion13_3[_config][7], vertexRegion13_3[_config][7],
                                volumeRegion13_3[_config][7], 10, 3, v12);
                    break;
                case 15: /* 13.3 */
                    v12 = createCenterVertex(i, j, k);
                    processCase(i, j, k, tiling13_3[_config][8], triangleRegion13_3[_config][8], vertexRegion13_3[_config][8],
                                volumeRegion13_3[_config][8], 10, 3, v12);
                    break;
                case 16: /* 13.3 */
                    v12 = createCenterVertex(i, j, k);
                    processCase(i, j, k, tiling13_3[_config][9], triangleRegion13_3[_config][9], vertexRegion13_3[_config][9],
                                volumeRegion13_3[_config][9], 10, 3, v12);
                    break;
                case 17: /* 13.3 */
                    v12 = createCenterVertex(i, j, k);
                    processCase(i, j, k, tiling13_3[_config][10], triangleRegion13_3[_config][10], vertexRegion13_3[_config][10],
                                volumeRegion13_3[_config][10], 10, 3, v12);
                    break;
                case 18: /* 13.3 */
                    v12 = createCenterVertex(i, j, k);
                    processCase(i, j, k, tiling13_3[_config][11], triangleRegion13_3[_config][11], vertexRegion13_3[_config][11],
                                volumeRegion13_3[_config][11], 10, 3, v12);
                    break;

                case 19: /* 13.4 */
                    v12 = createCenterVertex(i, j, k);
                    processCase(i, j, k, tiling13_4[_config][0], triangleRegion13_4[_config][0], vertexRegion13_4[_config][0],
                                volumeRegion13_4[_config][0], 12, 2, v12);
                    break;
                case 20: /* 13.4 */
                    v12 = createCenterVertex(i, j, k);
                    processCase(i, j, k, tiling13_4[_config][1], triangleRegion13_4[_config][1], vertexRegion13_4[_config][1],
                                volumeRegion13_4[_config][1], 12, 2, v12);
                    break;
                case 21: /* 13.4 */
                    v12 = createCenterVertex(i, j, k);
                    processCase(i, j, k, tiling13_4[_config][2], triangleRegion13_4[_config][2], vertexRegion13_4[_config][2],
                                volumeRegion13_4[_config][2], 12, 2, v12);
                    break;
                case 22: /* 13.4 */
                    v12 = createCenterVertex(i, j, k);
                    processCase(i, j, k, tiling13_4[_config][3], triangleRegion13_4[_config][3], vertexRegion13_4[_config][3],
                                volumeRegion13_4[_config][3], 12, 2, v12);
                    break;

                case 23: /* 13.5 */
                    _subconfig = 0;
                    if(testInterior(test13[_config][6]))
                        processCase(i, j, k, tiling13_5_1[_config][0], triangleRegion13_5_1[_config][0],
                                    vertexRegion13_5_1[_config][0], volumeRegion13_5_1[_config][0], 6, 4);
                    else
                        processCase(i, j, k, tiling13_5_2[_config][0], triangleRegion13_5_2[_config][0],
                                    vertexRegion13_5_2[_config][0], volumeRegion13_5_2[_config][0], 10, 5);
                    break;
                case 24: /* 13.5 */
                    _subconfig = 1;
                    if(testInterior(test13[_config][6]))
                        processCase(i, j, k, tiling13_5_1[_config][1], triangleRegion13_5_1[_config][1],
                                    vertexRegion13_5_1[_config][1], volumeRegion13_5_1[_config][1], 6, 4);
                    else
                        processCase(i, j, k, tiling13_5_2[_config][1], triangleRegion13_5_2[_config][1],
                                    vertexRegion13_5_2[_config][1], volumeRegion13_5_2[_config][1], 10, 5);
                    break;
                case 25: /* 13.5 */
                    _subconfig = 2;
                    if(testInterior(test13[_config][6]))
                        processCase(i, j, k, tiling13_5_1[_config][2], triangleRegion13_5_1[_config][2],
                                    vertexRegion13_5_1[_config][2], volumeRegion13_5_1[_config][2], 6, 4);
                    else
                        processCase(i, j, k, tiling13_5_2[_config][2], triangleRegion13_5_2[_config][2],
                                    vertexRegion13_5_2[_config][2], volumeRegion13_5_2[_config][2], 10, 5);
                    break;
                case 26: /* 13.5 */
                    _subconfig = 3;
                    if(testInterior(test13[_config][6]))
                        processCase(i, j, k, tiling13_5_1[_config][3], triangleRegion13_5_1[_config][3],
                                    vertexRegion13_5_1[_config][3], volumeRegion13_5_1[_config][3], 6, 4);
                    else
                        processCase(i, j, k, tiling13_5_2[_config][3], triangleRegion13_5_2[_config][3],
                                    vertexRegion13_5_2[_config][3], volumeRegion13_5_2[_config][3], 10, 5);
                    break;

                case 27: /* 13.3 */
                    v12 = createCenterVertex(i, j, k);
                    processCase(i, j, k, tiling13_3_[_config][0], triangleRegion13_3_[_config][0], vertexRegion13_3_[_config][0],
                                volumeRegion13_3_[_config][0], 10, 3, v12);
                    break;
                case 28: /* 13.3 */
                    v12 = createCenterVertex(i, j, k);
                    processCase(i, j, k, tiling13_3_[_config][1], triangleRegion13_3_[_config][1], vertexRegion13_3_[_config][1],
                                volumeRegion13_3_[_config][1], 10, 3, v12);
                    break;
                case 29: /* 13.3 */
                    v12 = createCenterVertex(i, j, k);
                    processCase(i, j, k, tiling13_3_[_config][2], triangleRegion13_3_[_config][2], vertexRegion13_3_[_config][2],
                                volumeRegion13_3_[_config][2], 10, 3, v12);
                    break;
                case 30: /* 13.3 */
                    v12 = createCenterVertex(i, j, k);
                    processCase(i, j, k, tiling13_3_[_config][3], triangleRegion13_3_[_config][3], vertexRegion13_3_[_config][3],
                                volumeRegion13_3_[_config][3], 10, 3, v12);
                    break;
                case 31: /* 13.3 */
                    v12 = createCenterVertex(i, j, k);
                    processCase(i, j, k, tiling13_3_[_config][4], triangleRegion13_3_[_config][4], vertexRegion13_3_[_config][4],
                                volumeRegion13_3_[_config][4], 10, 3, v12);
                    break;
                case 32: /* 13.3 */
                    v12 = createCenterVertex(i, j, k);
                    processCase(i, j, k, tiling13_3_[_config][5], triangleRegion13_3_[_config][5], vertexRegion13_3_[_config][5],
                                volumeRegion13_3_[_config][5], 10, 3, v12);
                    break;
                case 33: /* 13.3 */
                    v12 = createCenterVertex(i, j, k);
                    processCase(i, j, k, tiling13_3_[_config][6], triangleRegion13_3_[_config][6], vertexRegion13_3_[_config][6],
                                volumeRegion13_3_[_config][6], 10, 3, v12);
                    break;
                case 34: /* 13.3 */
                    v12 = createCenterVertex(i, j, k);
                    processCase(i, j, k, tiling13_3_[_config][7], triangleRegion13_3_[_config][7], vertexRegion13_3_[_config][7],
                                volumeRegion13_3_[_config][7], 10, 3, v12);
                    break;
                case 35: /* 13.3 */
                    v12 = createCenterVertex(i, j, k);
                    processCase(i, j, k, tiling13_3_[_config][8], triangleRegion13_3_[_config][8], vertexRegion13_3_[_config][8],
                                volumeRegion13_3_[_config][8], 10, 3, v12);
                    break;
                case 36: /* 13.3 */
                    v12 = createCenterVertex(i, j, k);
                    processCase(i, j, k, tiling13_3_[_config][9], triangleRegion13_3_[_config][9], vertexRegion13_3_[_config][9],
                                volumeRegion13_3_[_config][9], 10, 3, v12);
                    break;
                case 37: /* 13.3 */
                    v12 = createCenterVertex(i, j, k);
                    processCase(i, j, k, tiling13_3_[_config][10], triangleRegion13_3_[_config][10],
                                vertexRegion13_3_[_config][10], volumeRegion13_3_[_config][10], 10, 3, v12);
                    break;
                case 38: /* 13.3 */
                    v12 = createCenterVertex(i, j, k);
                    processCase(i, j, k, tiling13_3_[_config][11], triangleRegion13_3_[_config][11],
                                vertexRegion13_3_[_config][11], volumeRegion13_3_[_config][11], 10, 3, v12);
                    break;

                case 39:
                    processCase(i, j, k, tiling13_2_[_config][0], triangleRegion13_2_[_config][0], vertexRegion13_2_[_config][0],
                                volumeRegion13_2_[_config][0], 6, 4);
                    break;
                case 40:
                    processCase(i, j, k, tiling13_2_[_config][1], triangleRegion13_2_[_config][1], vertexRegion13_2_[_config][1],
                                volumeRegion13_2_[_config][1], 6, 4);
                    break;
                case 41:
                    processCase(i, j, k, tiling13_2_[_config][2], triangleRegion13_2_[_config][2], vertexRegion13_2_[_config][2],
                                volumeRegion13_2_[_config][2], 6, 4);
                    break;
                case 42:
                    processCase(i, j, k, tiling13_2_[_config][3], triangleRegion13_2_[_config][3], vertexRegion13_2_[_config][3],
                                volumeRegion13_2_[_config][3], 6, 4);
                    break;
                case 43:
                    processCase(i, j, k, tiling13_2_[_config][4], triangleRegion13_2_[_config][4], vertexRegion13_2_[_config][4],
                                volumeRegion13_2_[_config][4], 6, 4);
                    break;
                case 44:
                    processCase(i, j, k, tiling13_2_[_config][5], triangleRegion13_2_[_config][5], vertexRegion13_2_[_config][5],
                                volumeRegion13_2_[_config][5], 6, 4);
                    break;
                case 45:
                    processCase(i, j, k, tiling13_1_[_config], triangleRegion13_1_[_config], vertexRegion13_1_[_config],
                                volumeRegion13_1_[_config], 4, 5);
                    break;

                default: OVITO_ASSERT_MSG(false, "Marching cubes", "Impossible case 13?");
            }
            break;

        case 14:
            processCase(i, j, k, tiling14[_config], triangleRegion14[_config], vertexRegion14[_config], volumeRegion14[_config],
                        4, 2);
            break;
    };
}

/******************************************************************************
 * Processes a single case from the marching cubes set of tables:
 * Adds triangles to the mesh, labels their regions and calculates incremental the volume per region
 ******************************************************************************/
void MarchingCubes::processCase(int i, int j, int k, const signed char* triangles, const signed char* triangleRegions,
                                const signed char* vertexRegions, const signed char** volumeRegionsTriangulation,
                                int numTriangles, int numVolumeRegions, SurfaceMesh::vertex_index v12)
{
    if(identifyRegions()) {
        // case 0 where all vertices belong to the same side of the iso surface
        // 0 triangles, 1 region, 1.0 volume
        const std::array<int, 5> localRegionMap{processRegionsVoxelVertices(i, j, k, vertexRegions)};
        if(!triangles || !triangleRegions || !volumeRegionsTriangulation) {
            Q_ASSERT((localRegionMap[0] != -1) && (localRegionMap[1] == -1) && (localRegionMap[2] == -1) &&
                     (localRegionMap[3] == -1) && (localRegionMap[4] == -1));
            _regionVolumes[localRegionMap[0]] += 1;
        }
        else {
            addTriangle(i, j, k, triangles, triangleRegions, localRegionMap, numTriangles, v12);
            addVolume(i, j, k, volumeRegionsTriangulation, localRegionMap, numVolumeRegions, v12);
        }
    }
    else {
        addTriangle(i, j, k, triangles, numTriangles, v12);
    }
}

/******************************************************************************
 * Assign a region to each vertex of the voxel based on the tabluated vertex region map.
 * Regions are merged as much as possible already. Subsequent union-find-merg operation remains necessary
 * get vertex regions from the table (range 0 to 5) and map them to the global region IDs
 * triRegions and vertexRegions both contain values from 0 to 5 and will be used as an index into the local region map.
 ******************************************************************************/
void MarchingCubes::processRegionsVoxelVertex(int i, int j, int k, signed char vertexRegion, std::array<int, 5>& localRegionMap)
{
    int vertex{getVertexRegion(i, j, k)};
    // A grid vertex on an open (non-periodic) boundary of the domain makes its empty region an exterior one.
    const bool onOpenBoundary = ((i == 0 || i == _size_x) && !_pbcFlags[0]) || ((j == 0 || j == _size_y) && !_pbcFlags[1]) ||
                                ((k == 0 || k == _size_z) && !_pbcFlags[2]);
    // if vertex and the local region have no ID, create a new one
    if((vertex == -1) && (localRegionMap[vertexRegion] == -1)) {
        setVertexRegion(i, j, k, _maxRegionIndex);
        localRegionMap[vertexRegion] = _maxRegionIndex;
        _regionFilled.push_back(((getFieldValue(i, j, k) - _isolevel) > _epsilon));
        _regionExterior.push_back(!_regionFilled[_regionFilled.size() - 1] && onOpenBoundary);
        _regionVolumes.push_back(0);
        _maxRegionIndex++;
    }
    // if the vertex has no ID but the local regions has one, the vertex is updated with the local ID
    else if((vertex == -1) && (localRegionMap[vertexRegion] != -1)) {
        setVertexRegion(i, j, k, localRegionMap[vertexRegion]);
        // The region may have been created away from the boundary; it still becomes exterior if it reaches an
        // open boundary at this vertex. (Previously only the vertex creating a region was checked, so e.g. the
        // empty layer on one side of a slab with an open z boundary was misclassified as an interior void.)
        if(onOpenBoundary && !_regionFilled[localRegionMap[vertexRegion]])
            _regionExterior[localRegionMap[vertexRegion]] = true;
    }
    // if the vertex has an ID but the local regions does not have one, the local regions mapping is updated
    else if((vertex != -1) && (localRegionMap[vertexRegion] == -1)) {
        localRegionMap[vertexRegion] = vertex;
    }
    // if both vertex and local regions map have differing IDs they get added to the region merge list
    else if((vertex != -1) && (localRegionMap[vertexRegion] != -1) && (vertex != localRegionMap[vertexRegion])) {
        _regionsToMerge.emplace_back(vertex, localRegionMap[vertexRegion]);
        OVITO_ASSERT(_regionFilled[vertex] == _regionFilled[localRegionMap[vertexRegion]]);
    }
}

/******************************************************************************
 * Processes each vertex of the voxel and adds it to the local region map
 ******************************************************************************/
std::array<int, 5> MarchingCubes::processRegionsVoxelVertices(int i, int j, int k, const signed char* vertexRegions)
{
    std::array<int, 5> localRegionMap{{-1, -1, -1, -1, -1}};
    processRegionsVoxelVertex(i, j, k, vertexRegions[0], localRegionMap);              // cube vertex 0
    processRegionsVoxelVertex(i + 1, j, k, vertexRegions[1], localRegionMap);          // cube vertex 1
    processRegionsVoxelVertex(i + 1, j + 1, k, vertexRegions[2], localRegionMap);      // cube vertex 2
    processRegionsVoxelVertex(i, j + 1, k, vertexRegions[3], localRegionMap);          // cube vertex 3
    processRegionsVoxelVertex(i, j, k + 1, vertexRegions[4], localRegionMap);          // cube vertex 4
    processRegionsVoxelVertex(i + 1, j, k + 1, vertexRegions[5], localRegionMap);      // cube vertex 5
    processRegionsVoxelVertex(i + 1, j + 1, k + 1, vertexRegions[6], localRegionMap);  // cube vertex 6
    processRegionsVoxelVertex(i, j + 1, k + 1, vertexRegions[7], localRegionMap);      // cube vertex 7
    return localRegionMap;
}

/******************************************************************************
 * Converts the local (per voxel) edge indices to global vertices used in the mesh
 ******************************************************************************/
SurfaceMesh::vertex_index MarchingCubes::localToGlobalEdgeVertex(int i, int j, int k, int edgeIndex,
                                                                       SurfaceMesh::vertex_index v12) const
{
    switch(edgeIndex) {
        case 0: return getEdgeVertex(i, j, k, 0);
        case 1: return getEdgeVertex(i + 1, j, k, 1);
        case 2: return getEdgeVertex(i, j + 1, k, 0);
        case 3: return getEdgeVertex(i, j, k, 1);
        case 4: return getEdgeVertex(i, j, k + 1, 0);
        case 5: return getEdgeVertex(i + 1, j, k + 1, 1);
        case 6: return getEdgeVertex(i, j + 1, k + 1, 0);
        case 7: return getEdgeVertex(i, j, k + 1, 1);
        case 8: return getEdgeVertex(i, j, k, 2);
        case 9: return getEdgeVertex(i + 1, j, k, 2);
        case 10: return getEdgeVertex(i + 1, j + 1, k, 2);
        case 11: return getEdgeVertex(i, j + 1, k, 2);
        case 12: return v12;
        default: OVITO_ASSERT_MSG(false, "Marching cubes", "invalid triangle"); return SurfaceMesh::InvalidIndex;
    }
}

/******************************************************************************
 * Adds triangles to the mesh and labels their regions.
 ******************************************************************************/
void MarchingCubes::addTriangle(int i, int j, int k, const signed char* triangles, const signed char* triangleRegions,
                                const std::array<int, 5>& localRegionMap, signed char numTriangles,
                                SurfaceMesh::vertex_index v12)
{
    SurfaceMesh::vertex_index tv[3];
    OVITO_ASSERT(identifyRegions());

    for(int t = 0; t < 3 * numTriangles; t++) {
        tv[t % 3] = localToGlobalEdgeVertex(i, j, k, triangles[t], v12);

        SurfaceMesh::face_index face1, face2;
        if(t % 3 == 2) {
            if(_lowerIsSolid) {
                face1 = _faceGrower.createFace({tv[0], tv[1], tv[2]}, localRegionMap[triangleRegions[t / 3 * 2]]);
                face2 = _faceGrower.createFace({tv[2], tv[1], tv[0]}, localRegionMap[triangleRegions[t / 3 * 2 + 1]]);
            }
            else {
                face2 = _faceGrower.createFace({tv[2], tv[1], tv[0]}, localRegionMap[triangleRegions[t / 3 * 2]]);
                face1 = _faceGrower.createFace({tv[0], tv[1], tv[2]}, localRegionMap[triangleRegions[t / 3 * 2 + 1]]);
            }
            _outputMesh.linkOppositeFaces(face1, face2);
            if(_outputCellCoordinates)
                _meshFaceVoxelCoordinates.emplace_back(i, j, k);
        }
    }
}

/******************************************************************************
 * Adds triangles to the mesh.
 ******************************************************************************/
void MarchingCubes::addTriangle(int i, int j, int k, const signed char* triangles, signed char numTriangles,
                                SurfaceMesh::vertex_index v12)
{
    SurfaceMesh::vertex_index tv[3];
    OVITO_ASSERT(!identifyRegions());

    for(int t = 0; t < 3 * numTriangles; t++) {
        tv[t % 3] = localToGlobalEdgeVertex(i, j, k, triangles[t], v12);

        if(t % 3 == 2) {
            if(_lowerIsSolid)
                _faceGrower.createFace({tv[0], tv[1], tv[2]});
            else
                _faceGrower.createFace({tv[2], tv[1], tv[0]});
            if(_outputCellCoordinates)
                _meshFaceVoxelCoordinates.emplace_back(i, j, k);
        }
    }
}

/******************************************************************************
 * Converts the local (per voxel) edge indices to global vertices used in the mesh
 ******************************************************************************/
Vector3 MarchingCubes::getTriangleEdgeVector(int i, int j, int k, int edgeIndex, SurfaceMesh::vertex_index v12) const
{
    OVITO_ASSERT(edgeIndex >= 0);
    // One of the cube corners:
    if(edgeIndex < 8)
        return getCornerVertex(i, j, k, edgeIndex);

    // Vertex along the cube edges:
    int axis{-1};
    int iPBC{i}, jPBC{j}, kPBC{k};
    switch(edgeIndex) {
        case 8: axis = 0; break;
        case 9:
            iPBC = i + 1;
            axis = 1;
            break;
        case 10:
            jPBC = j + 1;
            axis = 0;
            break;
        case 11: axis = 1; break;
        case 12:
            kPBC = k + 1;
            axis = 0;
            break;
        case 13:
            iPBC = i + 1;
            kPBC = k + 1;
            axis = 1;
            break;
        case 14:
            jPBC = j + 1;
            kPBC = k + 1;
            axis = 0;
            break;
        case 15:
            kPBC = k + 1;
            axis = 1;
            break;
        case 16: axis = 2; break;
        case 17:
            iPBC = i + 1;
            axis = 2;
            break;
        case 18:
            iPBC = i + 1;
            jPBC = j + 1;
            axis = 2;
            break;
        case 19:
            jPBC = j + 1;
            axis = 2;
            break;
        case 20: axis = -1; break;
        default: OVITO_ASSERT_MSG(false, "Marching cubes", "invalid triangle"); return Vector3{SurfaceMesh::InvalidIndex};
    }
    // case==20 -> central vertex
    if(axis == -1) {
        OVITO_ASSERT(v12 != SurfaceMesh::InvalidIndex);
        return _vertexGrower.vertexPosition(v12) - Point3::Origin();
    }
    int index{getEdgeVertex(iPBC, jPBC, kPBC, axis)};
    Vector3 result;
    // if index == -1 We found an edge vertex where both adjacent cube vertices belong to the same iso level and region.
    // therefore this edge vertex position has not been computed an its index will be -1
    // here we can just take the center of the edge as vertex point (e.g.: i + 0.5, j ,k) if the axis is 0.
    if(index == -1) {
        result = getCornerVertex(iPBC, jPBC, kPBC, 0);
        result[axis] += 0.5;
    }
    else {  // Otherwise the vertex position has been calculated and it can be grabbed from the mesh
        result = _vertexGrower.vertexPosition(index) - Point3::Origin();
        if(iPBC == _size_x) result[0] = _size_x;
        if(jPBC == _size_y) result[1] = _size_y;
        if(kPBC == _size_z) result[2] = _size_z;
    }
    return result;
}

/******************************************************************************
 * Adds triangles to the mesh and labels their regions.
 ******************************************************************************/
void MarchingCubes::addVolume(int i, int j, int k, const signed char** volumeRegions, const std::array<int, 5>& localRegionMap,
                              const int numVolumeRegions, SurfaceMesh::vertex_index v12)
{
    FloatType total_volume{0};
    Vector3 tv[3];
    for(int ri{0}; ri < localRegionMap.size(); ri++) {
        if(localRegionMap[ri] == -1) continue;
        OVITO_ASSERT(ri < numVolumeRegions);
        FloatType volume{0};
        for(int ti{0}; ti < (volumeRegions[ri][0]); ti++) {
            for(int t{0}; t < 3; t++) {
                // ti+1 since the first value in the volumeRegion array gives the number of triangles
                tv[t] = getTriangleEdgeVector(i, j, k, volumeRegions[ri][3 * ti + 1 + t], v12);
            }
            volume += tv[0].dot(tv[1].cross(tv[2]));
        }
        volume = 1.0 / 6.0 * std::abs(volume);
        OVITO_ASSERT(volume >= 0 && volume < (1 + 1e-6));
        total_volume += volume;
        _regionVolumes[localRegionMap[ri]] += volume;
    }
    OVITO_ASSERT_MSG(std::abs(total_volume - 1) < 1e-6, "Total Volume", std::to_string(total_volume).c_str());
}

/******************************************************************************
 * Gets the region for each voxel corner
 ******************************************************************************/
int MarchingCubes::getVertexRegion(int i, int j, int k) const
{
    // No valid region outside the domain if boundaries are non-periodic
    // Nothing is set, invalid index is returned. Has to be handled by the caller!
    if(!_pbcFlags[0] && (i < 0 || i >= _size_x)) return -1;
    if(!_pbcFlags[1] && (j < 0 || j >= _size_y)) return -1;
    if(!_pbcFlags[2] && (k < 0 || k >= _size_z)) return -1;
    i = (i < 0) ? i + _size_x : i;
    i = (i >= _size_x) ? i - _size_x : i;
    j = (j < 0) ? j + _size_y : j;
    j = (j >= _size_y) ? j - _size_y : j;
    k = (k < 0) ? k + _size_y : k;
    k = (k >= _size_z) ? k - _size_z : k;
    return _vertRegions[i + j * _size_x + k * _size_x * _size_y];
}

/******************************************************************************
 * Sets the region for each voxel corner
 ******************************************************************************/
void MarchingCubes::setVertexRegion(int i, int j, int k, int value)
{
    // No valid region outside the domain if boundaries are non-periodic
    // Nothing is set, invalid index is returned. Has to be handled by the caller!
    if(!_pbcFlags[0] && (i < 0 || i >= _size_x)) return;
    if(!_pbcFlags[1] && (j < 0 || j >= _size_y)) return;
    if(!_pbcFlags[2] && (k < 0 || k >= _size_z)) return;

    i = (i < 0) ? i + _size_x : i;
    i = (i >= _size_x) ? i - _size_x : i;
    j = (j < 0) ? j + _size_y : j;
    j = (j >= _size_y) ? j - _size_y : j;
    k = (k < 0) ? k + _size_y : k;
    k = (k >= _size_z) ? k - _size_z : k;
    _vertRegions[i + j * _size_x + k * _size_x * _size_y] = value;
}

/******************************************************************************
 * Accesses the pre-computed vertex on a lower edge of a specific cube.
 ******************************************************************************/
SurfaceMesh::vertex_index MarchingCubes::getEdgeVertex(int i, int j, int k, int axis) const
{
    OVITO_ASSERT(i >= 0 && i <= _size_x);
    OVITO_ASSERT(j >= 0 && j <= _size_y);
    OVITO_ASSERT(k >= 0 && k <= _size_z);
    OVITO_ASSERT(axis >= 0 && axis < 3);
    if(i == _size_x) i = 0;
    if(j == _size_y) j = 0;
    if(k == _size_z) k = 0;
    return _cubeVerts[(i + j * _size_x + k * _size_x * _size_y) * 3 + axis];
}

/******************************************************************************
 * Calculates the position of a specific voxel corner in the volume.
 ******************************************************************************/
Vector3 MarchingCubes::getCornerVertex(int i, int j, int k, int edgeIndex) const
{
    OVITO_ASSERT(i >= 0 && i <= _size_x);
    OVITO_ASSERT(j >= 0 && j <= _size_y);
    OVITO_ASSERT(k >= 0 && k <= _size_z);
    OVITO_ASSERT(edgeIndex >= 0 && edgeIndex < 8);
    // Shift by -1 if the boundary conditions are non periodic.
    // Offset can also be found in createEdgeVertex...()
    FloatType ift = static_cast<FloatType>(i - (_pbcFlags[0] ? 0 : 1));
    FloatType jft = static_cast<FloatType>(j - (_pbcFlags[1] ? 0 : 1));
    FloatType kft = static_cast<FloatType>(k - (_pbcFlags[2] ? 0 : 1));
    switch(edgeIndex) {
        case 0: return Vector3{ift, jft, kft};
        case 1: return Vector3{ift + 1, jft, kft};
        case 2: return Vector3{ift + 1, jft + 1, kft};
        case 3: return Vector3{ift, jft + 1, kft};
        case 4: return Vector3{ift, jft, kft + 1};
        case 5: return Vector3{ift + 1, jft, kft + 1};
        case 6: return Vector3{ift + 1, jft + 1, kft + 1};
        case 7: return Vector3{ift, jft + 1, kft + 1};
        default:
            OVITO_ASSERT_MSG(false, "MarchingCubes::getCornerVertex()", "invalid corner");
            return Vector3::Zero();
    }
}

/******************************************************************************
 * Adds a vertex on the current horizontal edge.
 ******************************************************************************/
SurfaceMesh::vertex_index MarchingCubes::createEdgeVertexX(int i, int j, int k, FloatType u)
{
    OVITO_ASSERT(i >= 0 && i < _size_x);
    OVITO_ASSERT(j >= 0 && j < _size_y);
    OVITO_ASSERT(k >= 0 && k < _size_z);
    auto v = _vertexGrower.createVertex(Point3(i + u - (_pbcFlags[0] ? 0 : 1), j - (_pbcFlags[1] ? 0 : 1), k - (_pbcFlags[2] ? 0 : 1)));
    _cubeVerts[(i + j * _size_x + k * _size_x * _size_y) * 3 + 0] = v;
    return v;
}

/******************************************************************************
 * Adds a vertex on the current longitudinal edge.
 ******************************************************************************/
SurfaceMesh::vertex_index MarchingCubes::createEdgeVertexY(int i, int j, int k, FloatType u)
{
    OVITO_ASSERT(i >= 0 && i < _size_x);
    OVITO_ASSERT(j >= 0 && j < _size_y);
    OVITO_ASSERT(k >= 0 && k < _size_z);
    auto v = _vertexGrower.createVertex(Point3(i - (_pbcFlags[0] ? 0 : 1), j + u - (_pbcFlags[1] ? 0 : 1), k - (_pbcFlags[2] ? 0 : 1)));
    _cubeVerts[(i + j * _size_x + k * _size_x * _size_y) * 3 + 1] = v;
    return v;
}

/******************************************************************************
 *  Adds a vertex on the current vertical edge.
 ******************************************************************************/
SurfaceMesh::vertex_index MarchingCubes::createEdgeVertexZ(int i, int j, int k, FloatType u)
{
    OVITO_ASSERT(i >= 0 && i < _size_x);
    OVITO_ASSERT(j >= 0 && j < _size_y);
    OVITO_ASSERT(k >= 0 && k < _size_z);
    auto v = _vertexGrower.createVertex(Point3(i - (_pbcFlags[0] ? 0 : 1), j - (_pbcFlags[1] ? 0 : 1), k + u - (_pbcFlags[2] ? 0 : 1)));
    _cubeVerts[(i + j * _size_x + k * _size_x * _size_y) * 3 + 2] = v;
    return v;
}

/******************************************************************************
 * Adds a vertex inside the current cube.
 ******************************************************************************/
SurfaceMesh::vertex_index MarchingCubes::createCenterVertex(int i, int j, int k)
{
    int u = 0;
    Point3 p = Point3::Origin();

    // Computes the average of the intersection points of the cube
    auto addPosition = [this, &p, &u](int i, int j, int k, int axis) {
        SurfaceMesh::vertex_index v = getEdgeVertex(i, j, k, axis);
        if(v != SurfaceMesh::InvalidIndex) {
            const Point3& vp = _vertexGrower.vertexPosition(v);
            p.x() += vp.x();
            p.y() += vp.y();
            p.z() += vp.z();
            if(i == _size_x) p.x() += _size_x;
            if(j == _size_y) p.y() += _size_y;
            if(k == _size_z) p.z() += _size_z;
            ++u;
        }
    };
    addPosition(i, j, k, 0);
    addPosition(i + 1, j, k, 1);
    addPosition(i, j + 1, k, 0);
    addPosition(i, j, k, 1);
    addPosition(i, j, k + 1, 0);
    addPosition(i + 1, j, k + 1, 1);
    addPosition(i, j + 1, k + 1, 0);
    addPosition(i, j, k + 1, 1);
    addPosition(i, j, k, 2);
    addPosition(i + 1, j, k, 2);
    addPosition(i + 1, j + 1, k, 2);
    addPosition(i, j + 1, k, 2);

    p.x() /= u;
    p.y() /= u;
    p.z() /= u;

#if 0
    // Optimize the position of the central vertex to exactly match the desired isolevel
    // Requires the https://github.com/tirimatangi/LazyMath headers to work
    // Currently disabled for overhead as the div
    auto trilinearFieldInterpolation = [&](const auto& x, auto& fx) {
        // return a high function value (with gradient pointing towards the [0,1] domain
        // when any x,y,z is outside the voxel
        if((x[0] < 0) || (x[1] < 0) || (x[2] < 0) || (x[0] > 1) || (x[1] > 1) || (x[2] > 1)) {
            fx[0] = (x[0] * x[0] + x[1] * x[1] + x[2] * x[2]) + 1e6;
        }
        // return the interpolated value inside the voxel
        else {
            FloatType c00 = getFieldValue(i, j, k) * (1 - x[0]) + getFieldValue(i + 1, j, k) * x[0];
            FloatType c01 = getFieldValue(i, j, k + 1) * (1 - x[0]) + getFieldValue(i + 1, j, k + 1) * x[0];
            FloatType c10 = getFieldValue(i, j + 1, k) * (1 - x[0]) + getFieldValue(i + 1, j + 1, k) * x[0];
            FloatType c11 = getFieldValue(i, j + 1, k + 1) * (1 - x[0]) + getFieldValue(i + 1, j + 1, k + 1) * x[0];
            FloatType c0 = c00 * (1 - x[1]) + c10 * x[1];
            FloatType c1 = c01 * (1 - x[1]) + c11 * x[1];
            FloatType c = (c0 * (1 - x[2]) + c1 * x[2]) - _isolevel;
            fx[0] = c * c;
        }
    };
    // Optimize the position of the v12 vertex to match the isolevel
    LazyMath::Minimizer<FloatType> minimizer(3, 1);
    minimizer.function = std::move(trilinearFieldInterpolation);
    minimizer.optimalityLimit = 0;
    minimizer.maxIterations = 100;
    minimizer.initX = {p.x() - i, p.y() - j, p.z() - k};
    minimizer.run();
    // if the point found be the minimizer is on one of the edges (edges always contain at least one solution if
    // its two vertices are on opposite sides of the isolevel) we fall back onto p as solution.
    // Same if the minimizer did not find a solution
    FloatType eps = 1e-6;
    if(!((minimizer.minFx()[0] > eps) || (minimizer.minX()[0] < eps) || (minimizer.minX()[0] > (1 - eps)) || (minimizer.minX()[1] < eps) ||
         (minimizer.minX()[1] > (1 - eps)) || (minimizer.minX()[2] < eps) || (minimizer.minX()[2] > (1 - eps)))) {
        p.x() = i + minimizer.minX()[0];
        p.y() = j + minimizer.minX()[1];
        p.z() = k + minimizer.minX()[2];
    }
#endif

    return _vertexGrower.createVertex(p);
}

}  // namespace Ovito::Grid
