////////////////////////////////////////////////////////////////////////////////////////
//
//  Copyright 2026 OVITO GmbH, Germany
//
//  This file is part of OVITO (Open Visualization Tool).
//
////////////////////////////////////////////////////////////////////////////////////////

#include <ovito/particles/Particles.h>
#include <ovito/particles/objects/Particles.h>
#include <ovito/particles/modifier/analysis/amorphous_carbon/AmorphousCarbonTrackingModifier.h>
#include <ovito/stdobj/simcell/SimulationCell.h>
#include <ovito/core/dataset/data/DataCollection.h>
#include <ovito/core/dataset/pipeline/StaticSource.h>
#include <ovito/core/dataset/pipeline/PipelineEvaluationRequest.h>
#include <ovito/core/dataset/scene/Pipeline.h>
#include <ovito/core/utilities/Exception.h>
#include <ovito/core/app/StandaloneApplication.h>
#include "AmorphousCarbonSelfTestService.h"

#include <QCommandLineParser>
#include <QCoreApplication>

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(AmorphousCarbonSelfTestService);

namespace {

const int32_t Sp3 = 3;      // "Tracked phase" hybridization value used throughout the scenario.
const int32_t Sp2 = 2;      // "Left the tracked phase" hybridization value.

/// One synthetic configuration (reference or current), built up atom by atom.
struct Frame {
    std::vector<int64_t> id;
    std::vector<int64_t> clusterId;
    std::vector<int64_t> clusterSize;
    std::vector<int32_t> clusterRank;   // Only populated/used for the reference frame.
    std::vector<int32_t> hybridization;
    std::vector<int32_t> relevant;

    size_t count() const { return id.size(); }
};

/// Wraps a Frame into a StaticSource-backed pipeline node, exactly the way a real "Amorphous
/// carbon cluster analysis" modifier's output would look to the tracking modifier downstream --
/// except every value below is chosen by hand instead of computed, so the expected result of
/// running AmorphousCarbonTrackingModifier over it is known before the test runs.
OORef<StaticSource> buildSource(const Frame& frame, bool includeRank)
{
    DataOORef<Particles> particles = DataOORef<Particles>::create(ObjectInitializationFlag::DontCreateVisElement);
    particles->setElementCount(frame.count());

    {
        BufferWriteAccess<Point3, access_mode::discard_write> posAcc = particles->createProperty(Particles::PositionProperty);
        for(size_t i = 0; i < frame.count(); ++i)
            posAcc[i] = Point3(FloatType(frame.id[i]), 0, 0);
    }
    {
        BufferWriteAccess<int64_t, access_mode::discard_write> idAcc = particles->createProperty(Particles::IdentifierProperty);
        for(size_t i = 0; i < frame.count(); ++i)
            idAcc[i] = frame.id[i];
    }
    {
        BufferWriteAccess<int64_t, access_mode::discard_write> cidAcc =
            particles->createProperty(DataBuffer::Uninitialized, QStringLiteral("Carbon Cluster Identifier"), Property::Int64, 1);
        for(size_t i = 0; i < frame.count(); ++i)
            cidAcc[i] = frame.clusterId[i];
    }
    {
        BufferWriteAccess<int64_t, access_mode::discard_write> sizeAcc =
            particles->createProperty(DataBuffer::Uninitialized, QStringLiteral("Carbon Cluster Size"), Property::Int64, 1);
        for(size_t i = 0; i < frame.count(); ++i)
            sizeAcc[i] = frame.clusterSize[i];
    }
    if(includeRank) {
        BufferWriteAccess<int32_t, access_mode::discard_write> rankAcc =
            particles->createProperty(DataBuffer::Uninitialized, QStringLiteral("Carbon Cluster Rank"), Property::Int32, 1);
        for(size_t i = 0; i < frame.count(); ++i)
            rankAcc[i] = frame.clusterRank[i];
    }
    {
        BufferWriteAccess<int32_t, access_mode::discard_write> hybAcc =
            particles->createProperty(DataBuffer::Uninitialized, QStringLiteral("Hybridization"), Property::Int32, 1);
        for(size_t i = 0; i < frame.count(); ++i)
            hybAcc[i] = frame.hybridization[i];
    }
    {
        BufferWriteAccess<int32_t, access_mode::discard_write> relAcc =
            particles->createProperty(DataBuffer::Uninitialized, QStringLiteral("Relevant Carbon Cluster"), Property::Int32, 1);
        for(size_t i = 0; i < frame.count(); ++i)
            relAcc[i] = frame.relevant[i];
    }

    DataOORef<DataCollection> data = DataOORef<DataCollection>::create();
    data->addObject(std::move(particles));
    data->addObject(DataOORef<SimulationCell>::create(ObjectInitializationFlag::DontCreateVisElement,
        Box3(Point3(0, 0, 0), Point3(100, 100, 100)), false, false, false));

    OORef<StaticSource> source = OORef<StaticSource>::create();
    source->setDataCollection(std::move(data));
    return source;
}

/// Accumulates pass/fail results and prints each check as it runs.
class Checker {
public:
    template<typename A, typename B>
    void equal(const QString& label, const A& got, const B& expected) {
        ++_total;
        const bool ok = (got == expected);
        if(!ok) ++_failed;
        qInfo().noquote() << (ok ? "  [PASS] " : "  [FAIL] ") << label
            << "-- got" << QVariant::fromValue(got) << "expected" << QVariant::fromValue(expected);
    }

