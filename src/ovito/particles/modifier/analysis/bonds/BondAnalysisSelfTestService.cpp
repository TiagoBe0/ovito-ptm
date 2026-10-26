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
#include <ovito/particles/modifier/analysis/bonds/BondAnalysisModifier.h>
#include <ovito/particles/modifier/modify/CreateBondsModifier.h>
#include <ovito/particles/objects/Particles.h>
#include <ovito/particles/objects/Bonds.h>
#include <ovito/particles/modifier/selection/ParticlesExpressionSelectionModifierDelegate.h>
#include <ovito/stdmod/modifiers/ExpressionSelectionModifier.h>
#include <ovito/stdobj/table/DataTable.h>
#include <ovito/core/app/StandaloneApplication.h>
#include <ovito/core/dataset/io/FileImporter.h>
#include <ovito/core/dataset/io/FileSource.h>
#include <ovito/core/dataset/io/FileSourceImporter.h>
#include <ovito/core/dataset/pipeline/ModificationNode.h>
#include <ovito/core/dataset/pipeline/PipelineEvaluationRequest.h>
#include <ovito/core/dataset/scene/Pipeline.h>
#include <ovito/core/utilities/Exception.h>
#include "BondAnalysisSelfTestService.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTextStream>

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(BondAnalysisSelfTestService);

namespace {

/// Central atom 1 at x = 0.5; atom 4 sits at x = 8.9, i.e. 1.6 away through the periodic boundary.
void writeDataFile(const QString& path)
{
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly | QIODevice::Text))
        throw Exception(QStringLiteral("Cannot write test data file %1").arg(path));
    QTextStream out(&file);
    out << "Bond analysis self-test\n\n"
           "4 atoms\n3 bonds\n2 atom types\n2 bond types\n\n"
           "0 10 xlo xhi\n0 10 ylo yhi\n0 10 zlo zhi\n\n"
           "Atoms # bond\n\n"
           "1 1 1 0.5 5 5\n"
           "2 1 2 1.6 5 5\n"
           "3 1 2 0.5 7.2 5\n"
           "4 1 1 8.9 5 5\n\n"
           "Bonds\n\n"
           "1 1 1 2\n"
           "2 2 1 3\n"
           "3 1 4 1\n";
}

/// 300 particles of two types from a 64-bit LCG, uniform in a periodic 12^3 box.
void writeRandomDataFile(const QString& path)
{
    uint64_t state = 987654321;
    auto next = [&]() {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return (double)(state >> 11) / (double)(1ULL << 53);
    };
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly | QIODevice::Text))
        throw Exception(QStringLiteral("Cannot write test data file %1").arg(path));
    QTextStream out(&file);
    out << "Bond distributions self-test\n\n300 atoms\n2 atom types\n\n0 12 xlo xhi\n0 12 ylo yhi\n0 12 zlo zhi\n\n"
           "Masses\n\n1 1.0 # A\n2 1.0 # B\n\nAtoms # atomic\n\n";
    for(int i = 1; i <= 300; i++) {
        const int type = next() < 0.5 ? 1 : 2;
        const double x = 12 * next(), y = 12 * next(), z = 12 * next();
        out << QString::asprintf("%d %d %.10f %.10f %.10f\n", i, type, x, y, z);
    }
}

/// Returns one component of the histogram counts.
std::vector<double> counts(const PipelineFlowState& state, const char* identifier, size_t component)
{
    ConstDataObjectPath path = state.data()->getObject(DataTable::OOClass(), QString::fromLatin1(identifier));
    if(path.empty())
        throw Exception(QStringLiteral("Table '%1' is missing from the pipeline output: %2").arg(identifier).arg(state.status().text()));
    const Property* y = static_object_cast<DataTable>(path.back())->y();
    if(component >= y->componentCount())
        throw Exception(QStringLiteral("Table '%1' has only %2 components.").arg(identifier).arg(y->componentCount()));
    std::vector<double> values(y->size());
    y->copyComponentTo(values.begin(), component);
    return values;
}

size_t componentCount(const PipelineFlowState& state, const char* identifier)
{
    ConstDataObjectPath path = state.data()->getObject(DataTable::OOClass(), QString::fromLatin1(identifier));
    return path.empty() ? 0 : static_object_cast<DataTable>(path.back())->y()->componentCount();
}

