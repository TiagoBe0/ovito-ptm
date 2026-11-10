////////////////////////////////////////////////////////////////////////////////////////
//
//  Copyright 2026 OVITO GmbH, Germany
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
#include <ovito/particles/modifier/analysis/vacancy/DelaunayVacancyModifier.h>
#include <ovito/particles/objects/Particles.h>
#include <ovito/stdobj/simcell/SimulationCell.h>
#include <ovito/core/app/StandaloneApplication.h>
#include <ovito/core/dataset/io/FileImporter.h>
#include <ovito/core/dataset/io/FileSource.h>
#include <ovito/core/dataset/io/FileSourceImporter.h>
#include <ovito/core/dataset/pipeline/ModificationNode.h>
#include <ovito/core/dataset/pipeline/PipelineEvaluationRequest.h>
#include <ovito/core/dataset/scene/Pipeline.h>
#include <ovito/core/utilities/Exception.h>
#include "DelaunayVacancySelfTestService.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTextStream>
#include <random>

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(DelaunayVacancySelfTestService);

namespace {

/// One crystal: periodic cubic cell of N^3 unit cells with the given basis.
struct Crystal {
    const char* name;
    DelaunayVacancyModifier::LatticeType lattice;
    double a0;
    int n;
    std::vector<std::array<double,3>> basis;
};

const Crystal BCC_FE = {"BCC Fe", DelaunayVacancyModifier::BCC, 2.862, 8, {{0,0,0}, {0.5,0.5,0.5}}};
const Crystal FCC_CU = {"FCC Cu", DelaunayVacancyModifier::FCC, 3.615, 6, {{0,0,0}, {0.5,0.5,0}, {0.5,0,0.5}, {0,0.5,0.5}}};

/// Lattice sites of the crystal as positions, in generation order.
std::vector<std::array<double,3>> sitesOf(const Crystal& c)
{
    std::vector<std::array<double,3>> sites;
    for(int i = 0; i < c.n; i++) for(int j = 0; j < c.n; j++) for(int k = 0; k < c.n; k++)
        for(const auto& b : c.basis)
            sites.push_back({(i + b[0]) * c.a0, (j + b[1]) * c.a0, (k + b[2]) * c.a0});
    return sites;
}

double dist2(const std::array<double,3>& a, const std::array<double,3>& b, double L)
{
    double s = 0;
    for(int d = 0; d < 3; d++) {
        double x = a[d] - b[d];
        x -= L * std::round(x / L);
        s += x * x;
    }
    return s;
}

/// Index of the lattice site closest to p.
size_t siteNear(const std::vector<std::array<double,3>>& sites, const std::array<double,3>& p, double L)
{
    size_t best = 0;
    for(size_t i = 1; i < sites.size(); i++)
        if(dist2(sites[i], p, L) < dist2(sites[best], p, L)) best = i;
    return best;
}

/// A test case: sites to remove (given as points; the nearest site is removed).
struct Case {
    const char* name;
    std::vector<std::array<double,3>> removed;      // In units of a0.
};

/// Writes one dump frame per case. Frame 0 is the perfect crystal.
void writeDump(const QString& path, const Crystal& c, const std::vector<Case>& cases)
{
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly | QIODevice::Text))
        throw Exception(QStringLiteral("Cannot write test dump %1").arg(path));
    QTextStream out(&file);
    out.setRealNumberPrecision(10);
    const double L = c.n * c.a0;
    const auto sites = sitesOf(c);
    const double jitter = 0.017 * c.a0;     // ~0.05 A for Fe, comparable to thermal displacements at 300 K.
    for(size_t frame = 0; frame < cases.size(); frame++) {
        std::mt19937 rng(777 + (unsigned)frame);
        std::uniform_real_distribution<double> noise(-jitter, jitter);
        std::vector<char> removed(sites.size(), 0);
        for(const auto& r : cases[frame].removed)
            removed[siteNear(sites, {r[0] * c.a0, r[1] * c.a0, r[2] * c.a0}, L)] = 1;
        size_t count = std::count(removed.begin(), removed.end(), 0);
        out << "ITEM: TIMESTEP\n" << frame << "\n";
        out << "ITEM: NUMBER OF ATOMS\n" << count << "\n";
        out << "ITEM: BOX BOUNDS pp pp pp\n";
        for(int dim = 0; dim < 3; dim++)
            out << "0 " << L << "\n";
        out << "ITEM: ATOMS id type x y z\n";
        int id = 1;
        for(size_t s = 0; s < sites.size(); s++) {
            if(removed[s]) continue;
            out << id++ << " 1 " << sites[s][0] + noise(rng) << " " << sites[s][1] + noise(rng) << " " << sites[s][2] + noise(rng) << "\n";
        }
    }
}

