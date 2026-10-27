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
#include <ovito/particles/modifier/modify/ShrinkWrapBoxModifier.h>
#include <ovito/stdobj/simcell/SimulationCell.h>
#include <ovito/core/app/StandaloneApplication.h>
#include <ovito/core/dataset/io/FileImporter.h>
#include <ovito/core/dataset/io/FileSource.h>
#include <ovito/core/dataset/io/FileSourceImporter.h>
#include <ovito/core/dataset/pipeline/PipelineEvaluationRequest.h>
#include <ovito/core/dataset/scene/Pipeline.h>
#include <ovito/core/utilities/Exception.h>
#include "ShrinkWrapBoxSelfTestService.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTextStream>

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(ShrinkWrapBoxSelfTestService);

/******************************************************************************
* Registers the command-line options handled by this service.
******************************************************************************/
void ShrinkWrapBoxSelfTestService::registerCommandLineOptions(QCommandLineParser& cmdLineParser)
{
    cmdLineParser.addOption(QCommandLineOption(QStringList{QStringLiteral("shrink-wrap-selftest")},
        tr("Run the in-process regression test for the Shrink-wrap simulation box modifier and exit "
           "(0 on success, 1 on any failed check).")));
}

/******************************************************************************
* Runs the self-test after the application has entered the main event loop.
******************************************************************************/
void ShrinkWrapBoxSelfTestService::applicationStarted()
{
    if(!StandaloneApplication::instance()->cmdLineParser().isSet(QStringLiteral("shrink-wrap-selftest")))
        return;

    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("shrink_wrap.dump"));
    {
        QFile file(path);
        if(!file.open(QIODevice::WriteOnly | QIODevice::Text))
            throw Exception(QStringLiteral("Cannot write test dump %1").arg(path));
        QTextStream out(&file);
        out << "ITEM: TIMESTEP\n0\nITEM: NUMBER OF ATOMS\n3\nITEM: BOX BOUNDS pp pp ff\n-10 20\n-10 20\n-10 20\n"
               "ITEM: ATOMS id type x y z\n1 1 1.5 2 -3\n2 1 4 -1 5\n3 1 2 7.25 0\n";
    }
    QUrl url = QUrl::fromLocalFile(path);
    OORef<FileSourceImporter> importer = dynamic_object_cast<FileSourceImporter>(FileImporter::autodetectFileFormat(url).blockForResult());
    OORef<FileSource> source = OORef<FileSource>::create();
    source->setSource({url}, std::move(importer), false);
    (void) source->updateListOfFrames().blockForResult();
    OORef<Pipeline> pipeline = OORef<Pipeline>::create();
    pipeline->setHead(source);
    OORef<ShrinkWrapBoxModifier> modifier = OORef<ShrinkWrapBoxModifier>::create();
    pipeline->applyModifier(AnimationTime::fromFrame(0), false, modifier);
    PipelineFlowState state = pipeline->evaluatePipeline(PipelineEvaluationRequest(AnimationTime::fromFrame(0), true, false)).blockForResult();

    // Expected (as in OVITO Pro): origin (1.5, -1, -3), extents 2.5 x 8.25 x 8, no off-diagonal elements, no pbc.
    const SimulationCell* cell = state.expectObject<SimulationCell>();
    const AffineTransformation& m = cell->cellMatrix();
    const AffineTransformation expected(2.5, 0, 0, 1.5,
                                        0, 8.25, 0, -1,
                                        0, 0, 8, -3);
    bool ok = m.equals(expected, 1e-12) && !cell->pbcX() && !cell->pbcY() && !cell->pbcZ();
    if(ok)
        qInfo().noquote() << "Shrink-wrap self-test: ALL PASSED";
    else
        qCritical().noquote() << "Shrink-wrap self-test: FAILED, cell matrix" << m(0,0) << m(1,1) << m(2,2) << m(0,3) << m(1,3) << m(2,3)
                              << "pbc" << cell->pbcX() << cell->pbcY() << cell->pbcZ();
    // In --nogui mode QCoreApplication::exit() has no effect; throwing yields exit code 1.
    if(!ok)
        throw Exception(tr("Shrink-wrap self-test failed."));
    QCoreApplication::exit(0);
}

}   // End of namespace