bool check(const QString& name, const std::vector<double>& actual, const std::vector<double>& expected)
{
    const bool ok = actual == expected;
    QStringList a, e;
    for(double v : actual) a << QString::number(v);
    for(double v : expected) e << QString::number(v);
    if(ok)
        qInfo().noquote() << "  PASS" << name << ":" << a.join(' ');
    else
        qCritical().noquote() << "  FAIL" << name << ": got [" << a.join(' ') << "] expected [" << e.join(' ') << "]";
    return ok;
}

}   // End of anonymous namespace

/******************************************************************************
* Registers the command-line options handled by this service.
******************************************************************************/
void BondAnalysisSelfTestService::registerCommandLineOptions(QCommandLineParser& cmdLineParser)
{
    cmdLineParser.addOption(QCommandLineOption(QStringList{QStringLiteral("bond-analysis-selftest")},
        tr("Run the in-process regression test for the Bond analysis modifier and exit "
           "(0 on success, 1 on any failed check).")));
}

/******************************************************************************
* Runs the self-test after the application has entered the main event loop.
******************************************************************************/
void BondAnalysisSelfTestService::applicationStarted()
{
    const QCommandLineParser& parser = StandaloneApplication::instance()->cmdLineParser();
    if(!parser.isSet(QStringLiteral("bond-analysis-selftest")))
        return;

    bool ok = false;
    try {
        ok = runTest();
    }
    catch(const Exception& ex) {
        qCritical().noquote() << "Bond analysis self-test: FAILED with an exception:";
        for(const QString& msg : ex.messages())
            qCritical().noquote() << msg;
        ok = false;
    }
    catch(const std::exception& ex) {
        qCritical().noquote() << "Bond analysis self-test: FAILED with an exception:" << QString::fromLocal8Bit(ex.what());
        ok = false;
    }
    // In --nogui mode the Qt event loop is not running at this point, so QCoreApplication::exit()
    // has no effect and the process exits with 0 regardless. Throwing makes the startup code call
    // exitWithFatalError(), which shuts the application down and yields exit code 1.
    if(!ok)
        throw Exception(tr("Bond analysis self-test failed."));
    QCoreApplication::exit(0);
}