bool check(const QString& name, bool ok, const QString& detail)
{
    if(ok)
        qInfo().noquote() << "  PASS" << name << ":" << detail;
    else
        qCritical().noquote() << "  FAIL" << name << ":" << detail;
    return ok;
}

/// Evaluates the modifier on one frame and checks the number of vacancies and their positions.
bool checkCase(const QUrl& url, const Crystal& c, const Case& testCase, int frame, bool refine, size_t expected)
{
    OORef<FileSourceImporter> importer = dynamic_object_cast<FileSourceImporter>(FileImporter::autodetectFileFormat(url).blockForResult());
    if(!importer)
        throw Exception(QStringLiteral("Could not detect the format of the test dump."));
    importer->setMultiTimestepFile(true);
    OORef<FileSource> source = OORef<FileSource>::create();
    source->setSource({url}, std::move(importer), false);
    (void) source->updateListOfFrames().blockForResult();
    OORef<Pipeline> pipeline = OORef<Pipeline>::create();
    pipeline->setHead(source);
    OORef<DelaunayVacancyModifier> mod = OORef<DelaunayVacancyModifier>::create();
    mod->setLattice(c.lattice);
    mod->setRefineWithLocalLattice(refine);
    ModificationNode* node = pipeline->applyModifier(AnimationTime::fromFrame(0), false, mod);

    PipelineFlowState state = pipeline->evaluatePipeline(PipelineEvaluationRequest(AnimationTime::fromFrame(frame), true, false)).blockForResult();
    const QVariant count = state.getAttributeValue(node, QStringLiteral("DelaunayVacancy.vacancy_count"));
    if(!count.isValid())
        throw Exception(QStringLiteral("The vacancy count attribute is missing from the pipeline output: %1").arg(state.status().text()));

    const QString label = QStringLiteral("%1, %2, %3").arg(c.name).arg(testCase.name).arg(refine ? "local lattice" : "void merge only");
    bool ok = check(label + QStringLiteral(" count"), count.toLongLong() == (qlonglong)expected,
        QStringLiteral("%1 (expected %2)").arg(count.toLongLong()).arg(expected));

    // Every removed site must be reported within a fraction of the nearest-neighbor distance.
    const Particles* sites = state.expectObject<Particles>();
    BufferReadAccess<Point3> pos(sites->expectProperty(Particles::PositionProperty));
    const double L = c.n * c.a0;
    const auto lattice = sitesOf(c);
    const double tolerance = 0.35 * c.a0;
    if(refine || testCase.removed.size() <= 1) {
        for(const auto& r : testCase.removed) {
            const auto target = lattice[siteNear(lattice, {r[0] * c.a0, r[1] * c.a0, r[2] * c.a0}, L)];
            double best = 1e30;
            for(size_t i = 0; i < pos.size(); i++)
                best = std::min(best, std::sqrt(dist2({pos[i].x(), pos[i].y(), pos[i].z()}, target, L)));
            ok &= check(label + QStringLiteral(" position"), best < tolerance,
                QStringLiteral("nearest detection %1 A from the removed site (tolerance %2 A)").arg(best).arg(tolerance));
        }
    }
    return ok;
}

}   // End of anonymous namespace

