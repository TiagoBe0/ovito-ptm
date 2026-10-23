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
#include <ovito/crystalanalysis/objects/DislocationNetwork.h>
#include <ovito/crystalanalysis/objects/DislocationVis.h>
#include <ovito/crystalanalysis/objects/ClusterGraph.h>
#include <ovito/crystalanalysis/modifier/dxa/DislocationAnalysisEngine.h>
#include <ovito/mesh/surface/SurfaceMesh.h>
#include <ovito/mesh/surface/SurfaceMeshVis.h>
#include <ovito/stdobj/simcell/SimulationCell.h>
#include <ovito/core/utilities/io/CompressedTextReader.h>
#include "CAImporter.h"

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(CAImporter);
OVITO_CLASSINFO(CAImporter, "DisplayName", "CA File");

/******************************************************************************
* Checks if the given file has format that can be read by this importer.
******************************************************************************/
bool CAImporter::OOMetaClass::checkFileFormat(const FileHandle& file) const
{
    // Open input file.
    CompressedTextReader stream(file);

    // Read first line.
    stream.readLine(20);

    // Files start with the string "CA_FILE_VERSION ".
    if(stream.lineStartsWith("CA_FILE_VERSION "))
        return true;

    return false;
}

/******************************************************************************
* Scans the data file and builds a list of source frames.
******************************************************************************/
void CAImporter::discoverFramesInFile(const FileHandle& fileHandle, QVector<FileSourceImporter::Frame>& frames) const
{
    CompressedTextReader stream(fileHandle);

    TaskProgress progress(this_task::ui());
    progress.setText(tr("Scanning CA file %1").arg(stream.filename()));
    progress.setMaximum(stream.underlyingSize());

    Frame frame(fileHandle);
    int frameNumber = 0;

    while(!stream.eof() && !this_task::isCanceled()) {

        if(frameNumber == 0) {
            frame.byteOffset = stream.byteOffset();
            stream.readLine();
        }

        if(stream.line()[0] == '\0') break;
        if(!stream.lineStartsWith("CA_FILE_VERSION "))
            throw Exception(tr("Failed to parse file. This is not a proper file written by the Crystal Analysis Tool or OVITO."));

        // Create a new record for the frame.
        frame.lineNumber = stream.lineNumber();
        frame.label.setFrameOfFile(frameNumber++);
        frames.push_back(frame);
        stream.recordSeekPoint();

        // Seek to end of frame record.
        while(!stream.eof()) {
            frame.byteOffset = stream.byteOffset();
            stream.readLineTrimLeft();
            if(stream.lineStartsWith("CA_FILE_VERSION "))
                break;
            if((stream.lineNumber() % 4096) == 0)
                progress.setValue(stream.underlyingByteOffset());
        }
    }
}

