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
#include <ovito/particles/modifier/analysis/structure_factor/StructureFactorModifier.h>
#include <ovito/particles/objects/Particles.h>
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
#include "StructureFactorSelfTestService.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(StructureFactorSelfTestService);

namespace {

/// Cu3Au (L1_2), 4x4x4 cells, a = 3.75; atom id (1-based) displaced by 0.12 sin(1.3 id + 0.7 d) along direction d.
void writeDataFile(const QString& path)
{
    constexpr double A = 3.75;
    constexpr int N = 4;
    const struct { double b[3]; int type; } basis[4] = {{{0, 0, 0}, 2}, {{0, .5, .5}, 1}, {{.5, 0, .5}, 1}, {{.5, .5, 0}, 1}};
    QStringList lines;
    int id = 0;
    for(int i = 0; i < N; i++) for(int j = 0; j < N; j++) for(int k = 0; k < N; k++) for(const auto& site : basis) {
        id++;
        const int cellIndex[3] = {i, j, k};
        double p[3];
        for(int d = 0; d < 3; d++) p[d] = (cellIndex[d] + site.b[d]) * A + 0.12 * std::sin(1.3 * id + 0.7 * d);
        lines.push_back(QString::asprintf("%d %d %.10f %.10f %.10f", id, site.type, p[0], p[1], p[2]));
    }
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly | QIODevice::Text))
        throw Exception(QStringLiteral("Cannot write test file %1").arg(path));
    QTextStream out(&file);
    out << "structure factor test\n\n" << id << " atoms\n2 atom types\n\n";
    for(const char* d : {"x", "y", "z"}) out << QString::asprintf("0 %.10f %slo %shi\n", N * A, d, d);
    out << "\nMasses\n\n1 63.546 # Cu\n2 196.97 # Au\n\nAtoms # atomic\n\n" << lines.join('\n') << "\n";
}

}   // End of anonymous namespace

/******************************************************************************
* Registers the command-line options handled by this service.
******************************************************************************/
void StructureFactorSelfTestService::registerCommandLineOptions(QCommandLineParser& cmdLineParser)
{
    cmdLineParser.addOption(QCommandLineOption(QStringList{QStringLiteral("structure-factor-selftest")},
        tr("Run the in-process regression test for the Structure factor modifier and exit (0 on success, 1 on any failed check).")));
}

/******************************************************************************
* Runs the self-test after the application has entered the main event loop.
******************************************************************************/
void StructureFactorSelfTestService::applicationStarted()
{
    if(!StandaloneApplication::instance()->cmdLineParser().isSet(QStringLiteral("structure-factor-selftest")))
        return;
    bool ok = false;
    try {
        ok = runTest();
    }
    catch(const Exception& ex) {
        qCritical().noquote() << "Structure factor self-test: FAILED with an exception:";
        for(const QString& msg : ex.messages())
            qCritical().noquote() << msg;
        ok = false;
    }
    // In --nogui mode QCoreApplication::exit() has no effect; throwing yields exit code 1.
    if(!ok)
        throw Exception(tr("Structure factor self-test failed."));
    QCoreApplication::exit(0);
}