/******************************************************************************
* Registers the command-line options handled by this service.
******************************************************************************/
void DelaunayVacancySelfTestService::registerCommandLineOptions(QCommandLineParser& cmdLineParser)
{
    cmdLineParser.addOption(QCommandLineOption(QStringList{QStringLiteral("delaunay-vacancy-selftest")},
        tr("Run the in-process regression test for the Delaunay vacancy analysis modifier and exit "
           "(0 on success, 1 on any failed check).")));
    cmdLineParser.addOption(QCommandLineOption(QStringList{QStringLiteral("delaunay-vacancy-run")},
        tr("Run the Delaunay vacancy analysis modifier on the last frame of the given file and write the detected sites (see --delaunay-vacancy-out)."), tr("file")));
    cmdLineParser.addOption(QCommandLineOption(QStringList{QStringLiteral("delaunay-vacancy-out")},
        tr("Output text file of --delaunay-vacancy-run: one 'x y z' line per detected vacancy."), tr("file")));
    cmdLineParser.addOption(QCommandLineOption(QStringList{QStringLiteral("delaunay-vacancy-lattice")},
        tr("Lattice assumed by --delaunay-vacancy-run: 'bcc' (default) or 'fcc'."), tr("lattice")));
    cmdLineParser.addOption(QCommandLineOption(QStringList{QStringLiteral("delaunay-vacancy-a0")},
        tr("Lattice constant used by --delaunay-vacancy-run (default 0 = estimate from the nearest-neighbor distances)."), tr("length")));
}

/******************************************************************************
* Runs the self-test after the application has entered the main event loop.
******************************************************************************/
void DelaunayVacancySelfTestService::applicationStarted()
{
    const QCommandLineParser& parser = StandaloneApplication::instance()->cmdLineParser();
    if(parser.isSet(QStringLiteral("delaunay-vacancy-run"))) {
        runBatch(parser);
        QCoreApplication::exit(0);
        return;
    }
    if(!parser.isSet(QStringLiteral("delaunay-vacancy-selftest")))
        return;

    bool ok = false;
    try {
        ok = runTest();
    }
    catch(const Exception& ex) {
        qCritical().noquote() << "Delaunay vacancy self-test: FAILED with an exception:";
        for(const QString& msg : ex.messages())
            qCritical().noquote() << msg;
        ok = false;
    }
    catch(const std::exception& ex) {
        qCritical().noquote() << "Delaunay vacancy self-test: FAILED with an exception:" << QString::fromLocal8Bit(ex.what());
        ok = false;
    }
    // In --nogui mode QCoreApplication::exit() has no effect here; throwing yields exit code 1.
    if(!ok)
        throw Exception(tr("Delaunay vacancy self-test failed."));
    QCoreApplication::exit(0);
}