/******************************************************************************
* Parses the given input file.
******************************************************************************/
void CAImporter::FrameLoader::loadFile()
{
    TaskProgress progress(this_task::ui());
    progress.setText(tr("Reading CA file %1").arg(fileHandle().toString()));

    // Open file for reading, positioned at the requested frame. A single CA file may hold a whole
    // sequence of frames concatenated (discoverFramesInFile() records one byte offset per frame);
    // without seeking here, every animation frame would return the first record of the file.
    CompressedTextReader stream(fileHandle(), frame().byteOffset, frame().lineNumber);

    // Read file header.
    stream.readLine();
    if(!stream.lineStartsWith("CA_FILE_VERSION "))
        throw Exception(tr("Failed to parse file. This is not a proper CA file written by OVITO or the Crystal Analysis Tool."));
    int fileFormatVersion = 0;
    if(sscanf(stream.line(), "CA_FILE_VERSION %i", &fileFormatVersion) != 1)
        throw Exception(tr("Failed to parse file. This is not a proper CA file written by OVITO or the Crystal Analysis Tool."));
    if(fileFormatVersion != 4 && fileFormatVersion != 5 && fileFormatVersion != 6 && fileFormatVersion != 7)
        throw Exception(tr("Failed to parse file. This CA file format version is not supported: %1").arg(fileFormatVersion));
    stream.readLine();
    if(!stream.lineStartsWith("CA_LIB_VERSION"))
        throw Exception(tr("Failed to parse file. This is not a proper CA file written by OVITO or the Crystal Analysis Tool."));

    struct BurgersVectorFamilyInfo {
        int id = 0;
        QString name;
        Vector3 burgersVector = Vector3::Zero();
        Color color = Color(1,1,1);
    };

    struct PatternInfo {
        int id = 0;
        MicrostructurePhase::Dimensionality type = MicrostructurePhase::Dimensionality::Volumetric;
        MicrostructurePhase::CrystalSymmetryClass symmetryType = MicrostructurePhase::CrystalSymmetryClass::CubicSymmetry;
        QString shortName;
        QString longName;
        Color color = Color(1,1,1);
        QVector<BurgersVectorFamilyInfo> burgersVectorFamilies;
    };

    QString caFilename;
    QString atomsFilename;
    AffineTransformation cell = AffineTransformation::Zero();
    int pbcFlags[3] = {1,1,1};
    int numClusters = 0;
    int numClusterTransitions = 0;
    int numDislocationSegments = 0;
    SurfaceMesh* defectSurface = nullptr;
    DataOORef<DislocationNetwork> dislocations;
    DataOORef<ClusterGraph> clusterGraph = DataOORef<ClusterGraph>::create();
    QVector<PatternInfo> patterns;

    while(!stream.eof()) {
        stream.readLineTrimLeft();

        // Read file path information.
        if(stream.lineStartsWith("OUTPUT_PATH ")) {
            caFilename = stream.lineString().mid(12).trimmed();
        }
        else if(stream.lineStartsWith("ATOMS_PATH ")) {
            atomsFilename = stream.lineString().mid(11).trimmed();
        }
        else if(stream.lineStartsWith("STRUCTURE_PATTERNS ") || stream.lineStartsWith("STRUCTURE_TYPES ")) {
            // Read pattern catalog.
            int numPatterns;
            if(sscanf(stream.line(), "%*s %i", &numPatterns) != 1 || numPatterns <= 0)
                throw Exception(tr("Failed to parse file. Invalid number of structure types in line %1.").arg(stream.lineNumber()));
            std::vector<int> patternId2Index;
            for(int index = 0; index < numPatterns; index++) {
                PatternInfo pattern;
                if(fileFormatVersion <= 4) {
                    if(sscanf(stream.readLine(), "PATTERN ID %i", &pattern.id) != 1)
                        throw Exception(tr("Failed to parse file. Invalid pattern ID in line %1.").arg(stream.lineNumber()));
                }
                else {
                    if(sscanf(stream.readLine(), "STRUCTURE_TYPE %i", &pattern.id) != 1)
                        throw Exception(tr("Failed to parse file. Invalid structure type ID in line %1.").arg(stream.lineNumber()));
                }
                if((int)patternId2Index.size() <= pattern.id)
                    patternId2Index.resize(pattern.id+1);
                patternId2Index[pattern.id] = index;
                while(!stream.eof()) {
                    stream.readLineTrimLeft();
                    if(stream.lineStartsWith("NAME ")) {
                        pattern.shortName = stream.lineString().mid(5).trimmed();
                    }
                    else if(stream.lineStartsWith("FULL_NAME ")) {
                        pattern.longName = stream.lineString().mid(9).trimmed();
                    }
                    else if(stream.lineStartsWith("TYPE ")) {
                        QString patternTypeString = stream.lineString().mid(5).trimmed();
                        if(patternTypeString == QStringLiteral("LATTICE")) pattern.type = MicrostructurePhase::Dimensionality::Volumetric;
                        else if(patternTypeString == QStringLiteral("INTERFACE")) pattern.type = MicrostructurePhase::Dimensionality::Planar;
                        else if(patternTypeString == QStringLiteral("POINTDEFECT")) pattern.type = MicrostructurePhase::Dimensionality::Pointlike;
                        else throw Exception(tr("Failed to parse file. Invalid pattern type in line %1: %2").arg(stream.lineNumber()).arg(patternTypeString));
                    }
                    else if(stream.lineStartsWith("COLOR ")) {
                        if(sscanf(stream.line(), "COLOR " FLOATTYPE_SCANF_STRING " " FLOATTYPE_SCANF_STRING " " FLOATTYPE_SCANF_STRING, &pattern.color.r(), &pattern.color.g(), &pattern.color.b()) != 3)
                            throw Exception(tr("Failed to parse file. Invalid pattern color in line %1.").arg(stream.lineNumber()));
                    }
                    else if(stream.lineStartsWith("BURGERS_VECTOR_FAMILIES ")) {
                        int numFamilies;
                        if(sscanf(stream.line(), "BURGERS_VECTOR_FAMILIES %i", &numFamilies) != 1 || numFamilies < 0)
                            throw Exception(tr("Failed to parse file. Invalid number of Burgers vectors families in line %1.").arg(stream.lineNumber()));
                        for(int familyIndex = 0; familyIndex < numFamilies; familyIndex++) {
                            BurgersVectorFamilyInfo family;
                            if(sscanf(stream.readLine(), "BURGERS_VECTOR_FAMILY ID %i", &family.id) != 1)
                                throw Exception(tr("Failed to parse file. Invalid Burgers vector family ID in line %1.").arg(stream.lineNumber()));
                            stream.readLine();
                            family.name = stream.lineString().trimmed();
                            if(sscanf(stream.readLine(), FLOATTYPE_SCANF_STRING " " FLOATTYPE_SCANF_STRING " " FLOATTYPE_SCANF_STRING, &family.burgersVector.x(), &family.burgersVector.y(), &family.burgersVector.z()) != 3)
                                throw Exception(tr("Failed to parse file. Invalid Burgers vector in line %1.").arg(stream.lineNumber()));
                            if(sscanf(stream.readLine(), FLOATTYPE_SCANF_STRING " " FLOATTYPE_SCANF_STRING " " FLOATTYPE_SCANF_STRING, &family.color.r(), &family.color.g(), &family.color.b()) != 3)
                                throw Exception(tr("Failed to parse file. Invalid color in line %1.").arg(stream.lineNumber()));
                            pattern.burgersVectorFamilies.push_back(std::move(family));
                        }
                    }
                    else if(stream.lineStartsWith("END_PATTERN") || stream.lineStartsWith("END_STRUCTURE_TYPE"))
                        break;
                }
                if(pattern.longName.isEmpty())
                    pattern.longName = pattern.shortName;
                patterns.push_back(std::move(pattern));
            }
        }
        else if(stream.lineStartsWith("SIMULATION_CELL_ORIGIN ")) {
            // Read simulation cell geometry.
            if(sscanf(stream.line(), "SIMULATION_CELL_ORIGIN " FLOATTYPE_SCANF_STRING " " FLOATTYPE_SCANF_STRING " " FLOATTYPE_SCANF_STRING, &cell(0,3), &cell(1,3), &cell(2,3)) != 3)
                throw Exception(tr("Failed to parse file. Invalid cell origin in line %1.").arg(stream.lineNumber()));
        }
        else if(stream.lineStartsWith("SIMULATION_CELL ")) {
            if(sscanf(stream.line(), "SIMULATION_CELL " FLOATTYPE_SCANF_STRING " " FLOATTYPE_SCANF_STRING " " FLOATTYPE_SCANF_STRING
                    " " FLOATTYPE_SCANF_STRING " " FLOATTYPE_SCANF_STRING " " FLOATTYPE_SCANF_STRING
                    " " FLOATTYPE_SCANF_STRING " " FLOATTYPE_SCANF_STRING " " FLOATTYPE_SCANF_STRING,
                    &cell(0,0), &cell(0,1), &cell(0,2), &cell(1,0), &cell(1,1), &cell(1,2), &cell(2,0), &cell(2,1), &cell(2,2)) != 9)
                throw Exception(tr("Failed to parse file. Invalid cell vectors in line %1.").arg(stream.lineNumber()));
        }
        else if(stream.lineStartsWith("SIMULATION_CELL_MATRIX")) {
            for(size_t row = 0; row < 3; row++) {
                if(sscanf(stream.readLine(), FLOATTYPE_SCANF_STRING " " FLOATTYPE_SCANF_STRING " " FLOATTYPE_SCANF_STRING,
                        &cell(row,0), &cell(row,1), &cell(row,2)) != 3)
                    throw Exception(tr("Failed to parse file. Invalid cell matrix in line %1.").arg(stream.lineNumber()));
            }
        }
        else if(stream.lineStartsWith("PBC_FLAGS ")) {
            if(sscanf(stream.line(), "PBC_FLAGS %i %i %i", &pbcFlags[0], &pbcFlags[1], &pbcFlags[2]) != 3)
                throw Exception(tr("Failed to parse file. Invalid PBC flags in line %1.").arg(stream.lineNumber()));
        }
        else if(stream.lineStartsWith("CLUSTERS ")) {
            // Read cluster list.
            if(sscanf(stream.line(), "CLUSTERS %i", &numClusters) != 1)
                throw Exception(tr("Failed to parse file. Invalid number of clusters in line %1.").arg(stream.lineNumber()));
            progress.setText(tr("Reading clusters"));
            progress.setMaximum(numClusters);
            for(int index = 0; index < numClusters; index++) {
                progress.setValueIntermittent(index);
                if(fileFormatVersion <= 4) {
                    int patternId = 0, clusterId = 0, clusterProc = 0;
                    stream.readLine();
                    if(sscanf(stream.readLine(), "%i %i", &clusterId, &clusterProc) != 2)
                        throw Exception(tr("Failed to parse file. Invalid cluster ID in line %1.").arg(stream.lineNumber()));
                    if(sscanf(stream.readLine(), "%i", &patternId) != 1)
                        throw Exception(tr("Failed to parse file. Invalid cluster pattern index in line %1.").arg(stream.lineNumber()));
                    Cluster* cluster = clusterGraph->createCluster(patternId);
                    OVITO_ASSERT(cluster->structure != 0);
                    if(sscanf(stream.readLine(), "%i", &cluster->atomCount) != 1)
                        throw Exception(tr("Failed to parse file. Invalid cluster atom count in line %1.").arg(stream.lineNumber()));
                    if(sscanf(stream.readLine(), FLOATTYPE_SCANF_STRING " " FLOATTYPE_SCANF_STRING " " FLOATTYPE_SCANF_STRING, &cluster->centerOfMass.x(), &cluster->centerOfMass.y(), &cluster->centerOfMass.z()) != 3)
                        throw Exception(tr("Failed to parse file. Invalid cluster center of mass in line %1.").arg(stream.lineNumber()));
                    if(sscanf(stream.readLine(), FLOATTYPE_SCANF_STRING " " FLOATTYPE_SCANF_STRING " " FLOATTYPE_SCANF_STRING
                            " " FLOATTYPE_SCANF_STRING " " FLOATTYPE_SCANF_STRING " " FLOATTYPE_SCANF_STRING
                            " " FLOATTYPE_SCANF_STRING " " FLOATTYPE_SCANF_STRING " " FLOATTYPE_SCANF_STRING,
                            &cluster->orientation(0,0), &cluster->orientation(0,1), &cluster->orientation(0,2),
                            &cluster->orientation(1,0), &cluster->orientation(1,1), &cluster->orientation(1,2),
                            &cluster->orientation(2,0), &cluster->orientation(2,1), &cluster->orientation(2,2)) != 9)
                        throw Exception(tr("Failed to parse file. Invalid cluster orientation matrix in line %1.").arg(stream.lineNumber()));
                }
                else {
                    int patternId = 0, clusterId = 0, atomCount = 0;
                    Point3 centerOfMass = Point3::Origin();
                    Matrix3 orientation = Matrix3::Identity();
                    Color color(1,1,1);
                    while(!stream.eof()) {
                        stream.readLineTrimLeft();
                        if(stream.lineStartsWith("CLUSTER ")) {
                            if(sscanf(stream.line(), "CLUSTER %i", &clusterId) != 1)
                                throw Exception(tr("Failed to parse file. Invalid cluster ID in line %1.").arg(stream.lineNumber()));
                        }
                        else if(stream.lineStartsWith("CLUSTER_STRUCTURE ")) {
                            if(sscanf(stream.line(), "CLUSTER_STRUCTURE %i", &patternId) != 1)
                                throw Exception(tr("Failed to parse file. Invalid cluster structure type in line %1.").arg(stream.lineNumber()));
                        }
                        else if(stream.lineStartsWith("CLUSTER_SIZE ")) {
                            if(sscanf(stream.line(), "CLUSTER_SIZE %i", &atomCount) != 1)
                                throw Exception(tr("Failed to parse file. Invalid cluster size in line %1.").arg(stream.lineNumber()));
                        }
                        else if(stream.lineStartsWith("CLUSTER_CENTER_OF_MASS ")) {
                            if(sscanf(stream.line(), "CLUSTER_CENTER_OF_MASS " FLOATTYPE_SCANF_STRING " " FLOATTYPE_SCANF_STRING " " FLOATTYPE_SCANF_STRING, &centerOfMass.x(), &centerOfMass.y(), &centerOfMass.z()) != 3)
                                throw Exception(tr("Failed to parse file. Invalid cluster center in line %1.").arg(stream.lineNumber()));
                        }
                        else if(stream.lineStartsWith("CLUSTER_COLOR ")) {
                            if(sscanf(stream.line(), "CLUSTER_COLOR " FLOATTYPE_SCANF_STRING " " FLOATTYPE_SCANF_STRING " " FLOATTYPE_SCANF_STRING, &color.r(), &color.g(), &color.b()) != 3)
                                throw Exception(tr("Failed to parse file. Invalid cluster color in line %1.").arg(stream.lineNumber()));
                        }
                        else if(stream.lineStartsWith("CLUSTER_ORIENTATION")) {
                            for(size_t row = 0; row < 3; row++) {
                                if(sscanf(stream.readLine(), FLOATTYPE_SCANF_STRING " " FLOATTYPE_SCANF_STRING " " FLOATTYPE_SCANF_STRING,
                                        &orientation(row,0), &orientation(row,1), &orientation(row,2)) != 3)
                                    throw Exception(tr("Failed to parse file. Invalid cluster orientation matrix in line %1.").arg(stream.lineNumber()));
                            }
                        }
                        else if(stream.lineStartsWith("END_CLUSTER"))
                            break;
                    }
                    Cluster* cluster = clusterGraph->createCluster(patternId);
                    if(cluster->id != clusterId)
                        throw Exception(tr("Failed to parse file. Invalid cluster id: %1.").arg(clusterId));
                    cluster->atomCount = atomCount;
                    cluster->centerOfMass = centerOfMass;
                    cluster->orientation = orientation;
                    cluster->color = color;
                }
            }
        }
        else if(stream.lineStartsWith("CLUSTER_TRANSITIONS ")) {
            // Read cluster transition list.
            if(sscanf(stream.line(), "CLUSTER_TRANSITIONS %i", &numClusterTransitions) != 1)
                throw Exception(tr("Failed to parse file. Invalid number of cluster transitions in line %1.").arg(stream.lineNumber()));
            progress.setText(tr("Reading cluster transitions"));
            progress.setMaximum(numClusterTransitions);
            for(int index = 0; index < numClusterTransitions; index++) {
                progress.setValueIntermittent(index);
                int clusterIndex1, clusterIndex2;
                if(sscanf(stream.readLine(), "TRANSITION %i %i", &clusterIndex1, &clusterIndex2) != 2 || clusterIndex1 >= numClusters || clusterIndex2 >= numClusters) {
                    throw Exception(tr("Failed to parse file. Invalid cluster transition in line %1.").arg(stream.lineNumber()));
                }
                Matrix3 tm;
                if(sscanf(stream.readLine(), FLOATTYPE_SCANF_STRING " " FLOATTYPE_SCANF_STRING " " FLOATTYPE_SCANF_STRING
                        " " FLOATTYPE_SCANF_STRING " " FLOATTYPE_SCANF_STRING " " FLOATTYPE_SCANF_STRING
                        " " FLOATTYPE_SCANF_STRING " " FLOATTYPE_SCANF_STRING " " FLOATTYPE_SCANF_STRING,
                        &tm(0,0), &tm(0,1), &tm(0,2),
                        &tm(1,0), &tm(1,1), &tm(1,2),
                        &tm(2,0), &tm(2,1), &tm(2,2)) != 9)
                    throw Exception(tr("Failed to parse file. Invalid cluster transition matrix in line %1.").arg(stream.lineNumber()));
                clusterGraph->createClusterTransition(clusterGraph->clusters()[clusterIndex1+1], clusterGraph->clusters()[clusterIndex2+1], tm);
            }
        }
        else if(stream.lineStartsWith("DISLOCATIONS ")) {
            // Read dislocations list.
            if(sscanf(stream.line(), "DISLOCATIONS %i", &numDislocationSegments) != 1)
                throw Exception(tr("Failed to parse file. Invalid number of dislocation segments in line %1.").arg(stream.lineNumber()));
            progress.setText(tr("Reading dislocations"));
            progress.setMaximum(numDislocationSegments);
            if(const DislocationNetwork* existingDislocationsObj = state().getObject<DislocationNetwork>()) {
                dislocations = state().makeMutable(existingDislocationsObj);
            }
            else {
                dislocations = state().createObject<DislocationNetwork>(pipelineNode());
            }
            dislocations->setClusterGraph(clusterGraph);
            dislocations->segments().clear();
            for(int index = 0; index < numDislocationSegments; index++) {
                progress.setValueIntermittent(index);
                int segmentId;
                if(sscanf(stream.readLine(), "%i", &segmentId) != 1)
                    throw Exception(tr("Failed to parse file. Invalid segment ID in line %1.").arg(stream.lineNumber()));

                Vector3 burgersVector;
                if(sscanf(stream.readLine(), FLOATTYPE_SCANF_STRING " " FLOATTYPE_SCANF_STRING " " FLOATTYPE_SCANF_STRING, &burgersVector.x(), &burgersVector.y(), &burgersVector.z()) != 3)
                    throw Exception(tr("Failed to parse file. Invalid Burgers vector in line %1.").arg(stream.lineNumber()));

                Cluster* cluster = nullptr;
                if(fileFormatVersion <= 4) {
                    int clusterIndex;
                    if(sscanf(stream.readLine(), "%i", &clusterIndex) != 1 || clusterIndex < 0 || clusterIndex >= numClusters)
                        throw Exception(tr("Failed to parse file. Invalid cluster index in line %1.").arg(stream.lineNumber()));
                    cluster = clusterGraph->clusters()[clusterIndex+1];
                }
                else {
                    int clusterId;
                    if(sscanf(stream.readLine(), "%i", &clusterId) != 1 || clusterId <= 0)
                        throw Exception(tr("Failed to parse file. Invalid cluster ID in line %1.").arg(stream.lineNumber()));
                    cluster = clusterGraph->findCluster(clusterId);
                }
                if(!cluster)
                    throw Exception(tr("Failed to parse file. Invalid cluster reference in line %1.").arg(stream.lineNumber()));

                DislocationSegment* segment = dislocations->createSegment(ClusterVector(burgersVector, cluster));

                // Read polyline.
                int numPoints;
                if(sscanf(stream.readLine(), "%i", &numPoints) != 1 || numPoints <= 1)
                    throw Exception(tr("Failed to parse file. Invalid segment number of points in line %1.").arg(stream.lineNumber()));
                segment->line.resize(numPoints);
                for(Point3& p : segment->line) {
                    if(fileFormatVersion <= 4) {
                        if(sscanf(stream.readLine(), FLOATTYPE_SCANF_STRING " " FLOATTYPE_SCANF_STRING " " FLOATTYPE_SCANF_STRING, &p.x(), &p.y(), &p.z()) != 3)
                            throw Exception(tr("Failed to parse file. Invalid point in line %1.").arg(stream.lineNumber()));
                    }
                    else {
                        int coreSize = 0;
                        if(sscanf(stream.readLine(), FLOATTYPE_SCANF_STRING " " FLOATTYPE_SCANF_STRING " " FLOATTYPE_SCANF_STRING " %i", &p.x(), &p.y(), &p.z(), &coreSize) < 3)
                            throw Exception(tr("Failed to parse file. Invalid point in line %1.").arg(stream.lineNumber()));
                        if(coreSize > 0)
                            segment->coreSize.push_back(coreSize);
                    }
                }

                if(fileFormatVersion <= 4) {
                    // Read dislocation core size.
                    segment->coreSize.resize(numPoints);
                    for(int& coreSize : segment->coreSize) {
                        if(sscanf(stream.readLine(), "%i", &coreSize) != 1)
                            throw Exception(tr("Failed to parse file. Invalid core size in line %1.").arg(stream.lineNumber()));
                    }
                }
            }
        }
        else if(stream.lineStartsWith("DISLOCATION_JUNCTIONS") && dislocations) {
            // Read dislocation junctions.
            for(int index = 0; index < numDislocationSegments; index++) {
                DislocationSegment* segment = dislocations->segments()[index];
                for(int nodeIndex = 0; nodeIndex < 2; nodeIndex++) {
                    int isForward, otherSegmentId;
                    if(sscanf(stream.readLine(), "%i %i", &isForward, &otherSegmentId) != 2 || otherSegmentId < 0 || otherSegmentId >= numDislocationSegments)
                        throw Exception(tr("Failed to parse file. Invalid dislocation junction record in line %1.").arg(stream.lineNumber()));
                    segment->nodes[nodeIndex]->junctionRing = dislocations->segments()[otherSegmentId]->nodes[isForward ? 0 : 1];
                }
            }
        }
        else if(stream.lineStartsWith("DEFECT_MESH_VERTICES ")) {

            // Create surface mesh.
            defectSurface = state().getMutableLeafObject<SurfaceMesh>(SurfaceMesh::OOClass(), QStringLiteral("dxa-defect-mesh"));
            if(!defectSurface) {
                defectSurface = state().createObject<SurfaceMesh>(pipelineNode(), tr("Defect mesh"));
                defectSurface->setIdentifier(QStringLiteral("dxa-defect-mesh"));
                SurfaceMeshVis* vis = defectSurface->visElement<SurfaceMeshVis>();
                vis->setShowCap(true);
                vis->setSmoothShading(true);
                vis->setReverseOrientation(true);
                vis->setCapTransparency(0.5);
                vis->setObjectTitle(tr("Defect mesh"));
                vis->freezeInitialParameterValues({SHADOW_PROPERTY_FIELD(SurfaceMeshVis::showCap), SHADOW_PROPERTY_FIELD(SurfaceMeshVis::smoothShading), SHADOW_PROPERTY_FIELD(SurfaceMeshVis::reverseOrientation)});
            }
            SurfaceMeshBuilder meshBuilder(defectSurface);
            meshBuilder.clearMesh();
            // Read defect mesh vertices.
            int numDefectMeshVertices;
            if(sscanf(stream.line(), "DEFECT_MESH_VERTICES %i", &numDefectMeshVertices) != 1 || numDefectMeshVertices < 0)
                throw Exception(tr("Failed to parse file. Invalid number of defect mesh vertices in line %1.").arg(stream.lineNumber()));
            progress.setText(tr("Reading defect surface"));
            progress.setMaximum(numDefectMeshVertices);
            meshBuilder.createVertices(numDefectMeshVertices);
            BufferWriteAccess<Point3, access_mode::read_write> vertexPositions(meshBuilder.mutableVertexProperty(SurfaceMeshVertices::PositionProperty));
            for(int index = 0; index < numDefectMeshVertices; index++) {
                progress.setValueIntermittent(index);
                Point3 p;
                if(sscanf(stream.readLine(), FLOATTYPE_SCANF_STRING " " FLOATTYPE_SCANF_STRING " " FLOATTYPE_SCANF_STRING, &p.x(), &p.y(), &p.z()) != 3)
                    throw Exception(tr("Failed to parse file. Invalid point in line %1.").arg(stream.lineNumber()));
                vertexPositions[index] = p;
            }
        }
        else if(stream.lineStartsWith("DEFECT_MESH_FACETS ") && defectSurface) {
            SurfaceMeshBuilder meshBuilder(defectSurface);
            // Read defect mesh facets.
            int numDefectMeshFacets;
            if(sscanf(stream.line(), "DEFECT_MESH_FACETS %i", &numDefectMeshFacets) != 1 || numDefectMeshFacets < 0)
                throw Exception(tr("Failed to parse file. Invalid number of defect mesh facets in line %1.").arg(stream.lineNumber()));
            progress.setMaximum(numDefectMeshFacets * 2);
            SurfaceMeshBuilder::FaceGrower faceGrower(meshBuilder);
            for(int index = 0; index < numDefectMeshFacets; index++) {
                progress.setValueIntermittent(index);
                int v[3];
                if(sscanf(stream.readLine(), "%i %i %i", &v[0], &v[1], &v[2]) != 3
                        || v[0] < 0 || v[0] >= meshBuilder.vertexCount()
                        || v[1] < 0 || v[1] >= meshBuilder.vertexCount()
                        || v[2] < 0 || v[2] >= meshBuilder.vertexCount())
                    throw Exception(tr("Failed to parse file. Invalid triangle facet in line %1.").arg(stream.lineNumber()));
                faceGrower.createFace({v[0], v[1], v[2]});
            }

            // Read facet adjacency information.
            for(int index = 0; index < numDefectMeshFacets; index++) {
                progress.setValueIntermittent(index + numDefectMeshFacets);
                int v[3];
                if(sscanf(stream.readLine(), "%i %i %i", &v[0], &v[1], &v[2]) != 3)
                    throw Exception(tr("Failed to parse file. Invalid triangle adjacency info in line %1.").arg(stream.lineNumber()));
                SurfaceMesh::edge_index edge = meshBuilder.firstFaceEdge(index);
                for(int i = 0; i < 3; i++, edge = meshBuilder.nextFaceEdge(edge)) {
                    if(meshBuilder.hasOppositeEdge(edge)) continue;
                    SurfaceMesh::edge_index oppositeEdge = meshBuilder.findEdge(v[i], meshBuilder.vertex2(edge), meshBuilder.vertex1(edge));
                    if(oppositeEdge == SurfaceMesh::InvalidIndex)
                        throw Exception(tr("Failed to parse file. Invalid triangle adjacency info in line %1.").arg(stream.lineNumber()));
                    meshBuilder.linkOppositeEdges(edge, oppositeEdge);
                }
            }
        }
        else if(stream.lineStartsWith("METADATA SIMULATION_TIMESTEP ")) {
            int timestep;
            if(sscanf(stream.line(), "METADATA SIMULATION_TIMESTEP %i", &timestep) != 1)
                throw Exception(tr("CA file parsing error. Invalid timestep number (line %1):\n%2").arg(stream.lineNumber()).arg(stream.lineString()));
            state().setAttribute(QStringLiteral("Timestep"), QVariant::fromValue(timestep), pipelineNode());
        }
        else if(stream.lineStartsWith("METADATA ")) {
            // Ignore. This is for future use.
        }
        else if(stream.lineStartsWith("CA_FILE_VERSION ")) {
            // Beginning of next frame.
            signalAdditionalFrames();
            break;
        }
        else if(stream.line()[0] != '\0') {
            throw Exception(tr("Failed to parse file. Invalid keyword in line %1: %2").arg(stream.lineNumber()).arg(stream.lineString()));
        }
    }

    simulationCell()->setCellMatrix(cell);
    simulationCell()->setPbcFlags(pbcFlags[0], pbcFlags[1], pbcFlags[2]);
    if(defectSurface)
        defectSurface->setDomain(simulationCell());

    std::vector<size_t> structureCounts;
    if(clusterGraph) {
        // Count how many atoms of each structure type exist by summing the cluster atom counts.
        for(const Cluster* cluster : clusterGraph->clusters()) {
            if(cluster->structure < 0) continue;
            if(cluster->structure >= structureCounts.size())
                structureCounts.resize(cluster->structure + 1, 0);
            structureCounts[cluster->structure] += cluster->atomCount;
        }
    }

    // Insert dislocations.
    if(dislocations) {
        dislocations->setDomain(simulationCell());

        // Update structure catalog.
        for(int i = 0; i < patterns.size(); i++) {
            if(dislocations->structureByName(patterns[i].longName))
                continue;

            DataOORef<MicrostructurePhase> pattern = DataOORef<MicrostructurePhase>::create();
            pattern->setColor(patterns[i].color);
            pattern->setShortName(patterns[i].shortName);
            pattern->setLongName(patterns[i].longName);
            pattern->setDimensionality(patterns[i].type);
            pattern->setNumericId(patterns[i].id);
            pattern->setCrystalSymmetryClass(patterns[i].symmetryType);
            pattern->freezeInitialParameterValues({SHADOW_PROPERTY_FIELD(ElementType::name), SHADOW_PROPERTY_FIELD(ElementType::color), SHADOW_PROPERTY_FIELD(MicrostructurePhase::shortName), SHADOW_PROPERTY_FIELD(MicrostructurePhase::dimensionality), SHADOW_PROPERTY_FIELD(MicrostructurePhase::crystalSymmetryClass)});
            dislocations->addCrystalStructure(pattern);

            // Add Burgers vector families.
            for(int j = 0; j < patterns[i].burgersVectorFamilies.size(); j++) {
                pattern->createBurgersVectorFamily(
                    patterns[i].burgersVectorFamilies[j].id,
                    patterns[i].burgersVectorFamilies[j].name,
                    patterns[i].burgersVectorFamilies[j].burgersVector,
                    patterns[i].burgersVectorFamilies[j].color);
            }

            // Make sure there always is a default family.
            if(pattern->burgersVectorFamilies().empty())
                pattern->createBurgersVectorFamily();
        }

        // Determine the main crystal structure of the system, which has the most atoms.
        const MicrostructurePhase* mainStructure = nullptr;
        if(!structureCounts.empty()) {
            int maxStructure = std::distance(structureCounts.begin(), std::max_element(structureCounts.begin(), structureCounts.end()));
            mainStructure = dislocations->structureById(maxStructure);
        }

        // Compute dislocation line statistics.
        DislocationAnalysisEngine::generateDislocationStatistics(pipelineNode(), state(), dislocations, true, mainStructure);
    }

    state().setStatus(tr("Number of dislocations: %1").arg(numDislocationSegments));

    // Call base implementation to finalize the loaded data.
    ParticleImporter::FrameLoader::loadFile();
}

}   // End of namespace
