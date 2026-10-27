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


#include <ovito/particles/Particles.h>
#include <ovito/particles/modifier/analysis/bond_order/BondOrderModifier.h>
#include <ovito/particles/objects/Particles.h>
#include <ovito/particles/objects/Bonds.h>
#include <ovito/stdobj/properties/ElementType.h>
#include <ovito/core/app/StandaloneApplication.h>
#include <ovito/core/dataset/io/FileImporter.h>
#include <ovito/core/dataset/io/FileSource.h>
#include <ovito/core/dataset/io/FileSourceImporter.h>
#include <ovito/core/dataset/pipeline/ModificationNode.h>
#include <ovito/core/dataset/pipeline/PipelineEvaluationRequest.h>
#include <ovito/core/dataset/scene/Pipeline.h>
#include <ovito/core/utilities/Exception.h>
#include "BondOrderSelfTestService.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(BondOrderSelfTestService);

namespace {

using BondList = std::vector<std::pair<size_t, size_t>>;

/// Output of OVITO Pro 3.16.1 (fragment size limit 5) for molecules with explicit hydrogens. Fields: elements,
/// bonds (index pairs), penalty score, integer bond orders, fractional bond orders, particle groups, bond groups.
struct ReferenceMolecule {
    const char* name;
    const char* elements;
    const char* bonds;
    int64_t score;
    const char* orders;
    const char* fractionalOrders;
    const char* particleGroups;
    const char* bondGroups;
};

const ReferenceMolecule referenceMolecules[] = {
    {"methane", "C H H H H", "0 1 0 2 0 3 0 4", 0,
     "1 1 1 1",
     "1 1 1 1",
     "0 0 0 0 0",
     "0 0 0 0"},
    {"ethane", "C C H H H H H H", "0 1 0 2 0 3 0 4 1 5 1 6 1 7", 0,
     "1 1 1 1 1 1 1",
     "1 1 1 1 1 1 1",
     "0 0 0 0 0 0 0 0",
     "0 0 0 0 0 0 0"},
    {"ethene", "C C H H H H", "0 1 0 2 0 3 1 4 1 5", 0,
     "2 1 1 1 1",
     "2 1 1 1 1",
     "0 0 0 0 0 0",
     "0 0 0 0 0"},
    {"ethyne", "C C H H", "0 1 0 2 1 3", 0,
     "3 1 1",
     "3 1 1",
     "0 0 0 0",
     "0 0 0"},
    {"benzene", "C C C C C C H H H H H H", "0 1 1 2 2 3 3 4 4 5 5 0 0 6 1 7 2 8 3 9 4 10 5 11", 0,
     "2 1 2 1 2 1 1 1 1 1 1 1",
     "1.5 1.5 1.5 1.5 1.5 1.5 1 1 1 1 1 1",
     "2 2 2 2 2 2 0 0 0 0 0 0",
     "2 2 2 2 2 2 0 0 0 0 0 0"},
    {"naphthalene", "C C C C C C C C C C H H H H H H H H", "0 1 1 2 2 3 3 4 4 9 9 0 4 5 5 6 6 7 7 8 8 9 0 10 1 11 2 12 3 13 5 14 6 15 7 16 8 17", 0,
     "2 1 2 1 1 1 2 1 2 1 2 1 1 1 1 1 1 1 1",
     "1.5 1.5 1.5 1.5 1.5 1.5 1.5 1.5 1.5 1.5 1.5 1 1 1 1 1 1 1 1",
     "2 2 2 2 2 2 2 2 2 2 0 0 0 0 0 0 0 0",
     "2 2 2 2 2 2 2 2 2 2 2 0 0 0 0 0 0 0 0"},
    {"pyridine", "N C C C C C H H H H H", "0 1 1 2 2 3 3 4 4 5 5 0 1 6 2 7 3 8 4 9 5 10", 0,
     "2 1 2 1 2 1 1 1 1 1 1",
     "1.5 1.5 1.5 1.5 1.5 1.5 1 1 1 1 1",
     "2 2 2 2 2 2 0 0 0 0 0",
     "2 2 2 2 2 2 0 0 0 0 0"},
    {"pyrrole", "N C C C C H H H H H", "0 1 1 2 2 3 3 4 4 0 0 5 1 6 2 7 3 8 4 9", 0,
     "1 2 1 2 1 1 1 1 1 1",
     "1.5 1.5 1.5 1.5 1.5 1 1 1 1 1",
     "2 2 2 2 2 0 0 0 0 0",
     "2 2 2 2 2 0 0 0 0 0"},
    {"furan", "O C C C C H H H H", "0 1 1 2 2 3 3 4 4 0 1 5 2 6 3 7 4 8", 0,
     "1 2 1 2 1 1 1 1 1",
     "1.5 1.5 1.5 1.5 1.5 1 1 1 1",
     "2 2 2 2 2 0 0 0 0",
     "2 2 2 2 2 0 0 0 0"},
    {"thiophene", "S C C C C H H H H", "0 1 1 2 2 3 3 4 4 0 1 5 2 6 3 7 4 8", 0,
     "1 2 1 2 1 1 1 1 1",
     "1.5 1.5 1.5 1.5 1.5 1 1 1 1",
     "2 2 2 2 2 0 0 0 0",
     "2 2 2 2 2 0 0 0 0"},
    {"cyclopentadiene", "C C C C C H H H H H H", "0 1 1 2 2 3 3 4 4 0 0 5 0 6 1 7 2 8 3 9 4 10", 0,
     "1 2 1 2 1 1 1 1 1 1 1",
     "1 2 1 2 1 1 1 1 1 1 1",
     "0 0 0 0 0 0 0 0 0 0 0",
     "0 0 0 0 0 0 0 0 0 0 0"},
    {"cyclohexene", "C C C C C C H H H H H H H H H H", "0 1 1 2 2 3 3 4 4 5 5 0 0 6 1 7 2 8 2 9 3 10 3 11 4 12 4 13 5 14 5 15", 0,
     "2 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1",
     "2 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1",
     "0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0",
     "0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0"},
    {"water", "O H H", "0 1 0 2", 0,
     "1 1",
     "1 1",
     "0 0 0",
     "0 0"},
    {"ammonia", "N H H H", "0 1 0 2 0 3", 0,
     "1 1 1",
     "1 1 1",
     "0 0 0 0",
     "0 0 0"},
    {"methanol", "C O H H H H", "0 1 0 2 0 3 0 4 1 5", 0,
     "1 1 1 1 1",
     "1 1 1 1 1",
     "0 0 0 0 0 0",
     "0 0 0 0 0"},
    {"formaldehyde", "C O H H", "0 1 0 2 0 3", 0,
     "2 1 1",
     "2 1 1",
     "0 0 0 0",
     "0 0 0"},
    {"acetic_acid", "C C O O H H H H", "0 1 1 2 1 3 0 4 0 5 0 6 3 7", 0,
     "1 2 1 1 1 1 1",
     "1 2 1 1 1 1 1",
     "0 0 0 0 0 0 0 0",
     "0 0 0 0 0 0 0"},
    {"acetate", "C C O O H H H", "0 1 1 2 1 3 0 4 0 5 0 6", 2,
     "2 1 1 1 1 1",
     "1.5 1.5 1.5 1 1 1",
     "0 3 3 3 0 0 0",
     "0 0 0 0 0 0"},
    {"acetamide", "C C O N H H H H H", "0 1 1 2 1 3 0 4 0 5 0 6 3 7 3 8", 0,
     "1 2 1 1 1 1 1 1",
     "1 1.41 1.41 1 1 1 1 1",
     "0 1 1 1 0 0 0 0 0",
     "0 1 1 0 0 0 0 0"},
    {"nmethylacetamide", "C C O N C H H H H H H H", "0 1 1 2 1 3 3 4 0 5 0 6 0 7 3 8 4 9 4 10 4 11", 0,
     "1 2 1 1 1 1 1 1 1 1 1",
     "1 1.41 1.41 1 1 1 1 1 1 1 1",
     "0 1 1 1 0 0 0 0 0 0 0 0",
     "0 1 1 0 0 0 0 0 0 0 0"},
    {"methylamine", "C N H H H H H", "0 1 0 2 0 3 0 4 1 5 1 6", 0,
     "1 1 1 1 1 1",
     "1 1 1 1 1 1",
     "0 0 0 0 0 0 0",
     "0 0 0 0 0 0"},
    {"trimethylamine", "N C C C H H H H H H H H H", "0 1 0 2 0 3 1 4 1 5 1 6 2 7 2 8 2 9 3 10 3 11 3 12", 0,
     "1 1 1 1 1 1 1 1 1 1 1 1",
     "1 1 1 1 1 1 1 1 1 1 1 1",
     "0 0 0 0 0 0 0 0 0 0 0 0 0",
     "0 0 0 0 0 0 0 0 0 0 0 0"},
    {"nitromethane", "C N O O H H H", "0 1 1 2 1 3 0 4 0 5 0 6", 3,
     "2 1 1 1 1 1",
     "1.5 1.5 1.5 1 1 1",
     "0 4 4 4 0 0 0",
     "0 0 0 0 0 0"},
    {"nitrobenzene", "C C C C C C N O O H H H H H", "0 1 1 2 2 3 3 4 4 5 5 0 0 6 6 7 6 8 1 9 2 10 3 11 4 12 5 13", 3,
     "2 1 2 1 2 1 2 1 1 1 1 1 1 1",
     "1.5 1.5 1.5 1.5 1.5 1.5 1.5 1.5 1.5 1 1 1 1 1",
     "2 2 2 2 2 2 4 4 4 0 0 0 0 0",
     "2 2 2 2 2 2 0 0 0 0 0 0 0 0"},
    {"aniline", "C C C C C C N H H H H H H H", "0 1 1 2 2 3 3 4 4 5 5 0 0 6 1 7 2 8 3 9 4 10 5 11 6 12 6 13", 0,
     "2 1 2 1 2 1 1 1 1 1 1 1 1 1",
     "1.5 1.5 1.5 1.5 1.5 1.5 1 1 1 1 1 1 1 1",
     "2 2 2 2 2 2 5 0 0 0 0 0 5 5",
     "2 2 2 2 2 2 0 0 0 0 0 0 5 5"},
    {"benzoic_acid", "C C C C C C C O O H H H H H H", "0 1 1 2 2 3 3 4 4 5 5 0 0 6 6 7 6 8 1 9 2 10 3 11 4 12 5 13 8 14", 0,
     "2 1 2 1 2 1 1 2 1 1 1 1 1 1 1",
     "1.5 1.5 1.5 1.5 1.5 1.5 1 2 1 1 1 1 1 1 1",
     "2 2 2 2 2 2 0 0 0 0 0 0 0 0 0",
     "2 2 2 2 2 2 0 0 0 0 0 0 0 0 0"},
    {"acetonitrile", "C C N H H H", "0 1 1 2 0 3 0 4 0 5", 0,
     "1 3 1 1 1",
     "1 3 1 1 1",
     "0 0 0 0 0 0",
     "0 0 0 0 0"},
    {"co2", "C O O", "0 1 0 2", 0,
     "2 2",
     "2 2",
     "0 0 0",
     "0 0"},
    {"hcn", "C N H", "0 1 0 2", 0,
     "3 1",
     "3 1",
     "0 0 0",
     "0 0"},
    {"dmso", "C S O C H H H H H H", "0 1 1 2 1 3 0 4 0 5 0 6 3 7 3 8 3 9", 0,
     "1 2 1 1 1 1 1 1 1",
     "1 2 1 1 1 1 1 1 1",
     "0 0 0 0 0 0 0 0 0 0",
     "0 0 0 0 0 0 0 0 0"},
    {"sulfuric", "S O O O O H H", "0 1 0 2 0 3 0 4 3 5 4 6", 0,
     "2 2 1 1 1 1",
     "2 2 1 1 1 1",
     "0 0 0 0 0 0 0",
     "0 0 0 0 0 0"},
    {"phosphoric", "P O O O O H H H", "0 1 0 2 0 3 0 4 2 5 3 6 4 7", 0,
     "2 1 1 1 1 1 1",
     "2 1 1 1 1 1 1",
     "0 0 0 0 0 0 0 0",
     "0 0 0 0 0 0 0"},
    {"imidazole", "N C N C C H H H H", "0 1 1 2 2 3 3 4 4 0 0 5 1 6 3 7 4 8", 0,
     "1 2 1 2 1 1 1 1 1",
     "1.5 1.5 1.5 1.5 1.5 1 1 1 1",
     "2 2 2 2 2 0 0 0 0",
     "2 2 2 2 2 0 0 0 0"},
    {"indole", "N C C C C C C C C H H H H H H H", "0 1 1 2 2 3 3 8 8 0 3 4 4 5 5 6 6 7 7 8 0 9 1 10 2 11 4 12 5 13 6 14 7 15", 0,
     "1 2 1 1 1 2 1 2 1 2 1 1 1 1 1 1 1",
     "1.5 1.5 1.5 1.5 1.5 1.5 1.5 1.5 1.5 1.5 1 1 1 1 1 1 1",
     "2 2 2 2 2 2 2 2 2 0 0 0 0 0 0 0",
     "2 2 2 2 2 2 2 2 2 2 0 0 0 0 0 0 0"},
    {"pyridone", "N C C C C C O H H H H H", "0 1 1 2 2 3 3 4 4 5 5 0 1 6 0 7 2 8 3 9 4 10 5 11", 0,
     "1 1 2 1 2 1 2 1 1 1 1 1",
     "1.5 1.5 1.5 1.5 1.5 1.5 2 1 1 1 1 1",
     "2 2 2 2 2 2 1 0 0 0 0 0",
     "2 2 2 2 2 2 0 0 0 0 0 0"},
    {"cyclooctatetraene", "C C C C C C C C H H H H H H H H", "0 1 1 2 2 3 3 4 4 5 5 6 6 7 7 0 0 8 1 9 2 10 3 11 4 12 5 13 6 14 7 15", 0,
     "2 1 2 1 2 1 2 1 1 1 1 1 1 1 1 1",
     "2 1 2 1 2 1 2 1 1 1 1 1 1 1 1 1",
     "0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0",
     "0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0"},
    {"butadiene", "C C C C H H H H H H", "0 1 1 2 2 3 0 4 0 5 1 6 2 7 3 8 3 9", 0,
     "2 1 2 1 1 1 1 1 1",
     "2 1 2 1 1 1 1 1 1",
     "0 0 0 0 0 0 0 0 0 0",
     "0 0 0 0 0 0 0 0 0"},
    {"glycine_zw", "N C C O O H H H H H", "0 1 1 2 2 3 2 4 0 5 0 6 0 7 1 8 1 9", 2,
     "1 2 1 1 1 1 1 1 1",
     "1 1.5 1.5 1.5 1 1 1 1 1",
     "0 0 3 3 3 0 0 0 0 0",
     "0 0 0 0 0 0 0 0 0"},
    {"urea", "N C O N H H H H", "0 1 1 2 1 3 0 4 0 5 3 6 3 7", 0,
     "1 2 1 1 1 1 1",
     "1.41 1.41 1.41 1 1 1 1",
     "1 1 1 1 0 0 0 0",
     "1 1 1 0 0 0 0"},
    {"methyl_radical", "C H H H", "0 1 0 2 0 3", 32,
     "1 1 1",
     "1 1 1",
     "0 0 0 0",
     "0 0 0"},
    {"o2", "O O", "0 1", 0,
     "2",
     "2",
     "0 0",
     "0"},
    {"n2", "N N", "0 1", 0,
     "3",
     "3",
     "0 0",
     "0"},
    {"chlorobenzene", "C C C C C C Cl H H H H H", "0 1 1 2 2 3 3 4 4 5 5 0 0 6 1 7 2 8 3 9 4 10 5 11", 0,
     "2 1 2 1 2 1 1 1 1 1 1 1",
     "1.5 1.5 1.5 1.5 1.5 1.5 1 1 1 1 1 1",
     "2 2 2 2 2 2 0 0 0 0 0 0",
     "2 2 2 2 2 2 0 0 0 0 0 0"},
    {"azulene", "C C C C C C C C C C H H H H H H H H", "0 1 1 2 2 3 3 9 9 0 3 4 4 5 5 6 6 7 7 8 8 9 0 10 1 11 2 12 4 13 5 14 6 15 7 16 8 17", 0,
     "2 1 2 1 1 1 2 1 2 1 2 1 1 1 1 1 1 1 1",
     "2 1 2 1 1 1 2 1 2 1 2 1 1 1 1 1 1 1 1",
     "0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0",
     "0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0"},
    {"biphenyl", "C C C C C C C C C C C C H H H H H H H H H H", "0 1 1 2 2 3 3 4 4 5 5 0 6 7 7 8 8 9 9 10 10 11 11 6 0 6 1 12 2 13 3 14 4 15 5 16 7 17 8 18 9 19 10 20 11 21", 0,
     "2 1 2 1 2 1 2 1 2 1 2 1 1 1 1 1 1 1 1 1 1 1 1",
     "1.5 1.5 1.5 1.5 1.5 1.5 1.5 1.5 1.5 1.5 1.5 1.5 1 1 1 1 1 1 1 1 1 1 1",
     "2 2 2 2 2 2 2 2 2 2 2 2 0 0 0 0 0 0 0 0 0 0",
     "2 2 2 2 2 2 2 2 2 2 2 2 2 0 0 0 0 0 0 0 0 0 0"},
    {"pyridinium", "N C C C C C H H H H H H", "0 1 1 2 2 3 3 4 4 5 5 0 0 6 1 7 2 8 3 9 4 10 5 11", 1,
     "2 1 2 1 2 1 1 1 1 1 1 1",
     "1.5 1.5 1.5 1.5 1.5 1.5 1 1 1 1 1 1",
     "2 2 2 2 2 2 0 0 0 0 0 0",
     "2 2 2 2 2 2 0 0 0 0 0 0"},
    {"NNdimethylaniline", "C C C C C C N C C H H H H H H H H H H H", "0 1 1 2 2 3 3 4 4 5 5 0 0 6 6 7 6 8 1 9 2 10 3 11 4 12 5 13 7 14 7 15 7 16 8 17 8 18 8 19", 0,
     "2 1 2 1 2 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1",
     "1.5 1.5 1.5 1.5 1.5 1.5 1 1 1 1 1 1 1 1 1 1 1 1 1 1",
     "2 2 2 2 2 2 0 0 0 0 0 0 0 0 0 0 0 0 0 0",
     "2 2 2 2 2 2 0 0 0 0 0 0 0 0 0 0 0 0 0 0"},
    {"diphenylamine", "C C C C C C N C C C C C C H H H H H H H H H H H", "0 1 1 2 2 3 3 4 4 5 5 0 7 8 8 9 9 10 10 11 11 12 12 7 0 6 6 7 1 13 2 14 3 15 4 16 5 17 6 18 8 19 9 20 10 21 11 22 12 23", 0,
     "2 1 2 1 2 1 2 1 2 1 2 1 1 1 1 1 1 1 1 1 1 1 1 1 1",
     "1.5 1.5 1.5 1.5 1.5 1.5 1.5 1.5 1.5 1.5 1.5 1.5 1 1 1 1 1 1 1 1 1 1 1 1 1",
     "2 2 2 2 2 2 0 2 2 2 2 2 2 0 0 0 0 0 0 0 0 0 0 0",
     "2 2 2 2 2 2 2 2 2 2 2 2 0 0 0 0 0 0 0 0 0 0 0 0 0"},
    {"acetanilide", "C C C C C C N C O C H H H H H H H H H", "0 1 1 2 2 3 3 4 4 5 5 0 0 6 6 7 7 8 7 9 1 10 2 11 3 12 4 13 5 14 6 15 9 16 9 17 9 18", 0,
     "2 1 2 1 2 1 1 1 2 1 1 1 1 1 1 1 1 1 1",
     "1.5 1.5 1.5 1.5 1.5 1.5 1 1.41 1.41 1 1 1 1 1 1 1 1 1 1",
     "2 2 2 2 2 2 1 1 1 0 0 0 0 0 0 0 0 0 0",
     "2 2 2 2 2 2 0 1 1 0 0 0 0 0 0 0 0 0 0"},
    {"thioacetamide", "C C S N H H H H H", "0 1 1 2 1 3 0 4 0 5 0 6 3 7 3 8", 0,
     "1 2 1 1 1 1 1 1",
     "1 2 1 1 1 1 1 1",
     "0 0 0 5 0 0 0 5 5",
     "0 0 0 0 0 0 5 5"},
    {"carbamate", "C O C O N H H H H H", "0 1 1 2 2 3 2 4 0 5 0 6 0 7 4 8 4 9", 0,
     "1 1 2 1 1 1 1 1 1",
     "1 1 1.41 1.41 1 1 1 1 1",
     "0 0 1 1 1 0 0 0 0 0",
     "0 0 1 1 0 0 0 0 0"},
    {"nitric", "N O O O H", "0 1 0 2 0 3 3 4", 3,
     "2 1 1 1",
     "1.33 1.33 1.33 1",
     "4 4 4 0 0",
     "0 0 0 0"},
    {"nitrate", "N O O O", "0 1 0 2 0 3", 5,
     "2 1 1",
     "1.33 1.33 1.33",
     "4 4 4 4",
     "0 0 0"},
    {"HNO2", "N O O H", "0 1 0 2 0 3", 3,
     "2 1 1",
     "1.5 1.5 1.5",
     "4 4 4 0",
     "0 0 0"},
    {"methylformate", "C O O C H H H H", "0 1 0 2 2 3 0 4 3 5 3 6 3 7", 0,
     "2 1 1 1 1 1 1",
     "2 1 1 1 1 1 1",
     "0 0 0 0 0 0 0 0",
     "0 0 0 0 0 0 0"},
    {"phenol", "C C C C C C O H H H H H H", "0 1 1 2 2 3 3 4 4 5 5 0 0 6 1 7 2 8 3 9 4 10 5 11 6 12", 0,
     "2 1 2 1 2 1 1 1 1 1 1 1 1",
     "1.5 1.5 1.5 1.5 1.5 1.5 1 1 1 1 1 1 1",
     "2 2 2 2 2 2 0 0 0 0 0 0 0",
     "2 2 2 2 2 2 0 0 0 0 0 0 0"},
    {"pyrimidine", "N C N C C C H H H H", "0 1 1 2 2 3 3 4 4 5 5 0 1 6 3 7 4 8 5 9", 0,
     "2 1 2 1 2 1 1 1 1 1",
     "1.5 1.5 1.5 1.5 1.5 1.5 1 1 1 1",
     "2 2 2 2 2 2 0 0 0 0",
     "2 2 2 2 2 2 0 0 0 0"},
    {"aminopyridine", "N C C C C C N H H H H H H", "0 1 1 2 2 3 3 4 4 5 5 0 1 6 2 7 3 8 4 9 5 10 6 11 6 12", 0,
     "2 1 2 1 2 1 1 1 1 1 1 1 1",
     "1.5 1.5 1.5 1.5 1.5 1.5 1 1 1 1 1 1 1",
     "2 2 2 2 2 2 5 0 0 0 0 5 5",
     "2 2 2 2 2 2 0 0 0 0 0 5 5"},
    {"enamine", "C C N H H H H H", "0 1 1 2 0 3 0 4 1 5 2 6 2 7", 0,
     "2 1 1 1 1 1 1",
     "2 1 1 1 1 1 1",
     "0 0 5 0 0 0 5 5",
     "0 0 0 0 0 5 5"},
    {"formamide", "C O N H H H", "0 1 0 2 0 3 2 4 2 5", 0,
     "2 1 1 1 1",
     "1.41 1.41 1 1 1",
     "1 1 1 0 0 0",
     "1 1 0 0 0"},
    {"DMF", "C O N C C H H H H H H H", "0 1 0 2 2 3 2 4 0 5 3 6 3 7 3 8 4 9 4 10 4 11", 0,
     "2 1 1 1 1 1 1 1 1 1 1",
     "1.41 1.41 1 1 1 1 1 1 1 1 1",
     "1 1 1 0 0 0 0 0 0 0 0 0",
     "1 1 0 0 0 0 0 0 0 0 0"},
    {"benzoate", "C C C C C C C O O H H H H H", "0 1 1 2 2 3 3 4 4 5 5 0 0 6 6 7 6 8 1 9 2 10 3 11 4 12 5 13", 2,
     "2 1 2 1 2 1 2 1 1 1 1 1 1 1",
     "1.5 1.5 1.5 1.5 1.5 1.5 1.5 1.5 1.5 1 1 1 1 1",
     "2 2 2 2 2 2 3 3 3 0 0 0 0 0",
     "2 2 2 2 2 2 0 0 0 0 0 0 0 0"},
    {"cyclohexadienone", "C C C C C C O H H H H H H", "0 1 1 2 2 3 3 4 4 5 5 0 0 6 1 7 2 8 3 9 3 10 4 11 5 12", 0,
     "1 2 1 1 2 1 2 1 1 1 1 1 1",
     "1 2 1 1 2 1 2 1 1 1 1 1 1",
     "0 0 0 0 0 0 0 0 0 0 0 0 0",
     "0 0 0 0 0 0 0 0 0 0 0 0 0"},
    {"tropylium_like_7", "C C C C C C C H H H H H H H", "0 1 1 2 2 3 3 4 4 5 5 6 6 0 0 7 1 8 2 9 3 10 4 11 5 12 6 13", 32,
     "2 1 2 1 2 1 1 1 1 1 1 1 1 1",
     "2 1 2 1 2 1 1 1 1 1 1 1 1 1",
     "0 0 0 0 0 0 0 0 0 0 0 0 0 0",
     "0 0 0 0 0 0 0 0 0 0 0 0 0 0"},
    {"anthracene", "C C C C C C C C C C C C C C H H H H H H H H H H", "0 1 1 2 2 3 3 4 4 13 13 0 4 5 5 6 6 11 11 12 12 13 6 7 7 8 8 9 9 10 10 11 0 14 1 15 2 16 3 17 5 18 7 19 8 20 9 21 10 22 12 23", 0,
     "2 1 2 1 1 1 2 1 1 1 2 2 1 2 1 2 1 1 1 1 1 1 1 1 1 1",
     "1.5 1.5 1.5 1.5 1.5 1.5 1.5 1.5 1.5 1.5 1.5 1.5 1.5 1.5 1.5 1.5 1 1 1 1 1 1 1 1 1 1",
     "2 2 2 2 2 2 2 2 2 2 2 2 2 2 0 0 0 0 0 0 0 0 0 0",
     "2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 0 0 0 0 0 0 0 0 0 0"},
    {"purine", "N C N C C N C N C H H H H", "0 1 1 2 2 3 3 4 4 8 8 0 4 5 5 6 6 7 7 3 1 9 6 10 7 11 8 12", 0,
     "2 1 2 1 2 1 1 2 1 1 1 1 1 1",
     "1.5 1.5 1.5 1.5 1.5 1.5 1.5 1.5 1.5 1.5 1 1 1 1",
     "2 2 2 2 2 2 2 2 2 0 0 0 0",
     "2 2 2 2 2 2 2 2 2 2 0 0 0 0"},
    {"cp_anion(C5H5)", "C C C C C H H H H H", "0 1 1 2 2 3 3 4 4 0 0 5 1 6 2 7 3 8 4 9", 32,
     "2 1 2 1 1 1 1 1 1 1",
     "2 1 2 1 1 1 1 1 1 1",
     "0 0 0 0 0 0 0 0 0 0",
     "0 0 0 0 0 0 0 0 0 0"},
    {"borole", "B C C C C H H H H H", "0 1 1 2 2 3 3 4 4 0 0 5 1 6 2 7 3 8 4 9", 0,
     "2 1 2 1 2 1 1 1 1 1",
     "2 1 2 1 2 1 1 1 1 1",
     "0 0 0 0 0 0 0 0 0 0",
     "0 0 0 0 0 0 0 0 0 0"},
    {"silole", "Si C C C C H H H H H H", "0 1 1 2 2 3 3 4 4 0 0 5 0 6 1 7 2 8 3 9 4 10", 0,
     "1 2 1 2 1 1 1 1 1 1 1",
     "1 2 1 2 1 1 1 1 1 1 1",
     "0 0 0 0 0 0 0 0 0 0 0",
     "0 0 0 0 0 0 0 0 0 0 0"},
    {"phosphole", "P C C C C H H H H H", "0 1 1 2 2 3 3 4 4 0 0 5 1 6 2 7 3 8 4 9", 0,
     "1 2 1 2 1 1 1 1 1 1",
     "1 2 1 2 1 1 1 1 1 1",
     "0 0 0 0 0 0 0 0 0 0",
     "0 0 0 0 0 0 0 0 0 0"},
    {"selenophene", "Se C C C C H H H H", "0 1 1 2 2 3 3 4 4 0 1 5 2 6 3 7 4 8", 0,
     "2 1 2 1 2 1 1 1 1",
     "2 1 2 1 2 1 1 1 1",
     "0 0 0 0 0 0 0 0 0",
     "0 0 0 0 0 0 0 0 0"},
    {"oxazole", "O C N C C H H H", "0 1 1 2 2 3 3 4 4 0 1 5 3 6 4 7", 0,
     "1 2 1 2 1 1 1 1",
     "1.5 1.5 1.5 1.5 1.5 1 1 1",
     "2 2 2 2 2 0 0 0",
     "2 2 2 2 2 0 0 0"},
    {"thiazole", "S C N C C H H H", "0 1 1 2 2 3 3 4 4 0 1 5 3 6 4 7", 0,
     "1 2 1 2 1 1 1 1",
     "1.5 1.5 1.5 1.5 1.5 1 1 1",
     "2 2 2 2 2 0 0 0",
     "2 2 2 2 2 0 0 0"},
    {"pyridineNoxide", "N C C C C C O H H H H H", "0 1 1 2 2 3 3 4 4 5 5 0 0 6 1 7 2 8 3 9 4 10 5 11", 3,
     "2 1 2 1 2 1 1 1 1 1 1 1",
     "1.5 1.5 1.5 1.5 1.5 1.5 1 1 1 1 1 1",
     "2 2 2 2 2 2 0 0 0 0 0 0",
     "2 2 2 2 2 2 0 0 0 0 0 0"},
    {"pyrylium", "O C C C C C H H H H H", "0 1 1 2 2 3 3 4 4 5 5 0 1 6 2 7 3 8 4 9 5 10", 16,
     "2 1 2 1 2 1 1 1 1 1 1",
     "2 1 2 1 2 1 1 1 1 1 1",
     "0 0 0 0 0 0 0 0 0 0 0",
     "0 0 0 0 0 0 0 0 0 0 0"},
    {"pyrrole_anion", "N C C C C H H H H", "0 1 1 2 2 3 3 4 4 0 1 5 2 6 3 7 4 8", 2,
     "2 1 2 1 2 1 1 1 1",
     "2 1 2 1 2 1 1 1 1",
     "0 0 0 0 0 0 0 0 0",
     "0 0 0 0 0 0 0 0 0"},
    {"4pyridone", "N C C C C C O H H H H H", "0 1 1 2 2 3 3 4 4 5 5 0 3 6 0 7 1 8 2 9 4 10 5 11", 0,
     "1 2 1 1 2 1 2 1 1 1 1 1",
     "1.5 1.5 1.5 1.5 1.5 1.5 2 1 1 1 1 1",
     "2 2 2 2 2 2 0 0 0 0 0 0",
     "2 2 2 2 2 2 0 0 0 0 0 0"},
    {"fulvene", "C C C C C C H H H H H H", "0 1 1 2 2 3 3 4 4 0 0 5 1 6 2 7 3 8 4 9 5 10 5 11", 0,
     "1 2 1 2 1 2 1 1 1 1 1 1",
     "1 2 1 2 1 2 1 1 1 1 1 1",
     "0 0 0 0 0 0 0 0 0 0 0 0",
     "0 0 0 0 0 0 0 0 0 0 0 0"},
    {"uracil", "N C N C C C O O H H H H", "0 1 1 2 2 3 3 4 4 5 5 0 1 6 3 7 0 8 2 9 4 10 5 11", 0,
     "1 1 1 1 2 1 2 2 1 1 1 1",
     "1.5 1.5 1.5 1.5 1.5 1.5 2 2 1 1 1 1",
     "2 2 2 2 2 2 1 1 0 0 0 0",
     "2 2 2 2 2 2 0 0 0 0 0 0"},
    {"benzoquinone", "C C C C C C O O H H H H", "0 1 1 2 2 3 3 4 4 5 5 0 0 6 3 7 1 8 2 9 4 10 5 11", 0,
     "1 2 1 1 2 1 2 2 1 1 1 1",
     "1 2 1 1 2 1 2 2 1 1 1 1",
     "0 0 0 0 0 0 0 0 0 0 0 0",
     "0 0 0 0 0 0 0 0 0 0 0 0"},
    {"triazine", "N C N C N C H H H", "0 1 1 2 2 3 3 4 4 5 5 0 1 6 3 7 5 8", 0,
     "2 1 2 1 2 1 1 1 1",
     "1.5 1.5 1.5 1.5 1.5 1.5 1 1 1",
     "2 2 2 2 2 2 0 0 0",
     "2 2 2 2 2 2 0 0 0"},
    {"tetrazole", "N N N N C H H", "0 1 1 2 2 3 3 4 4 0 0 5 4 6", 0,
     "1 2 1 2 1 1 1",
     "1.5 1.5 1.5 1.5 1.5 1 1",
     "2 2 2 2 2 0 0",
     "2 2 2 2 2 0 0"},
    {"Si6H6", "Si Si Si Si Si Si H H H H H H", "0 1 1 2 2 3 3 4 4 5 5 0 0 6 1 7 2 8 3 9 4 10 5 11", 0,
     "2 1 2 1 2 1 1 1 1 1 1 1",
     "2 1 2 1 2 1 1 1 1 1 1 1",
     "0 0 0 0 0 0 0 0 0 0 0 0",
     "0 0 0 0 0 0 0 0 0 0 0 0"},
    {"pyrroline_sp3N_charged", "N C C C C H H H H H H", "0 1 1 2 2 3 3 4 4 0 0 5 0 6 1 7 2 8 3 9 4 10", 0,
     "1 2 1 2 1 1 1 1 1 1 1",
     "1 2 1 2 1 1 1 1 1 1 1",
     "0 0 0 0 0 0 0 0 0 0 0",
     "0 0 0 0 0 0 0 0 0 0 0"},
    {"pyridinium_methyl", "N C C C C C C H H H H H H H H", "0 1 1 2 2 3 3 4 4 5 5 0 0 6 1 7 2 8 3 9 4 10 5 11 6 12 6 13 6 14", 1,
     "2 1 2 1 2 1 1 1 1 1 1 1 1 1 1",
     "1.5 1.5 1.5 1.5 1.5 1.5 1 1 1 1 1 1 1 1 1",
     "2 2 2 2 2 2 0 0 0 0 0 0 0 0 0",
     "2 2 2 2 2 2 0 0 0 0 0 0 0 0 0"},
    {"cyclobutadiene", "C C C C H H H H", "0 1 1 2 2 3 3 0 0 4 1 5 2 6 3 7", 0,
     "2 1 2 1 1 1 1 1",
     "2 1 2 1 1 1 1 1",
     "0 0 0 0 0 0 0 0",
     "0 0 0 0 0 0 0 0"},
    {"benzene_radical_C6H5", "C C C C C C H H H H H", "0 1 1 2 2 3 3 4 4 5 5 0 1 6 2 7 3 8 4 9 5 10", 32,
     "2 1 2 1 1 2 1 1 1 1 1",
     "2 1 2 1 1 2 1 1 1 1 1",
     "0 0 0 0 0 0 0 0 0 0 0",
     "0 0 0 0 0 0 0 0 0 0 0"},
};

template<typename T>
std::vector<T> parseList(const char* text)
{
    std::vector<T> values;
    for(const QString& token : QString::fromLatin1(text).split(' ', Qt::SkipEmptyParts))
        values.push_back(static_cast<T>(token.toDouble()));
    return values;
}

bool check(const QString& name, bool ok, const QString& detail)
{
    if(ok)
        qInfo().noquote() << "  PASS" << name << ":" << detail;
    else
        qCritical().noquote() << "  FAIL" << name << ":" << detail;
    return ok;
}

template<typename T>
QString join(const std::vector<T>& values)
{
    QStringList parts;
    for(const T& v : values) parts << QString::number(v);
    return parts.join(' ');
}

/// Lowest penalty over all bond order assignments, by enumeration (the same rules for fixed single bonds).
int64_t bruteForceScore(const std::vector<int>& z, const BondList& bonds)
{
    std::vector<int> connectivity(z.size(), 0);
    for(auto [a, b] : bonds) { connectivity[a]++; connectivity[b]++; }
    const std::vector<bool> amideCarbon = BondOrderModifier::amideCarbons(z, bonds);
    std::vector<int> baseValence(connectivity);
    std::vector<size_t> free;
    for(size_t b = 0; b < bonds.size(); b++) {
        auto [i, j] = bonds[b];
        if((amideCarbon[i] && z[j] == 8 && connectivity[j] == 1) || (amideCarbon[j] && z[i] == 8 && connectivity[i] == 1)) {
            baseValence[i]++; baseValence[j]++;     // The amide C=O bond is fixed double.
            continue;
        }
        if(connectivity[i] < BondOrderModifier::maximumConnectivity(z[i]) && connectivity[j] < BondOrderModifier::maximumConnectivity(z[j])
                && (BondOrderModifier::hasPenaltyTable(z[i]) || BondOrderModifier::hasPenaltyTable(z[j])))
            free.push_back(b);
    }
    int64_t best = std::numeric_limits<int64_t>::max();
    std::vector<int> excess(free.size(), 0);
    for(;;) {
        std::vector<int> valence(baseValence);
        for(size_t k = 0; k < free.size(); k++) { valence[bonds[free[k]].first] += excess[k]; valence[bonds[free[k]].second] += excess[k]; }
        int64_t score = 0;
        for(size_t i = 0; i < z.size(); i++) score += BondOrderModifier::atomPenalty(z[i], connectivity[i], valence[i], amideCarbon[i]);
        best = std::min(best, score);
        size_t k = 0;
        while(k < free.size() && excess[k] == 2) excess[k++] = 0;
        if(k == free.size()) break;
        excess[k]++;
    }
    return best;
}

/// Writes acetanilide as a LAMMPS data file with type labels (coordinates are irrelevant, only the bonds count).
void writeAcetanilide(const QString& path, const QString& oxygenLabel)
{
    // Heavy atoms: ring C0..C5, N6, carbonyl C7, O8, methyl C9; then hydrogens.
    const QStringList elements = {"C","C","C","C","C","C","N","C","O","C","H","H","H","H","H","H","H","H","H"};
    const std::vector<std::pair<int, int>> bonds = {{0,1},{1,2},{2,3},{3,4},{4,5},{5,0},{0,6},{6,7},{7,8},{7,9},
        {1,10},{2,11},{3,12},{4,13},{5,14},{6,15},{9,16},{9,17},{9,18}};
    const QStringList labels = {"C", "H", "N", oxygenLabel};
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly | QIODevice::Text))
        throw Exception(QStringLiteral("Cannot write test data file %1").arg(path));
    QTextStream out(&file);
    out << "Bond order self-test\n\n" << elements.size() << " atoms\n" << bonds.size() << " bonds\n4 atom types\n1 bond types\n\n"
           "0 40 xlo xhi\n0 40 ylo yhi\n0 40 zlo zhi\n\nAtom Type Labels\n\n";
    for(int t = 0; t < labels.size(); t++) out << t + 1 << " " << labels[t] << "\n";
    out << "\nAtoms # bond\n\n";
    for(int i = 0; i < elements.size(); i++) {
        const QString& e = elements[i];
        const int type = (e == "O") ? 4 : labels.indexOf(e) + 1;
        out << i + 1 << " 1 " << type << " " << 2 + 1.9 * (i % 6) << " " << 2 + 1.9 * (i / 6) << " 20\n";
    }
    out << "\nBonds\n\n";
    for(size_t b = 0; b < bonds.size(); b++) out << b + 1 << " 1 " << bonds[b].first + 1 << " " << bonds[b].second + 1 << "\n";
}

}   // End of anonymous namespace