/******************************************************************************
* Batch mode: runs the modifier on a file and writes the detected sites.
******************************************************************************/
void DelaunayVacancySelfTestService::runBatch(const QCommandLineParser& parser)
{
    if(!parser.isSet(QStringLiteral("delaunay-vacancy-out")))
        throw Exception(tr("--delaunay-vacancy-run requires --delaunay-vacancy-out."));
    const QString latticeName = parser.isSet(QStringLiteral("delaunay-vacancy-lattice")) ? parser.value(QStringLiteral("delaunay-vacancy-lattice")).toLower() : QStringLiteral("bcc");
    if(latticeName != QStringLiteral("bcc") && latticeName != QStringLiteral("fcc"))
        throw Exception(tr("--delaunay-vacancy-lattice must be 'bcc' or 'fcc'."));

    const QUrl url = QUrl::fromLocalFile(parser.value(QStringLiteral("delaunay-vacancy-run")));
    OORef<FileSourceImporter> importer = dynamic_object_cast<FileSourceImporter>(FileImporter::autodetectFileFormat(url).blockForResult());
    if(!importer)
        throw Exception(tr("Could not detect the format of the input file."));
    importer->setMultiTimestepFile(true);
    OORef<FileSource> source = OORef<FileSource>::create();
    source->setSource({url}, std::move(importer), false);
    (void) source->updateListOfFrames().blockForResult();
    OORef<Pipeline> pipeline = OORef<Pipeline>::create();
    pipeline->setHead(source);
    OORef<DelaunayVacancyModifier> mod = OORef<DelaunayVacancyModifier>::create();
    mod->setLattice(latticeName == QStringLiteral("fcc") ? DelaunayVacancyModifier::FCC : DelaunayVacancyModifier::BCC);
    if(parser.isSet(QStringLiteral("delaunay-vacancy-a0"))) {
        bool parsed = false;
        const double a0 = parser.value(QStringLiteral("delaunay-vacancy-a0")).toDouble(&parsed);
        if(!parsed || a0 < 0)
            throw Exception(tr("--delaunay-vacancy-a0 must be a non-negative number."));
        mod->setLatticeConstant(a0);
    }
    pipeline->applyModifier(AnimationTime::fromFrame(0), false, mod);

    const int lastFrame = std::max(0, source->numberOfSourceFrames() - 1);
    PipelineFlowState state = pipeline->evaluatePipeline(PipelineEvaluationRequest(AnimationTime::fromFrame(lastFrame), true, false)).blockForResult();
    const Particles* sites = state.expectObject<Particles>();
    BufferReadAccess<Point3> pos(sites->expectProperty(Particles::PositionProperty));

    QFile file(parser.value(QStringLiteral("delaunay-vacancy-out")));
    if(!file.open(QIODevice::WriteOnly | QIODevice::Text))
        throw Exception(tr("Cannot write %1").arg(file.fileName()));
    QTextStream out(&file);
    out.setRealNumberPrecision(8);
    for(const Point3& p : pos)
        out << p.x() << " " << p.y() << " " << p.z() << "\n";
    qInfo().noquote() << "Delaunay vacancy: frame" << lastFrame << "->" << pos.size() << "vacancies," << state.status().text();
}

/******************************************************************************
* Runs all checks.
******************************************************************************/
bool DelaunayVacancySelfTestService::runTest()
{
    QTemporaryDir dir;
    if(!dir.isValid())
        throw Exception(QStringLiteral("Cannot create a temporary directory."));

    bool ok = true;
    for(const Crystal* c : {&BCC_FE, &FCC_CU}) {
        // Positions in units of a0. The divacancy removes a site and its first neighbor.
        const double d1 = (c->lattice == DelaunayVacancyModifier::BCC) ? 0.5 : 0.5;
        const std::vector<Case> cases = {
            {"perfect crystal", {}},
            {"single vacancy", {{2.0, 2.0, 2.0}}},
            {"first-neighbor divacancy", {{2.0, 2.0, 2.0}, {2.0 + d1, 2.0 + d1, 2.0 + (c->lattice == DelaunayVacancyModifier::BCC ? d1 : 0.0)}}},
            {"three isolated vacancies", {{1.0, 1.0, 1.0}, {3.0, 4.0, 1.0}, {4.0, 1.0, 3.0}}},
        };
        const QString path = dir.filePath(QStringLiteral("delaunay_vacancy_test_%1.dump").arg(c->n));
        writeDump(path, *c, cases);
        const QUrl url = QUrl::fromLocalFile(path);
        ok &= checkCase(url, *c, cases[0], 0, true, 0);
        ok &= checkCase(url, *c, cases[1], 1, true, 1);
        ok &= checkCase(url, *c, cases[1], 1, false, 1);
        ok &= checkCase(url, *c, cases[2], 2, true, 2);
        ok &= checkCase(url, *c, cases[3], 3, true, 3);
    }

    qInfo().noquote() << (ok ? "Delaunay vacancy self-test: ALL PASSED" : "Delaunay vacancy self-test: FAILED");
    return ok;
}

}   // End of namespace