    void approx(const QString& label, double got, double expected, double tol = 1e-9) {
        ++_total;
        const bool ok = std::abs(got - expected) <= tol;
        if(!ok) ++_failed;
        qInfo().noquote() << (ok ? "  [PASS] " : "  [FAIL] ") << label
            << "-- got" << got << "expected" << expected;
    }

    int total() const { return _total; }
    int failed() const { return _failed; }

private:
    int _total = 0;
    int _failed = 0;
};

}   // End of anonymous namespace

/******************************************************************************
* Registers the command-line options handled by this service.
******************************************************************************/
void AmorphousCarbonSelfTestService::registerCommandLineOptions(QCommandLineParser& cmdLineParser)
{
    cmdLineParser.addOption(QCommandLineOption(QStringList{QStringLiteral("carbon-track-selftest")},
        tr("Run the in-process regression test for AmorphousCarbonTrackingModifier and exit "
           "(0 on success, 1 on any failed check).")));
}

/******************************************************************************
* Runs the self-test after the application has entered the main event loop.
******************************************************************************/
void AmorphousCarbonSelfTestService::applicationStarted()
{
    const QCommandLineParser& parser = StandaloneApplication::instance()->cmdLineParser();
    if(!parser.isSet(QStringLiteral("carbon-track-selftest")))
        return;

    bool ok = false;
    try {
        ok = runSelfTest();
    }
    catch(const Exception& ex) {
        qCritical().noquote() << "Amorphous carbon self-test: FAILED with an exception:";
        for(const QString& msg : ex.messages())
            qCritical().noquote() << msg;
        ok = false;
    }
    catch(const std::exception& ex) {
        qCritical().noquote() << "Amorphous carbon self-test: FAILED with an exception:" << QString::fromLocal8Bit(ex.what());
        ok = false;
    }
    // In --nogui mode the Qt event loop is not running at this point, so QCoreApplication::exit()
    // has no effect and the process exits with 0 regardless. Throwing makes the startup code call
    // exitWithFatalError(), which shuts the application down and yields exit code 1.
    if(!ok)
        throw Exception(tr("Amorphous carbon self-test failed."));
    QCoreApplication::exit(0);
}