/******************************************************************************
* Runs all checks.
******************************************************************************/
bool BondAnalysisSelfTestService::runTest()
{
    QTemporaryDir dir;
    if(!dir.isValid())
        throw Exception(QStringLiteral("Cannot create a temporary directory."));
    const QString dataPath = dir.filePath(QStringLiteral("bond_analysis_test.data"));
    writeDataFile(dataPath);

    QUrl url = QUrl::fromLocalFile(dataPath);
    OORef<FileSourceImporter> importer = dynamic_object_cast<FileSourceImporter>(FileImporter::autodetectFileFormat(url).blockForResult());
    if(!importer)
        throw Exception(QStringLiteral("Could not detect the format of the test data file."));
    OORef<FileSource> source = OORef<FileSource>::create();
    source->setSource({url}, std::move(importer), false);
    (void) source->updateListOfFrames().blockForResult();
    OORef<Pipeline> pipeline = OORef<Pipeline>::create();
    pipeline->setHead(source);

    // Select the type-2 particles and the type-2 bond, so that the selection modes reproduce the type modes.
    OORef<ExpressionSelectionModifier> particleSelection = OORef<ExpressionSelectionModifier>::create();
    particleSelection->setExpression(QStringLiteral("ParticleType == 2"));
    pipeline->applyModifier(AnimationTime::fromFrame(0), false, particleSelection);
    OORef<ExpressionSelectionModifier> bondSelection = OORef<ExpressionSelectionModifier>::create();
    bondSelection->setDelegate(OORef<BondsExpressionSelectionModifierDelegate>::create());
    bondSelection->setExpression(QStringLiteral("BondType == 2"));
    pipeline->applyModifier(AnimationTime::fromFrame(0), false, bondSelection);

    OORef<BondAnalysisModifier> analysis = OORef<BondAnalysisModifier>::create();
    analysis->setBins(9);
    analysis->setLengthCutoff(2.7);
    analysis->setUseCosine(false);  // Memorized fields: do not inherit GUI settings.
    pipeline->applyModifier(AnimationTime::fromFrame(0), false, analysis);

    auto evaluate = [&]() {
        return pipeline->evaluatePipeline(PipelineEvaluationRequest(AnimationTime::fromFrame(0), true, false)).blockForResult();
    };
    const char* L = BondAnalysisModifier::lengthTableIdentifier;
    const char* A = BondAnalysisModifier::angleTableIdentifier;

    bool ok = true;

    // Length bins of width 0.3: 1.1 -> 3, 1.6 -> 5, 2.2 -> 7. Angle bins of 20 degrees: 90 -> 4, 180 -> 8.
    qInfo().noquote() << "Bond analysis self-test: no partitioning";
    {
        PipelineFlowState state = evaluate();
        ok &= check(QStringLiteral("lengths"), counts(state, L, 0), {0, 0, 0, 1, 0, 1, 0, 1, 0});
        ok &= check(QStringLiteral("angles"), counts(state, A, 0), {0, 0, 0, 0, 2, 0, 0, 0, 1});
    }

    qInfo().noquote() << "Bond analysis self-test: cosines (cos 90 = 0 -> bin 4, cos 180 = -1 -> bin 0)";
    analysis->setUseCosine(true);
    {
        PipelineFlowState state = evaluate();
        ok &= check(QStringLiteral("cosines"), counts(state, A, 0), {1, 0, 0, 0, 2, 0, 0, 0, 0});
    }
    analysis->setUseCosine(false);

    qInfo().noquote() << "Bond analysis self-test: length cutoff 2.0 drops the 2.2 bond from the lengths only";
    analysis->setLengthCutoff(2.0);
    {
        PipelineFlowState state = evaluate();
        // Bins of width 2/9: 1.1 -> 4, 1.6 -> 7.
        ok &= check(QStringLiteral("lengths"), counts(state, L, 0), {0, 0, 0, 0, 1, 0, 0, 1, 0});
        ok &= check(QStringLiteral("angles unchanged"), counts(state, A, 0), {0, 0, 0, 0, 2, 0, 0, 0, 1});
    }
    analysis->setLengthCutoff(2.7);

    // Bond kinds: 1 (bonds to atoms 2 and 4) and 2 (bond to atom 3). Angle components: 1-1, 1-2, 2-2.
    for(auto mode : {BondAnalysisModifier::ByBondType, BondAnalysisModifier::ByBondSelection}) {
        qInfo().noquote() << (mode == BondAnalysisModifier::ByBondType ? "Bond analysis self-test: by bond type" : "Bond analysis self-test: by bond selection");
        analysis->setPartition(mode);
        PipelineFlowState state = evaluate();
        ok &= check(QStringLiteral("component counts (lengths, angles)"), {(double)componentCount(state, L), (double)componentCount(state, A)}, {2, 3});
        ok &= check(QStringLiteral("lengths kind 1"), counts(state, L, 0), {0, 0, 0, 1, 0, 1, 0, 0, 0});
        ok &= check(QStringLiteral("lengths kind 2"), counts(state, L, 1), {0, 0, 0, 0, 0, 0, 0, 1, 0});
        ok &= check(QStringLiteral("angles 1-1"), counts(state, A, 0), {0, 0, 0, 0, 0, 0, 0, 0, 1});
        ok &= check(QStringLiteral("angles 1-2"), counts(state, A, 1), {0, 0, 0, 0, 2, 0, 0, 0, 0});
        ok &= check(QStringLiteral("angles 2-2"), counts(state, A, 2), {0, 0, 0, 0, 0, 0, 0, 0, 0});
    }

    // Species: 1 (atoms 1, 4) and 2 (atoms 2, 3). Length components 1-1, 1-2, 2-2; angle components
    // per center (1, 2) x end pair (1-1, 1-2, 2-2). All angles have a type-1 center.
    for(auto mode : {BondAnalysisModifier::ByParticleType, BondAnalysisModifier::ByParticleSelection}) {
        qInfo().noquote() << (mode == BondAnalysisModifier::ByParticleType ? "Bond analysis self-test: by particle type" : "Bond analysis self-test: by particle selection");
        analysis->setPartition(mode);
        PipelineFlowState state = evaluate();
        ok &= check(QStringLiteral("component counts (lengths, angles)"), {(double)componentCount(state, L), (double)componentCount(state, A)}, {3, 6});
        ok &= check(QStringLiteral("lengths 1-1"), counts(state, L, 0), {0, 0, 0, 0, 0, 1, 0, 0, 0});
        ok &= check(QStringLiteral("lengths 1-2"), counts(state, L, 1), {0, 0, 0, 1, 0, 0, 0, 1, 0});
        ok &= check(QStringLiteral("lengths 2-2"), counts(state, L, 2), {0, 0, 0, 0, 0, 0, 0, 0, 0});
        ok &= check(QStringLiteral("angles 1-1-1"), counts(state, A, 0), {0, 0, 0, 0, 0, 0, 0, 0, 0});
        ok &= check(QStringLiteral("angles 1-1-2"), counts(state, A, 1), {0, 0, 0, 0, 1, 0, 0, 0, 1});
        ok &= check(QStringLiteral("angles 2-1-2"), counts(state, A, 2), {0, 0, 0, 0, 1, 0, 0, 0, 0});
        double centerTwo = 0;
        for(size_t c = 3; c < 6; c++)
            for(double v : counts(state, A, c)) centerTwo += v;
        ok &= check(QStringLiteral("no angles with a type-2 center"), {centerTwo}, {0});
    }

    // The Bond length/angle distribution modifiers on a random two-species system with bonds from Create bonds
    // (cutoff 3). Per component: total count and first moment sum(bin * count), compared with OVITO Pro 3.16.1
    // (BondLengthDistributionModifier / BondAngleDistributionModifier, ovitos, same input and settings).
    qInfo().noquote() << "Bond analysis self-test: bond length and bond angle distribution modifiers";
    {
        const QString randomPath = dir.filePath(QStringLiteral("random.data"));
        writeRandomDataFile(randomPath);
        const QUrl randomUrl = QUrl::fromLocalFile(randomPath);
        auto run = [&](OORef<BondAnalysisModifier> modifier) {
            OORef<FileSource> src = OORef<FileSource>::create();
            src->setSource({randomUrl}, dynamic_object_cast<FileSourceImporter>(FileImporter::autodetectFileFormat(randomUrl).blockForResult()), false);
            (void) src->updateListOfFrames().blockForResult();
            OORef<Pipeline> p = OORef<Pipeline>::create();
            p->setHead(src);
            OORef<CreateBondsModifier> createBonds = OORef<CreateBondsModifier>::create();
            createBonds->setCutoffMode(CreateBondsModifier::UniformCutoff);
            createBonds->setUniformCutoff(3.0);
            createBonds->setOnlyIntraMoleculeBonds(false);
            p->applyModifier(AnimationTime::fromFrame(0), false, createBonds);
            p->applyModifier(AnimationTime::fromFrame(0), false, modifier);
            return p->evaluatePipeline(PipelineEvaluationRequest(AnimationTime::fromFrame(0), true, false)).blockForResult();
        };
        auto moments = [&](const PipelineFlowState& state, const char* id) {
            std::vector<double> m;
            for(size_t c = 0; c < componentCount(state, id); c++) {
                double total = 0, first = 0;
                std::vector<double> v = counts(state, id, c);
                for(size_t b = 0; b < v.size(); b++) { total += v[b]; first += b * v[b]; }
                m.push_back(total);
                m.push_back(first);
            }
            return m;
        };
        struct Case { bool lengths; BondAnalysisModifier::PartitioningMode partition; bool cosine; std::vector<double> expected; };
        const Case cases[] = {
            {true, BondAnalysisModifier::Off, false, {2903, 160952}},
            {true, BondAnalysisModifier::ByParticleType, false, {707, 39210, 1446, 80554, 750, 41188}},
            {false, BondAnalysisModifier::Off, false, {56256, 5035612}},
            {false, BondAnalysisModifier::ByParticleType, true, {6497, 584994, 13530, 1199776, 7417, 668251, 6787, 605411, 14495, 1295768, 7530, 682009}},
        };
        for(const Case& c : cases) {
            OORef<BondAnalysisModifier> modifier;
            if(c.lengths) modifier = OORef<BondLengthDistributionModifier>::create();
            else modifier = OORef<BondAngleDistributionModifier>::create();
            modifier->setBins(c.lengths ? 100 : 180);
            modifier->setLengthCutoff(4.0);
            modifier->setUseCosine(c.cosine);
            modifier->setPartition(c.partition);
            PipelineFlowState state = run(modifier);
            const char* own = c.lengths ? L : A;
            const char* other = c.lengths ? A : L;
            const QString name = QStringLiteral("%1 partition=%2 cosine=%3").arg(c.lengths ? "lengths" : "angles").arg((int)c.partition).arg(c.cosine);
            ok &= check(name + QStringLiteral(": only its own table"), {(double)componentCount(state, other)}, {0});
            std::vector<double> m = moments(state, own);
            if(qEnvironmentVariableIsSet("OVITO_BA_DUMP")) {
                QStringList values;
                for(double v : m) values << QString::number(v, 'g', 17);
                qInfo().noquote() << "  DUMP" << name << values.join(QStringLiteral(", "));
            }
            else
                ok &= check(name + QStringLiteral(": (total, first moment) per component"), m, c.expected);
        }
    }

    qInfo().noquote() << (ok ? "Bond analysis self-test: ALL PASSED" : "Bond analysis self-test: FAILED");
    return ok;
}

}   // End of namespace