/******************************************************************************
* Runs all checks.
******************************************************************************/
bool StructureFactorSelfTestService::runTest()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("sf_test.data"));
    writeDataFile(path);

    /// One output column: row-major values and number of components.
    struct Curve { std::vector<double> values; size_t components = 0; };
    std::map<QString, std::map<QString, Curve>> results;

    QJsonObject dump;
    bool ok = true;
    for(auto mode : {StructureFactorModifier::Direct, StructureFactorModifier::Debye})
    for(bool partials : {false, true})
    for(bool formFactors : {false, true})
    for(bool selectedOnly : {false, true}) {
        QUrl url = QUrl::fromLocalFile(path);
        OORef<FileSourceImporter> importer = dynamic_object_cast<FileSourceImporter>(FileImporter::autodetectFileFormat(url).blockForResult());
        OORef<FileSource> source = OORef<FileSource>::create();
        source->setSource({url}, std::move(importer), false);
        (void) source->updateListOfFrames().blockForResult();
        OORef<Pipeline> pipeline = OORef<Pipeline>::create();
        pipeline->setHead(source);
        if(selectedOnly) {
            OORef<ExpressionSelectionModifier> selection = OORef<ExpressionSelectionModifier>::create();
            selection->setExpression(QStringLiteral("ParticleType == 1"));
            pipeline->applyModifier(AnimationTime::fromFrame(0), false, selection);
        }
        OORef<StructureFactorModifier> sf = OORef<StructureFactorModifier>::create();
        sf->setMode(mode);
        sf->setKMin(0);
        sf->setKMax(mode == StructureFactorModifier::Direct ? 8 : 10);
        sf->setNumberOfBins(mode == StructureFactorModifier::Direct ? 32 : 100);
        sf->setComputePartials(partials);
        sf->setUseFormFactors(formFactors);
        sf->setOnlySelected(selectedOnly);
        pipeline->applyModifier(AnimationTime::fromFrame(0), false, sf);
        PipelineFlowState state = pipeline->evaluatePipeline(PipelineEvaluationRequest(AnimationTime::fromFrame(0), true, false)).blockForResult();

        const QString key = QStringLiteral("%1 partial=%2 ff=%3 sel=%4").arg(mode == StructureFactorModifier::Direct ? "Direct" : "Debye")
            .arg(partials).arg(formFactors).arg(selectedOnly);
        QJsonObject tables;
        for(const char* id : {StructureFactorModifier::tableIdentifier, StructureFactorModifier::faberZimanTableIdentifier}) {
            ConstDataObjectPath p = state.data()->getObject(DataTable::OOClass(), QString::fromLatin1(id));
            if(p.empty()) continue;
            const Property* y = static_object_cast<DataTable>(p.back())->y();
            std::vector<double> values(y->size() * y->componentCount());
            y->copyTo(values.begin());
            QJsonArray rows;
            for(size_t r = 0; r < y->size(); r++) {
                if(y->componentCount() == 1) { rows.append(values[r]); continue; }
                QJsonArray row;
                for(size_t c = 0; c < y->componentCount(); c++) row.append(values[r * y->componentCount() + c]);
                rows.append(row);
            }
            tables.insert(QString::fromLatin1(id), rows);
            results[key][QString::fromLatin1(id)] = Curve{std::move(values), y->componentCount()};
        }
        dump.insert(key, tables);
    }

    if(qEnvironmentVariableIsSet("OVITO_SF_DUMP")) {
        QFile out(qEnvironmentVariable("OVITO_SF_DUMP"));
        if(!out.open(QIODevice::WriteOnly))
            throw Exception(QStringLiteral("Cannot write %1").arg(out.fileName()));
        out.write(QJsonDocument(dump).toJson(QJsonDocument::Compact));
        qInfo().noquote() << "Structure factor self-test: results written to" << out.fileName();
        return true;
    }

    const QString sfId = QString::fromLatin1(StructureFactorModifier::tableIdentifier);
    const QString fzId = QString::fromLatin1(StructureFactorModifier::faberZimanTableIdentifier);
    auto curve = [&](const QString& key, const QString& id) -> const Curve* {
        auto r = results.find(key);
        if(r == results.end()) return nullptr;
        auto c = r->second.find(id);
        return c == r->second.end() ? nullptr : &c->second;
    };
    auto fail = [&](const QString& msg) { qCritical().noquote() << "Structure factor self-test:" << msg; ok = false; };
    auto close = [](double a, double b, double relTol) { return std::abs(a - b) <= relTol * std::max(1.0, std::abs(b)); };

    // (1) Values of OVITO Pro 3.16.1 (ovitos, same input and settings) at selected bins. The direct method agrees
    // to machine precision; the Debye method to ~1e-5 because of the RDF discretization.
    struct Reference { const char* key; const char* table; size_t bin; std::vector<double> values; double tol; };
    const Reference references[] = {
        {"Direct partial=0 ff=0 sel=0", StructureFactorModifier::tableIdentifier, 8, {0.031981473926018536}, 1e-9},
        {"Direct partial=0 ff=0 sel=0", StructureFactorModifier::tableIdentifier, 13, {8.787783888775856}, 1e-9},
        {"Direct partial=0 ff=0 sel=0", StructureFactorModifier::tableIdentifier, 31, {0.6839307239248916}, 1e-9},
        {"Direct partial=1 ff=0 sel=0", StructureFactorModifier::faberZimanTableIdentifier, 13, {8.46936266735919, 9.745195717539405, 5.90910390894458}, 1e-9},
        {"Direct partial=1 ff=0 sel=0", StructureFactorModifier::faberZimanTableIdentifier, 16, {0.6128395755736205, -1.2735688113283208, 4.274343895442401}, 1e-9},
        {"Debye partial=0 ff=0 sel=0", StructureFactorModifier::tableIdentifier, 0, {40.551334725240345}, 1e-4},
        {"Debye partial=0 ff=0 sel=0", StructureFactorModifier::tableIdentifier, 25, {1.0890085660530464}, 1e-4},
        {"Debye partial=0 ff=0 sel=0", StructureFactorModifier::tableIdentifier, 99, {1.3712303172516378}, 1e-4},
        {"Debye partial=1 ff=1 sel=0", StructureFactorModifier::tableIdentifier, 0, {11.251199078316677, 10.130867811474578, 8.816317643743224}, 1e-4},
        {"Debye partial=1 ff=1 sel=0", StructureFactorModifier::tableIdentifier, 50, {0.44692304519748355, 0.0018335863933644503, 0.6327490929221402}, 1e-4},
    };
    for(const Reference& ref : references) {
        const Curve* c = curve(QString::fromLatin1(ref.key), QString::fromLatin1(ref.table));
        if(!c || c->components != ref.values.size() || (ref.bin + 1) * c->components > c->values.size()) {
            fail(QStringLiteral("[%1] table %2 missing or has the wrong shape").arg(ref.key).arg(ref.table));
            continue;
        }
        for(size_t q = 0; q < ref.values.size(); q++) {
            const double v = c->values[ref.bin * c->components + q];
            if(!close(v, ref.values[q], ref.tol))
                fail(QStringLiteral("[%1] %2 bin %3 component %4: got %5, expected %6").arg(ref.key).arg(ref.table)
                    .arg(ref.bin).arg(q).arg(v, 0, 'g', 17).arg(ref.values[q], 0, 'g', 17));
        }
    }

    // Type pairs in output order: Cu-Cu, Cu-Au, Au-Au; concentrations of the full system.
    const double x[2] = {0.75, 0.25};
    const int pairA[3] = {0, 0, 1}, pairB[3] = {0, 1, 1};
    for(const char* mode : {"Direct", "Debye"}) {
        for(int ff : {0, 1}) {
            // (2) The total factor equals sum (2 - delta_ab) S_ab of the partial run.
            const Curve* total = curve(QStringLiteral("%1 partial=0 ff=%2 sel=0").arg(mode).arg(ff), sfId);
            const Curve* parts = curve(QStringLiteral("%1 partial=1 ff=%2 sel=0").arg(mode).arg(ff), sfId);
            if(!total || !parts || parts->components != 3 || parts->values.size() != 3 * total->values.size()) {
                fail(QStringLiteral("[%1 ff=%2] missing total or partial table").arg(mode).arg(ff));
                continue;
            }
            for(size_t bin = 0; bin < total->values.size(); bin++) {
                const double sum = parts->values[3 * bin] + 2 * parts->values[3 * bin + 1] + parts->values[3 * bin + 2];
                if(!close(total->values[bin], sum, 1e-10))
                    fail(QStringLiteral("[%1 ff=%2] bin %3: total %4 != sum of partials %5").arg(mode).arg(ff).arg(bin).arg(total->values[bin]).arg(sum));
            }
            // (3) Faber-Ziman relation A_ab = (S_ab - x_a delta_ab) / (x_a x_b) + 1.
            const Curve* fz = curve(QStringLiteral("%1 partial=1 ff=%2 sel=0").arg(mode).arg(ff), fzId);
            if(!fz || fz->values.size() != parts->values.size()) {
                fail(QStringLiteral("[%1 ff=%2] missing Faber-Ziman table").arg(mode).arg(ff));
                continue;
            }
            for(size_t i = 0; i < fz->values.size(); i++) {
                const int q = (int)(i % 3);
                const double expected = (parts->values[i] - (pairA[q] == pairB[q] ? x[pairA[q]] : 0.0)) / (x[pairA[q]] * x[pairB[q]]) + 1;
                if(!close(fz->values[i], expected, 1e-9))
                    fail(QStringLiteral("[%1 ff=%2] Faber-Ziman element %3: %4 != %5").arg(mode).arg(ff).arg(i).arg(fz->values[i]).arg(expected));
            }
        }

        // (4) Only Cu selected: a single species, so the X-ray weighting cancels (S^X = S). Deliberate difference
        // from OVITO Pro 3.16.1, which returns NaN for S when a defined type has no selected particles.
        const Curve* plain = curve(QStringLiteral("%1 partial=0 ff=0 sel=1").arg(mode), sfId);
        const Curve* xray = curve(QStringLiteral("%1 partial=0 ff=1 sel=1").arg(mode), sfId);
        if(!plain || !xray || plain->values.size() != xray->values.size())
            fail(QStringLiteral("[%1 sel=1] missing tables").arg(mode));
        else
            for(size_t bin = 0; bin < plain->values.size(); bin++)
                if(!close(xray->values[bin], plain->values[bin], 1e-10))
                    fail(QStringLiteral("[%1 sel=1] bin %2: S^X %3 != S %4").arg(mode).arg(bin).arg(xray->values[bin]).arg(plain->values[bin]));

        // (5) Pairs involving the empty species (Au): S_ab = 0 and A_ab undefined (NaN); the Cu-Cu pair is finite.
        const QString key = QStringLiteral("%1 partial=1 ff=1 sel=1").arg(mode);
        const Curve* sp = curve(key, sfId);
        const Curve* ap = curve(key, fzId);
        if(!sp || !ap || sp->components != 3 || ap->values.size() != sp->values.size())
            fail(QStringLiteral("[%1] missing tables").arg(key));
        else
            for(size_t bin = 0; bin < sp->values.size() / 3; bin++) {
                if(!std::isfinite(sp->values[3 * bin]) || !std::isfinite(ap->values[3 * bin]) || sp->values[3 * bin + 1] != 0 || sp->values[3 * bin + 2] != 0
                        || !std::isnan(ap->values[3 * bin + 1]) || !std::isnan(ap->values[3 * bin + 2])) {
                    fail(QStringLiteral("[%1] bin %2: unexpected values for the empty species").arg(key).arg(bin));
                    break;
                }
            }
    }

    qInfo().noquote() << (ok ? "Structure factor self-test: ALL PASSED" : "Structure factor self-test: FAILED");
    return ok;
}

}   // End of namespace