/******************************************************************************
* Builds the synthetic scenario, runs it through the real modifier, and checks
* every expected outcome.
******************************************************************************/
bool AmorphousCarbonSelfTestService::runSelfTest()
{
    // --- Build the synthetic scenario -------------------------------------------------
    //
    // Eight atom groups (A-H), each engineered by hand to land on one specific, known
    // classification. See AmorphousCarbonSelfTestService.h and AMORPHOUS_CARBON_MODIFIER.md,
    // "C++ self-test" for the full table this mirrors.
    //
    //   A (5 atoms):  intact cluster, unchanged           -> Stable / Stable
    //   B (12 atoms): the rank-1 (largest) reference cluster, split 6 (main fragment) / 4 (minor
    //                 fragment, later joined by C) / 1 (dispersed) / 1 (left phase)
    //                                                      -> Fragmented (ref side)
    //   C (3 atoms):  merges entirely into B's minor fragment
    //                                                      -> Stable (ref side) into a Merged cluster
    //   D (5 atoms):  untracked in the reference, newly clustered now
    //                                                      -> Created
    //   E (4 atoms):  leaves the tracked phase entirely (Cluster Fate = LeftPhase)
    //                                                      -> Disappeared
    //   F (2 atoms):  stays in the tracked phase but drops out of the clustered core
    //                 (Cluster Fate = LeftCore)            -> Disappeared
    //   G (2 atoms):  present in the reference only (vanished)
    //                                                      -> Disappeared, vanished_reference_atom_count
    //   H (3 atoms):  present in the current configuration only (unmapped)
    //                                                      -> Created, unmapped_atom_count
    Frame ref, cur;
    int64_t nextId = 1;

    auto pushRef = [&](int64_t id, int64_t cid, int64_t size, int32_t rank, int32_t hybrid, bool relevant) {
        ref.id.push_back(id);
        ref.clusterId.push_back(cid);
        ref.clusterSize.push_back(size);
        ref.clusterRank.push_back(rank);
        ref.hybridization.push_back(hybrid);
        ref.relevant.push_back(relevant ? 1 : 0);
    };
    auto pushCur = [&](int64_t id, int64_t cid, int64_t size, int32_t hybrid, bool relevant) {
        cur.id.push_back(id);
        cur.clusterId.push_back(cid);
        cur.clusterSize.push_back(size);
        cur.hybridization.push_back(hybrid);
        cur.relevant.push_back(relevant ? 1 : 0);
    };

    // Group A: Stable / Stable.
    std::vector<int64_t> idsA;
    for(int k = 0; k < 5; ++k) {
        int64_t id = nextId++;
        idsA.push_back(id);
        pushRef(id, /*cid*/ 1, /*size*/ 5, /*rank*/ 2, Sp3, true);
        pushCur(id, /*cid*/ 101, /*size*/ 5, Sp3, true);
    }

    // Group B: rank 1 (size 12, the largest relevant reference cluster) -> Fragmented.
    std::vector<int64_t> idsB_main, idsB_minor;
    int64_t idB_dispersed = 0, idB_leftPhase = 0;
    for(int k = 0; k < 6; ++k) {
        int64_t id = nextId++;
        idsB_main.push_back(id);
        pushRef(id, 2, 12, 1, Sp3, true);
        pushCur(id, /*cid*/ 102, /*size*/ 6, Sp3, true);
    }
    for(int k = 0; k < 4; ++k) {
        int64_t id = nextId++;
        idsB_minor.push_back(id);
        pushRef(id, 2, 12, 1, Sp3, true);
        pushCur(id, /*cid*/ 103, /*size*/ 7, Sp3, true);   // Size 7: 4 from B + 3 from C below.
    }
    {
        idB_dispersed = nextId++;
        pushRef(idB_dispersed, 2, 12, 1, Sp3, true);
        pushCur(idB_dispersed, /*cid*/ 106, /*size*/ 1, Sp3, false);   // Its own singleton cluster.
    }
    {
        idB_leftPhase = nextId++;
        pushRef(idB_leftPhase, 2, 12, 1, Sp3, true);
        pushCur(idB_leftPhase, /*cid*/ 0, /*size*/ 0, Sp2, false);   // Left the tracked phase.
    }

    // Group C: entirely joins B's minor fragment (cluster 103) -> Stable (ref side) feeding a Merged cluster.
    std::vector<int64_t> idsC;
    for(int k = 0; k < 3; ++k) {
        int64_t id = nextId++;
        idsC.push_back(id);
        pushRef(id, /*cid*/ 3, /*size*/ 3, /*rank*/ 3, Sp3, true);
        pushCur(id, /*cid*/ 103, /*size*/ 7, Sp3, true);
    }

    // Group D: untracked in the reference (Carbon Cluster Identifier == 0), newly clustered now -> Created.
    std::vector<int64_t> idsD;
    for(int k = 0; k < 5; ++k) {
        int64_t id = nextId++;
        idsD.push_back(id);
        pushRef(id, /*cid*/ 0, /*size*/ 0, /*rank*/ 0, Sp2, false);
        pushCur(id, /*cid*/ 104, /*size*/ 5, Sp3, true);
    }

    // Group E: leaves the tracked phase entirely -> Disappeared (Cluster Fate = LeftPhase).
    std::vector<int64_t> idsE;
    for(int k = 0; k < 4; ++k) {
        int64_t id = nextId++;
        idsE.push_back(id);
        pushRef(id, /*cid*/ 4, /*size*/ 4, /*rank*/ 4, Sp3, true);
        pushCur(id, /*cid*/ 0, /*size*/ 0, Sp2, false);
    }

    // Group F: still in the tracked phase but no longer clustered -> Disappeared (Cluster Fate = LeftCore).
    std::vector<int64_t> idsF;
    for(int k = 0; k < 2; ++k) {
        int64_t id = nextId++;
        idsF.push_back(id);
        pushRef(id, /*cid*/ 5, /*size*/ 2, /*rank*/ 5, Sp3, true);
        pushCur(id, /*cid*/ 0, /*size*/ 0, Sp3, false);   // Same hybridization as reference => LeftCore, not LeftPhase.
    }

    // Group G: present in the reference only -> Disappeared, vanished_reference_atom_count.
    std::vector<int64_t> idsG;
    for(int k = 0; k < 2; ++k) {
        int64_t id = nextId++;
        idsG.push_back(id);
        pushRef(id, /*cid*/ 6, /*size*/ 2, /*rank*/ 6, Sp3, true);
        // No corresponding entry in `cur`.
    }

    // Group H: present in the current configuration only -> Created, unmapped_atom_count.
    std::vector<int64_t> idsH;
    for(int k = 0; k < 3; ++k) {
        int64_t id = nextId++;
        idsH.push_back(id);
        pushCur(id, /*cid*/ 105, /*size*/ 3, Sp3, true);
        // No corresponding entry in `ref`.
    }

    // --- Run the real modifier through a real pipeline --------------------------------
    OORef<StaticSource> refSource = buildSource(ref, /*includeRank*/ true);
    OORef<StaticSource> curSource = buildSource(cur, /*includeRank*/ false);

    OORef<AmorphousCarbonTrackingModifier> trackMod = OORef<AmorphousCarbonTrackingModifier>::create();
    trackMod->setColorMode(AmorphousCarbonTrackingModifier::NoColoring);
    trackMod->setGenealogyOverlapFraction(0.10);
    trackMod->setUseReferenceFrameOffset(false);
    trackMod->setReferenceFrameNumber(0);
    trackMod->setReferenceConfiguration(refSource);

    OORef<Pipeline> pipeline = OORef<Pipeline>::create();
    pipeline->setHead(curSource);
    pipeline->applyModifier(AnimationTime::fromFrame(0), false, trackMod);

    PipelineFlowState state = pipeline->evaluatePipeline(PipelineEvaluationRequest(AnimationTime::fromFrame(0), true, false)).blockForResult();
    if(state.status().type() == PipelineStatus::Error)
        throw Exception(tr("Pipeline evaluation failed: %1").arg(state.status().text()));

    const Particles* outParticles = state.getObject<Particles>();
    if(!outParticles)
        throw Exception(tr("Pipeline produced no Particles object."));
    const Property* fateProp = outParticles->getProperty(QStringLiteral("Cluster Fate"));
    const Property* refGenProp = outParticles->getProperty(QStringLiteral("Reference Cluster Genealogy"));
    const Property* curGenProp = outParticles->getProperty(QStringLiteral("Cluster Genealogy"));
    if(!fateProp || !refGenProp || !curGenProp)
        throw Exception(tr("Pipeline output is missing Cluster Fate / *Genealogy properties."));

    BufferReadAccess<int64_t> outId(outParticles->expectProperty(Particles::IdentifierProperty));
    BufferReadAccess<int32_t> outFate(fateProp);
    BufferReadAccess<int32_t> outRefGen(refGenProp);
    BufferReadAccess<int32_t> outCurGen(curGenProp);

    std::unordered_map<int64_t, size_t> indexById;
    for(size_t i = 0; i < outId.size(); ++i)
        indexById.emplace(outId[i], i);
    auto idx = [&](int64_t id) -> size_t {
        auto it = indexById.find(id);
        if(it == indexById.end())
            throw Exception(tr("Test setup error: atom id %1 missing from pipeline output.").arg(id));
        return it->second;
    };

    auto attr = [&](const QString& name) { return state.getAttributeValue(name, QVariant()); };

    // --- Checks -------------------------------------------------------------------------
    Checker c;
    qInfo().noquote() << "Amorphous carbon self-test: running checks...";

    using Fate = AmorphousCarbonTrackingModifier::ClusterFate;
    using RefGen = AmorphousCarbonTrackingModifier::ReferenceClusterGenealogy;
    using Gen = AmorphousCarbonTrackingModifier::ClusterGenealogy;

    // Per-atom spot checks: one representative atom per outcome, chosen to also exercise the
    // asymmetric bidirectional case (group C is Stable on the reference side while feeding a
    // current cluster that is Merged) and the "Created" case an atom-fate-only view cannot show
    // (group D atoms read as harmless FateNotTracked yet their current cluster is genuinely new).
    c.equal("A (id 1): Cluster Fate == MainFragment", outFate[idx(idsA[0])], (int32_t)Fate::FateMainFragment);
    c.equal("A (id 1): Reference Cluster Genealogy == Stable", outRefGen[idx(idsA[0])], (int32_t)RefGen::RefGenealogyStable);
    c.equal("A (id 1): Cluster Genealogy == Stable", outCurGen[idx(idsA[0])], (int32_t)Gen::GenealogyStable);

    c.equal("B main (id 6): Cluster Fate == MainFragment", outFate[idx(idsB_main[0])], (int32_t)Fate::FateMainFragment);
    c.equal("B main (id 6): Reference Cluster Genealogy == Fragmented", outRefGen[idx(idsB_main[0])], (int32_t)RefGen::RefGenealogyFragmented);
    c.equal("B main (id 6): Cluster Genealogy == Stable", outCurGen[idx(idsB_main[0])], (int32_t)Gen::GenealogyStable);

    c.equal("B minor (id 12): Cluster Fate == MinorFragment", outFate[idx(idsB_minor[0])], (int32_t)Fate::FateMinorFragment);
    c.equal("B minor (id 12): Reference Cluster Genealogy == Fragmented", outRefGen[idx(idsB_minor[0])], (int32_t)RefGen::RefGenealogyFragmented);
    c.equal("B minor (id 12): Cluster Genealogy == Merged", outCurGen[idx(idsB_minor[0])], (int32_t)Gen::GenealogyMerged);

    c.equal("B dispersed: Cluster Fate == Dispersed", outFate[idx(idB_dispersed)], (int32_t)Fate::FateDispersed);
    c.equal("B dispersed: Reference Cluster Genealogy == Fragmented (broadcast from its ref cluster)", outRefGen[idx(idB_dispersed)], (int32_t)RefGen::RefGenealogyFragmented);
    c.equal("B dispersed: Cluster Genealogy == NotClustered", outCurGen[idx(idB_dispersed)], (int32_t)Gen::GenealogyNotClustered);

    c.equal("B left-phase: Cluster Fate == LeftPhase", outFate[idx(idB_leftPhase)], (int32_t)Fate::FateLeftPhase);
    c.equal("B left-phase: Cluster Genealogy == NotClustered", outCurGen[idx(idB_leftPhase)], (int32_t)Gen::GenealogyNotClustered);

    c.equal("C (id 19): Cluster Fate == MainFragment (its own only destination)", outFate[idx(idsC[0])], (int32_t)Fate::FateMainFragment);
    c.equal("C (id 19): Reference Cluster Genealogy == Stable (single continuation)", outRefGen[idx(idsC[0])], (int32_t)RefGen::RefGenealogyStable);
    c.equal("C (id 19): Cluster Genealogy == Merged (its target absorbed B too)", outCurGen[idx(idsC[0])], (int32_t)Gen::GenealogyMerged);

    c.equal("D (id 22): Cluster Fate == NotTracked", outFate[idx(idsD[0])], (int32_t)Fate::FateNotTracked);
    c.equal("D (id 22): Reference Cluster Genealogy == NotTracked", outRefGen[idx(idsD[0])], (int32_t)RefGen::RefGenealogyNotTracked);
    c.equal("D (id 22): Cluster Genealogy == Created", outCurGen[idx(idsD[0])], (int32_t)Gen::GenealogyCreated);

    c.equal("E (id 27): Cluster Fate == LeftPhase", outFate[idx(idsE[0])], (int32_t)Fate::FateLeftPhase);
    c.equal("E (id 27): Reference Cluster Genealogy == Disappeared", outRefGen[idx(idsE[0])], (int32_t)RefGen::RefGenealogyDisappeared);

    c.equal("F (id 31): Cluster Fate == LeftCore", outFate[idx(idsF[0])], (int32_t)Fate::FateLeftCore);
    c.equal("F (id 31): Reference Cluster Genealogy == Disappeared", outRefGen[idx(idsF[0])], (int32_t)RefGen::RefGenealogyDisappeared);

    c.equal("H (id 34): Cluster Fate == NotTracked (unmapped)", outFate[idx(idsH[0])], (int32_t)Fate::FateNotTracked);
    c.equal("H (id 34): Reference Cluster Genealogy == NotTracked", outRefGen[idx(idsH[0])], (int32_t)RefGen::RefGenealogyNotTracked);
    c.equal("H (id 34): Cluster Genealogy == Created", outCurGen[idx(idsH[0])], (int32_t)Gen::GenealogyCreated);

    // Rank-1 (largest relevant reference cluster = B, size 12) aggregate fractions.
    c.equal("rank-1 target: reference_largest_cluster_size", attr(QStringLiteral("AmorphousCarbonTracking.reference_largest_cluster_size")).toLongLong(), (int64_t)12);
    c.approx("rank-1 target: retained_fraction (11/12)", attr(QStringLiteral("AmorphousCarbonTracking.retained_fraction")).toDouble(), 11.0 / 12.0);
    c.approx("rank-1 target: main_fragment_fraction (6/12)", attr(QStringLiteral("AmorphousCarbonTracking.main_fragment_fraction")).toDouble(), 0.5);
    c.approx("rank-1 target: left_phase_fraction (1/12)", attr(QStringLiteral("AmorphousCarbonTracking.left_phase_fraction")).toDouble(), 1.0 / 12.0);
    c.approx("rank-1 target: left_core_fraction (0, F isn't the target cluster)", attr(QStringLiteral("AmorphousCarbonTracking.left_core_fraction")).toDouble(), 0.0);
    c.approx("rank-1 target: dispersed_fraction (1/12)", attr(QStringLiteral("AmorphousCarbonTracking.dispersed_fraction")).toDouble(), 1.0 / 12.0);
    c.approx("rank-1 target: vanished_fraction (0, G isn't the target cluster)", attr(QStringLiteral("AmorphousCarbonTracking.vanished_fraction")).toDouble(), 0.0);
    c.equal("rank-1 target: fragment_count (102 and 103)", attr(QStringLiteral("AmorphousCarbonTracking.fragment_count")).toLongLong(), (int64_t)2);
    c.equal("unmapped_atom_count (group H)", attr(QStringLiteral("AmorphousCarbonTracking.unmapped_atom_count")).toLongLong(), (int64_t)3);
    c.equal("vanished_reference_atom_count (group G)", attr(QStringLiteral("AmorphousCarbonTracking.vanished_reference_atom_count")).toLongLong(), (int64_t)2);

    // Aggregates pooled over every relevant reference cluster: A(5) + B(12) + C(3) + E(4) + F(2) +
    // G(2) = 6 clusters, 28 atoms. Group D is excluded because it has no reference cluster at all,
    // and B's dispersed atom is relevant-by-inheritance from B even though its own singleton
    // current cluster is not.
    //   retained  = A 5 + B 11 (6 main + 4 minor + 1 dispersed) + C 3          = 19
    //   left phase = B 1 + E 4                                                 =  5
    //   left core  = F 2                                                       =  2
    //   vanished   = G 2                                                       =  2   (sums to 28)
    //   main frag  = A 5 + B 6 + C 3                                           = 14
    //   fragments  = A 1 (101) + B 2 (102, 103) + C 1 (103); E/F/G contribute 0 =  4
    c.equal("pooled: relevant_reference_atom_count", attr(QStringLiteral("AmorphousCarbonTracking.relevant_reference_atom_count")).toLongLong(), (int64_t)28);
    c.approx("pooled: relevant_retained_fraction (19/28)", attr(QStringLiteral("AmorphousCarbonTracking.relevant_retained_fraction")).toDouble(), 19.0 / 28.0);
    c.equal("pooled: relevant_main_fragment_size", attr(QStringLiteral("AmorphousCarbonTracking.relevant_main_fragment_size")).toLongLong(), (int64_t)14);
    c.approx("pooled: relevant_main_fragment_fraction (14/28)", attr(QStringLiteral("AmorphousCarbonTracking.relevant_main_fragment_fraction")).toDouble(), 0.5);
    c.approx("pooled: relevant_left_phase_fraction (5/28)", attr(QStringLiteral("AmorphousCarbonTracking.relevant_left_phase_fraction")).toDouble(), 5.0 / 28.0);
    c.approx("pooled: relevant_left_core_fraction (2/28)", attr(QStringLiteral("AmorphousCarbonTracking.relevant_left_core_fraction")).toDouble(), 2.0 / 28.0);
    c.approx("pooled: relevant_dispersed_fraction (1/28)", attr(QStringLiteral("AmorphousCarbonTracking.relevant_dispersed_fraction")).toDouble(), 1.0 / 28.0);
    c.approx("pooled: relevant_vanished_fraction (2/28)", attr(QStringLiteral("AmorphousCarbonTracking.relevant_vanished_fraction")).toDouble(), 2.0 / 28.0);
    c.equal("pooled: relevant_fragment_count", attr(QStringLiteral("AmorphousCarbonTracking.relevant_fragment_count")).toLongLong(), (int64_t)4);
    c.approx("pooled: relevant_mean_fragments_per_cluster (4/6)", attr(QStringLiteral("AmorphousCarbonTracking.relevant_mean_fragments_per_cluster")).toDouble(), 4.0 / 6.0);
    // The invariant the pooled denominator exists to preserve, checked directly rather than assumed.
    c.approx("pooled: retained + left_phase + left_core + vanished == 1",
        attr(QStringLiteral("AmorphousCarbonTracking.relevant_retained_fraction")).toDouble()
            + attr(QStringLiteral("AmorphousCarbonTracking.relevant_left_phase_fraction")).toDouble()
            + attr(QStringLiteral("AmorphousCarbonTracking.relevant_left_core_fraction")).toDouble()
            + attr(QStringLiteral("AmorphousCarbonTracking.relevant_vanished_fraction")).toDouble(),
        1.0);

    // Bidirectional genealogy aggregates, over every relevant cluster on both sides:
    //   reference: A=Stable, B=Fragmented, C=Stable, E=Disappeared, F=Disappeared, G=Disappeared
    //              -> 6 relevant, 2 stable, 1 fragmented, 3 disappeared
    //   current:   101=Stable, 102=Stable, 103=Merged, 104=Created, 105=Created
    //              -> 5 relevant, 2 stable, 1 merged, 2 created
    c.equal("genealogy: reference_clusters_relevant", attr(QStringLiteral("AmorphousCarbonTracking.reference_clusters_relevant")).toLongLong(), (int64_t)6);
    c.equal("genealogy: reference_clusters_stable", attr(QStringLiteral("AmorphousCarbonTracking.reference_clusters_stable")).toLongLong(), (int64_t)2);
    c.equal("genealogy: reference_clusters_fragmented", attr(QStringLiteral("AmorphousCarbonTracking.reference_clusters_fragmented")).toLongLong(), (int64_t)1);
    c.equal("genealogy: reference_clusters_disappeared", attr(QStringLiteral("AmorphousCarbonTracking.reference_clusters_disappeared")).toLongLong(), (int64_t)3);
    c.equal("genealogy: current_clusters_relevant", attr(QStringLiteral("AmorphousCarbonTracking.current_clusters_relevant")).toLongLong(), (int64_t)5);
    c.equal("genealogy: current_clusters_stable", attr(QStringLiteral("AmorphousCarbonTracking.current_clusters_stable")).toLongLong(), (int64_t)2);
    c.equal("genealogy: current_clusters_merged", attr(QStringLiteral("AmorphousCarbonTracking.current_clusters_merged")).toLongLong(), (int64_t)1);
    c.equal("genealogy: current_clusters_created", attr(QStringLiteral("AmorphousCarbonTracking.current_clusters_created")).toLongLong(), (int64_t)2);

    qInfo().noquote() << QStringLiteral("Amorphous carbon self-test: %1/%2 checks passed.").arg(c.total() - c.failed()).arg(c.total());
    return c.failed() == 0;
}

}   // End of namespace