/******************************************************************************
* Registers the command-line options handled by this service.
******************************************************************************/
void BondOrderSelfTestService::registerCommandLineOptions(QCommandLineParser& cmdLineParser)
{
    cmdLineParser.addOption(QCommandLineOption(QStringList{QStringLiteral("bond-order-selftest")},
        tr("Run the in-process regression test for the Bond order modifier and exit (0 on success, 1 on any failed check).")));
}

/******************************************************************************
* Runs the self-test after the application has entered the main event loop.
******************************************************************************/
void BondOrderSelfTestService::applicationStarted()
{
    if(!StandaloneApplication::instance()->cmdLineParser().isSet(QStringLiteral("bond-order-selftest")))
        return;

    bool ok = false;
    try {
        ok = runTest();
    }
    catch(const Exception& ex) {
        qCritical().noquote() << "Bond order self-test: FAILED with an exception:";
        for(const QString& msg : ex.messages())
            qCritical().noquote() << msg;
        ok = false;
    }
    // In --nogui mode QCoreApplication::exit() has no effect; throwing yields exit code 1.
    if(!ok)
        throw Exception(tr("Bond order self-test failed."));
    QCoreApplication::exit(0);
}

/******************************************************************************
* Runs all checks.
******************************************************************************/
bool BondOrderSelfTestService::runTest()
{
    bool ok = true;

    qInfo().noquote() << "Bond order self-test: reference molecules (OVITO Pro 3.16.1)";
    for(const ReferenceMolecule& ref : referenceMolecules) {
        std::vector<int> z;
        for(const QString& symbol : QString::fromLatin1(ref.elements).split(' '))
            z.push_back(BondOrderModifier::atomicNumber(symbol));
        const std::vector<size_t> flat = parseList<size_t>(ref.bonds);
        BondList bonds;
        for(size_t k = 0; k + 1 < flat.size(); k += 2) bonds.emplace_back(flat[k], flat[k + 1]);
        const BondOrderModifier::Result integer = BondOrderModifier::compute(z, bonds, 5, false);
        const BondOrderModifier::Result fractional = BondOrderModifier::compute(z, bonds, 5, true);
        const std::vector<int> expectedOrders = parseList<int>(ref.orders);
        const std::vector<double> expectedFractional = parseList<double>(ref.fractionalOrders);
        const std::vector<int32_t> expectedParticleGroups = parseList<int32_t>(ref.particleGroups);
        const std::vector<int32_t> expectedBondGroups = parseList<int32_t>(ref.bondGroups);
        const QString name = QString::fromLatin1(ref.name);

        bool same = integer.penaltyScore == ref.score && integer.particleGroups == expectedParticleGroups
            && fractional.penaltyScore == ref.score && fractional.integerOrders == integer.integerOrders;
        QString detail = QStringLiteral("score %1 (Pro %2), groups [%3]").arg(integer.penaltyScore).arg(ref.score).arg(join(integer.particleGroups));
        const bool deviation = std::ranges::any_of(expectedParticleGroups, [](int32_t g) {
            return g == BondOrderModifier::Carboxylate || g == BondOrderModifier::Nitro; });
        if(!deviation) {
            bool fractionalSame = true;
            for(size_t b = 0; b < bonds.size(); b++)
                fractionalSame &= std::abs(fractional.bondOrders[b] - expectedFractional[b]) < 1e-5;
            same &= integer.integerOrders == expectedOrders && fractionalSame && integer.bondGroups == expectedBondGroups;
            if(integer.integerOrders != expectedOrders)
                detail += QStringLiteral("; orders [%1] Pro [%2]").arg(join(integer.integerOrders)).arg(join(expectedOrders));
            if(!fractionalSame)
                detail += QStringLiteral("; fractional [%1] Pro [%2]").arg(join(fractional.bondOrders)).arg(join(expectedFractional));
            if(integer.bondGroups != expectedBondGroups)
                detail += QStringLiteral("; bond groups [%1] Pro [%2]").arg(join(integer.bondGroups)).arg(join(expectedBondGroups));
        }
        else {
            // Documented differences: no C or N above valence 4; the C-O / N-O bonds carry the group and share their total order.
            std::vector<int> valence(z.size(), 0);
            for(size_t b = 0; b < bonds.size(); b++) { valence[bonds[b].first] += integer.integerOrders[b]; valence[bonds[b].second] += integer.integerOrders[b]; }
            for(size_t i = 0; i < z.size(); i++)
                if((z[i] == 6 || z[i] == 7) && valence[i] > 4) { same = false; detail += QStringLiteral("; atom %1 has valence %2").arg(i).arg(valence[i]); }
            std::map<size_t, std::pair<double, double>> groupTotals;   // Central atom -> (integer sum, fractional sum).
            for(size_t b = 0; b < bonds.size(); b++) {
                const int32_t g = integer.bondGroups[b];
                if(g == BondOrderModifier::Carboxylate || g == BondOrderModifier::Nitro) {
                    const size_t center = (z[bonds[b].first] == 8) ? bonds[b].second : bonds[b].first;
                    groupTotals[center].first += integer.integerOrders[b];
                    groupTotals[center].second += fractional.bondOrders[b];
                }
                else if(g == BondOrderModifier::NoGroup && std::abs(fractional.bondOrders[b] - integer.integerOrders[b]) > 1e-9) {
                    same = false;
                    detail += QStringLiteral("; bond %1 outside any group became fractional").arg(b);
                }
            }
            same &= !groupTotals.empty();
            for(const auto& [center, totals] : groupTotals)
                same &= std::abs(totals.first - totals.second) < 1e-9;
            detail += QStringLiteral(" (carboxylate/nitro: checked the documented differences)");
        }
        ok &= check(name, same, detail);
    }

    qInfo().noquote() << "Bond order self-test: branch and bound vs. enumeration on random molecules";
    {
        uint64_t state = 20261026;
        auto next = [&](uint64_t n) {
            state = state * 6364136223846793005ULL + 1442695040888963407ULL;
            return (state >> 33) % n;
        };
        const int heavyElements[] = {6, 6, 6, 6, 7, 7, 8, 8, 14, 15, 16, 26};
        const int typicalValence[] = {0, 0, 0, 0, 0, 0, 4, 3, 2, 0, 0, 0, 0, 0, 4, 3, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2};
        int mismatches = 0, tested = 0;
        for(int trial = 0; trial < 3000; trial++) {
            const size_t heavy = 2 + next(6);
            std::vector<int> z;
            BondList bonds;
            std::vector<int> degree;
            for(size_t i = 0; i < heavy; i++) {
                z.push_back(heavyElements[next(std::size(heavyElements))]);
                degree.push_back(0);
                if(i > 0) { size_t j = next(i); bonds.emplace_back(j, i); degree[i]++; degree[j]++; }
            }
            if(next(2)) {   // Close a ring.
                size_t i = next(heavy), j = next(heavy);
                if(i != j && std::ranges::find(bonds, std::make_pair(std::min(i, j), std::max(i, j))) == bonds.end()
                        && std::ranges::find(bonds, std::make_pair(std::max(i, j), std::min(i, j))) == bonds.end()) {
                    bonds.emplace_back(i, j); degree[i]++; degree[j]++;
                }
            }
            for(size_t i = 0; i < heavy; i++) {
                const int hydrogens = std::max(0, typicalValence[z[i]] - degree[i] - (int)next(3));
                for(int h = 0; h < hydrogens; h++) { bonds.emplace_back(i, z.size()); z.push_back(1); }
            }
            const int64_t exact = bruteForceScore(z, bonds);
            const BondOrderModifier::Result result = BondOrderModifier::compute(z, bonds, 5, false);
            tested++;
            if(result.penaltyScore != exact || result.heuristicComponents != 0) {
                if(mismatches++ < 5)
                    qCritical().noquote() << "    molecule" << trial << ": branch and bound" << result.penaltyScore << "enumeration" << exact;
            }
        }
        ok &= check(QStringLiteral("optimal penalty on random molecules"), mismatches == 0, QStringLiteral("%1 of %2 differ").arg(mismatches).arg(tested));
    }

    qInfo().noquote() << "Bond order self-test: local search fallback";
    {
        // Linear polyacene with n rings (C_{4n+2} H_{2n+4}); a Kekule structure exists (penalty 0).
        const int rings = 12;
        std::vector<int> z;
        BondList bonds;
        const int top = 0;  // Top row: atoms 0..2n, bottom row: 2n+1..4n+1.
        const int rowLength = 2 * rings + 1;
        for(int i = 0; i < 2 * rowLength; i++) z.push_back(6);
        for(int i = 0; i + 1 < rowLength; i++) { bonds.emplace_back(top + i, top + i + 1); bonds.emplace_back(rowLength + i, rowLength + i + 1); }
        for(int i = 0; i < rowLength; i += 2) bonds.emplace_back(top + i, rowLength + i);
        std::vector<int> degree(z.size(), 0);
        for(auto [a, b] : bonds) { degree[a]++; degree[b]++; }
        const size_t carbons = z.size();
        for(size_t i = 0; i < carbons; i++)
            if(degree[i] == 2) { bonds.emplace_back(i, z.size()); z.push_back(1); }
        const BondOrderModifier::Result exact = BondOrderModifier::compute(z, bonds, 5, false);
        ok &= check(QStringLiteral("polyacene, exact: penalty 0, all carbons aromatic"),
            exact.penaltyScore == 0 && exact.heuristicComponents == 0 && std::count(exact.particleGroups.begin(), exact.particleGroups.end(), (int32_t)BondOrderModifier::Aromatic) == (long)carbons,
            QStringLiteral("score %1, %2 aromatic of %3").arg(exact.penaltyScore).arg(std::count(exact.particleGroups.begin(), exact.particleGroups.end(), (int32_t)BondOrderModifier::Aromatic)).arg(carbons));
        for(int limit : {3, 8}) {
            const BondOrderModifier::Result local = BondOrderModifier::compute(z, bonds, limit, false, 0);
            ok &= check(QStringLiteral("polyacene, node budget 0, fragment limit %1").arg(limit),
                local.penaltyScore >= exact.penaltyScore && (local.heuristicComponents == 0) == (local.penaltyScore == 0),
                QStringLiteral("score %1, %2 component(s) flagged").arg(local.penaltyScore).arg(local.heuristicComponents));
        }
    }

    qInfo().noquote() << "Bond order self-test: pipeline";
    QTemporaryDir dir;
    auto evaluate = [&](const QString& oxygenLabel, int fragmentSizeLimit, bool redistribute) {
        const QString path = dir.filePath(QStringLiteral("acetanilide_%1.data").arg(oxygenLabel));
        writeAcetanilide(path, oxygenLabel);
        QUrl url = QUrl::fromLocalFile(path);
        OORef<FileSourceImporter> importer = dynamic_object_cast<FileSourceImporter>(FileImporter::autodetectFileFormat(url).blockForResult());
        OORef<FileSource> source = OORef<FileSource>::create();
        source->setSource({url}, std::move(importer), false);
        (void) source->updateListOfFrames().blockForResult();
        OORef<Pipeline> pipeline = OORef<Pipeline>::create();
        pipeline->setHead(source);
        OORef<BondOrderModifier> modifier = OORef<BondOrderModifier>::create();
        modifier->setFragmentSizeLimit(fragmentSizeLimit);
        modifier->setRedistributeBondOrders(redistribute);
        pipeline->applyModifier(AnimationTime::fromFrame(0), false, modifier);
        return pipeline->evaluatePipeline(PipelineEvaluationRequest(AnimationTime::fromFrame(0), true, false)).blockForResult();
    };
    {
        PipelineFlowState state = evaluate(QStringLiteral("O"), 5, true);
        const Particles* particles = state.getObject<Particles>();
        const Bonds* bonds = particles ? particles->bonds() : nullptr;
        const Property* particleGroups = particles ? particles->getProperty(QStringLiteral("Functional Group")) : nullptr;
        const Property* bondGroups = bonds ? bonds->getProperty(QStringLiteral("Functional Group")) : nullptr;
        const Property* orders = bonds ? bonds->getProperty(QStringLiteral("Bond Order")) : nullptr;
        if(!particleGroups || !bondGroups || !orders)
            return check(QStringLiteral("acetanilide: output properties"), false, state.status().text());
        std::vector<int32_t> groups(particleGroups->size());
        particleGroups->copyTo(groups.begin());
        const std::vector<int32_t> expectedGroups = {2,2,2,2,2,2,1,1,1,0,0,0,0,0,0,0,0,0,0};
        ok &= check(QStringLiteral("acetanilide: particle groups"), groups == expectedGroups, join(groups));
        std::vector<FloatType> values(orders->size());
        orders->copyTo(values.begin());
        const std::vector<FloatType> expectedOrders = {1.5,1.5,1.5,1.5,1.5,1.5,1,1.41,1.41,1,1,1,1,1,1,1,1,1,1};
        bool ordersSame = values.size() == expectedOrders.size();
        for(size_t b = 0; ordersSame && b < values.size(); b++) ordersSame = std::abs(values[b] - expectedOrders[b]) < 1e-6;
        ok &= check(QStringLiteral("acetanilide: fractional bond orders"), ordersSame, join(values));
        bool typesOk = particleGroups->elementTypes().size() == BondOrderModifier::NumGroups && bondGroups->elementTypes().size() == BondOrderModifier::NumGroups;
        for(int g = 0; typesOk && g < BondOrderModifier::NumGroups; g++) {
            const ElementType* type = bondGroups->elementType(g);
            typesOk = type && type->name() == BondOrderModifier::groupName(g) && particleGroups->elementType(g)->color() == BondOrderModifier::groupColor(g);
        }
        ok &= check(QStringLiteral("acetanilide: functional group types"), typesOk, bondGroups->elementType(1) ? bondGroups->elementType(1)->name() : QString());
        ok &= check(QStringLiteral("acetanilide: BondOrder.PenaltyScore"), state.getAttributeValue(QStringLiteral("BondOrder.PenaltyScore")).toLongLong() == 0
            && state.getAttributeValue(QStringLiteral("BondOrder.PenaltyScore")).isValid(), state.getAttributeValue(QStringLiteral("BondOrder.PenaltyScore")).toString());
    }
    {
        // Modifier errors surface either as an exception or as an error status.
        auto errorOf = [&](const QString& oxygenLabel, int fragmentSizeLimit) -> QString {
            try {
                PipelineFlowState state = evaluate(oxygenLabel, fragmentSizeLimit, false);
                return state.status().type() == PipelineStatus::Error ? state.status().text() : QString();
            }
            catch(const Exception& ex) {
                return ex.messages().join(' ');
            }
        };
        const QString unknown = errorOf(QStringLiteral("Xx"), 5);
        ok &= check(QStringLiteral("unknown element: error"), unknown.contains(QStringLiteral("'Xx'")), unknown);
        const QString limit = errorOf(QStringLiteral("O"), 2);
        ok &= check(QStringLiteral("fragment size limit 2: error"), limit.contains(QStringLiteral("at least 3")), limit);
    }

    // Optional comparison with a large set of molecules evaluated with OVITO Pro.
    const QString referencePath = qEnvironmentVariable("OVITO_BOND_ORDER_REFERENCE");
    if(!referencePath.isEmpty()) {
        qInfo().noquote() << "Bond order self-test: external reference" << referencePath;
        QFile file(referencePath);
        if(!file.open(QIODevice::ReadOnly))
            return check(QStringLiteral("external reference"), false, QStringLiteral("cannot open %1").arg(referencePath));
        const QJsonArray molecules = QJsonDocument::fromJson(file.readAll()).array();
        size_t scoreMismatches = 0, orderMatches = 0, count = 0, heuristic = 0;
        for(const QJsonValue& value : molecules) {
            const QJsonObject molecule = value.toObject();
            if(!molecule.contains(QStringLiteral("bonds"))) continue;
            std::vector<int> z;
            for(const QJsonValue& e : molecule[QStringLiteral("elems")].toArray()) z.push_back(BondOrderModifier::atomicNumber(e.toString()));
            BondList bonds;
            for(const QJsonValue& b : molecule[QStringLiteral("bonds")].toArray())
                bonds.emplace_back(b.toArray()[0].toInt(), b.toArray()[1].toInt());
            const BondOrderModifier::Result result = BondOrderModifier::compute(z, bonds, 5, false);
            count++;
            if(result.heuristicComponents != 0) heuristic++;
            if(result.penaltyScore != molecule[QStringLiteral("score")].toInteger()) {
                if(scoreMismatches++ < 5)
                    qCritical().noquote() << "    molecule" << count - 1 << ": score" << result.penaltyScore << "Pro" << molecule[QStringLiteral("score")].toInteger();
            }
            std::vector<int> proOrders;
            for(const QJsonValue& o : molecule[QStringLiteral("bo")].toArray()) proOrders.push_back(o.toInt());
            if(proOrders == result.integerOrders) orderMatches++;
        }
        ok &= check(QStringLiteral("external reference: penalty scores"), scoreMismatches == 0,
            QStringLiteral("%1 of %2 differ; identical integer bond orders in %3; %4 not solved exactly").arg(scoreMismatches).arg(count).arg(orderMatches).arg(heuristic));
    }

    qInfo().noquote() << (ok ? "Bond order self-test: ALL PASSED" : "Bond order self-test: FAILED");
    return ok;
}

}   // End of namespace
