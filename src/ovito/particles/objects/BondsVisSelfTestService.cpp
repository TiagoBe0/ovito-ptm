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
#include <ovito/particles/objects/Particles.h>
#include <ovito/particles/objects/Bonds.h>
#include <ovito/particles/objects/BondsVis.h>
#include <ovito/stdobj/simcell/SimulationCell.h>
#include <ovito/core/app/StandaloneApplication.h>
#include <ovito/core/dataset/pipeline/StaticSource.h>
#include <ovito/core/dataset/pipeline/PipelineEvaluationRequest.h>
#include <ovito/core/dataset/scene/Pipeline.h>
#include <ovito/core/dataset/scene/SceneNode.h>
#include <ovito/core/rendering/FrameGraph.h>
#include <ovito/core/rendering/CylinderPrimitive.h>
#include <ovito/core/rendering/RendererResourceCache.h>
#include <ovito/core/utilities/Exception.h>
#include "BondsVisSelfTestService.h"
#include <QCommandLineParser>
#include <QCoreApplication>

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(BondsVisSelfTestService);

namespace {

/// Regular hexagon with side 1.4 in the xy plane, centered at (0.7, 1.2124, 0). Bond i connects atoms i and i+1.
const std::vector<Point3> ringPositions = {
    {0, 0, 0}, {1.4, 0, 0}, {2.1, 1.212435565, 0}, {1.4, 2.42487113, 0}, {0, 2.42487113, 0}, {-0.7, 1.212435565, 0}};
const Point3 ringCenter(0.7, 1.212435565, 0);
const std::vector<double> ringOrders = {1, 2, 3, 1.5, 0, 1.5};

/// One rendered cylinder.
struct Cylinder {
    Point3 base, head;
    double width;
    size_t bond;
    double length() const { return (head - base).length(); }
    Point3 middle() const { return base + (head - base) * 0.5; }
};

DataOORef<DataCollection> buildRing(const std::vector<double>& orders)
{
    DataOORef<Particles> particles = DataOORef<Particles>::create(ObjectInitializationFlag::DontCreateVisElement);
    particles->setElementCount(ringPositions.size());
    {
        BufferWriteAccess<Point3, access_mode::discard_write> pos = particles->createProperty(Particles::PositionProperty);
        for(size_t i = 0; i < ringPositions.size(); i++) pos[i] = ringPositions[i];
    }
    DataOORef<Bonds> bonds = DataOORef<Bonds>::create(ObjectInitializationFlag::DontCreateVisElement);
    bonds->setElementCount(ringPositions.size());
    {
        BufferWriteAccess<ParticleIndexPair, access_mode::discard_write> topology = bonds->createProperty(Bonds::TopologyProperty);
        for(size_t i = 0; i < ringPositions.size(); i++) topology[i] = {(int64_t)i, (int64_t)((i + 1) % ringPositions.size())};
        BufferWriteAccess<FloatType, access_mode::discard_write> order = bonds->createProperty(DataBuffer::Uninitialized, QString::fromLatin1(BondsVis::bondOrderPropertyName), Property::FloatDefault, 1);
        for(size_t i = 0; i < orders.size(); i++) order[i] = orders[i];
    }
    particles->setBonds(std::move(bonds));
    DataOORef<DataCollection> data = DataOORef<DataCollection>::create();
    data->addObject(std::move(particles));
    data->addObject(DataOORef<SimulationCell>::create(ObjectInitializationFlag::DontCreateVisElement,
        Box3(Point3(-5, -5, -5), Point3(10, 10, 10)), false, false, false));
    return data;
}

/// Renders the bonds of the given data with the given vis element and returns the cylinders and the pick info.
std::vector<Cylinder> renderBonds(DataOORef<DataCollection> data, BondsVis* vis, OORef<BondPickInfo>& pickInfo)
{
    OORef<StaticSource> source = OORef<StaticSource>::create();
    source->setDataCollection(std::move(data));
    OORef<Pipeline> pipeline = OORef<Pipeline>::create();
    pipeline->setHead(source);
    PipelineFlowState state = pipeline->evaluatePipeline(PipelineEvaluationRequest(AnimationTime::fromFrame(0), true, false)).blockForResult();
    const Particles* particles = state.expectObject<Particles>();
    ConstDataObjectPath path{particles, particles->bonds()};

    auto cache = std::make_shared<RendererResourceCache>();
    std::vector<Cylinder> result;
    {
        OORef<FrameGraph> frameGraph = OORef<FrameGraph>::create(cache->acquireResourceFrame(), AnimationTime::fromFrame(0),
            ViewProjectionParameters{}, QSize(100, 100), false, false, false, QImage::Format_ARGB32, 1.0);
        OORef<SceneNode> sceneNode = OORef<SceneNode>::create();
        sceneNode->setPipeline(pipeline);
        (void) vis->render(path, state, *frameGraph, sceneNode);
        for(const auto& group : frameGraph->commandGroups()) {
            for(const auto& command : group.commands()) {
                const CylinderPrimitive* cylinders = dynamic_cast<const CylinderPrimitive*>(command.primitive());
                if(!cylinders) continue;
                pickInfo = dynamic_object_cast<BondPickInfo>(command.pickInfo());
                BufferReadAccess<Point3G> base(cylinders->basePositions());
                BufferReadAccess<Point3G> head(cylinders->headPositions());
                BufferReadAccess<GraphicsFloatType> widths(cylinders->widths());
                for(size_t i = 0; i < base.size(); i++) {
                    result.push_back({base[i].toDataType<FloatType>(), head[i].toDataType<FloatType>(),
                        widths ? (double)widths[i] : (double)cylinders->uniformWidth(),
                        pickInfo ? pickInfo->bondIndexFromSubObjectID((uint32_t)i) : std::numeric_limits<size_t>::max()});
                }
            }
        }
    }
    return result;
}

bool check(const QString& name, bool ok, const QString& detail)
{
    if(ok)
        qInfo().noquote() << "  PASS" << name << ":" << detail;
    else
        qCritical().noquote() << "  FAIL" << name << ":" << detail;
    return ok;
}

bool approxEqual(double a, double b) { return std::abs(a - b) < 1e-5; }

std::vector<const Cylinder*> ofBond(const std::vector<Cylinder>& cylinders, size_t bond)
{
    std::vector<const Cylinder*> list;
    for(const Cylinder& c : cylinders) if(c.bond == bond) list.push_back(&c);
    return list;
}

/// Distance between the axes of two parallel cylinders of a bond (measured between their midpoints).
double separation(const Cylinder& a, const Cylinder& b) { return (a.middle() - b.middle()).length(); }

}   // End of anonymous namespace

/******************************************************************************
* Registers the command-line options handled by this service.
******************************************************************************/
void BondsVisSelfTestService::registerCommandLineOptions(QCommandLineParser& cmdLineParser)
{
    cmdLineParser.addOption(QCommandLineOption(QStringList{QStringLiteral("bonds-vis-selftest")},
        tr("Run the in-process regression test for the bond order rendering of the Bonds visual element and exit "
           "(0 on success, 1 on any failed check).")));
}

/******************************************************************************
* Runs the self-test after the application has entered the main event loop.
******************************************************************************/
void BondsVisSelfTestService::applicationStarted()
{
    const QCommandLineParser& parser = StandaloneApplication::instance()->cmdLineParser();
    if(!parser.isSet(QStringLiteral("bonds-vis-selftest")))
        return;

    bool ok = false;
    try {
        ok = runTest();
    }
    catch(const Exception& ex) {
        qCritical().noquote() << "Bonds vis self-test: FAILED with an exception:";
        for(const QString& msg : ex.messages())
            qCritical().noquote() << msg;
        ok = false;
    }
    catch(const std::exception& ex) {
        qCritical().noquote() << "Bonds vis self-test: FAILED with an exception:" << QString::fromLocal8Bit(ex.what());
        ok = false;
    }
    // In --nogui mode QCoreApplication::exit() has no effect; throwing yields exit code 1.
    if(!ok)
        throw Exception(tr("Bonds vis self-test failed."));
    QCoreApplication::exit(0);
}

/******************************************************************************
* Runs all checks.
******************************************************************************/
bool BondsVisSelfTestService::runTest()
{
    bool ok = true;
    const double w = 0.4;   // Default bond width.

    qInfo().noquote() << "Bonds vis self-test: hexagon with bond orders 1, 2, 3, 1.5, 0, 1.5 (4 filled segments, fraction 0.5)";
    {
        OORef<BondsVis> vis = OORef<BondsVis>::create();
        vis->setBondWidth(w);
        vis->setFilledSegments(4);
        vis->setFilledFraction(0.5);
        OORef<BondPickInfo> pickInfo;
        std::vector<Cylinder> cylinders = renderBonds(buildRing(ringOrders), vis, pickInfo);

        QStringList counts;
        std::vector<size_t> perBond;
        for(size_t b = 0; b < ringOrders.size(); b++) {
            perBond.push_back(ofBond(cylinders, b).size());
            counts << QString::number(perBond.back());
        }
        ok &= check(QStringLiteral("cylinders per bond (picking maps every cylinder to its bond)"),
            cylinders.size() == 24 && perBond == std::vector<size_t>{2, 4, 6, 6, 0, 6}, QStringLiteral("%1 total, per bond %2").arg(cylinders.size()).arg(counts.join(' ')));

        // Double bond: two pairs of half-bonds of width 0.8 w, axes 2 * 0.5 * 1.25 * 0.8 w apart.
        auto dbl = ofBond(cylinders, 1);
        bool dblOk = dbl.size() == 4;
        for(auto c : dbl) dblOk &= approxEqual(c->width, 0.8 * w);
        if(dblOk) dblOk &= approxEqual(separation(*dbl[0], *dbl[2]), 1.25 * 0.8 * w);
        ok &= check(QStringLiteral("double bond: width 0.8 w, axes 1.25 * 0.8 w apart"), dblOk,
            dbl.size() == 4 ? QStringLiteral("width %1, separation %2").arg(dbl[0]->width).arg(separation(*dbl[0], *dbl[2])) : QStringLiteral("wrong count"));

        // Triple bond: width 0.63 w, central cylinder on the axis, outer ones 0.5 * 2.5 * 0.63 w away.
        auto tri = ofBond(cylinders, 2);
        bool triOk = tri.size() == 6;
        for(auto c : tri) triOk &= approxEqual(c->width, 0.63 * w);
        if(triOk) triOk &= approxEqual(separation(*tri[0], *tri[2]), 0.5 * 2.5 * 0.63 * w) && approxEqual(separation(*tri[0], *tri[4]), 0.5 * 2.5 * 0.63 * w)
            && approxEqual(separation(*tri[2], *tri[4]), 2.5 * 0.63 * w);
        ok &= check(QStringLiteral("triple bond: width 0.63 w, spacing 0.5 * 2.5 * 0.63 w"), triOk,
            tri.size() == 6 ? QStringLiteral("width %1, outer separation %2").arg(tri[0]->width).arg(separation(*tri[2], *tri[4])) : QStringLiteral("wrong count"));

        // Bond order 1.5: one solid pair plus four dashed segments with a total length of 0.5 * 1.4, closer to the ring center.
        auto frac = ofBond(cylinders, 3);
        if(frac.size() == 6) {
            double dashedLength = 0, solidDist = 0, dashedDist = 0;
            for(size_t i = 0; i < 6; i++) {
                const double d = (frac[i]->middle() - ringCenter).length();
                if(i < 2) { solidDist += d / 2; }
                else { dashedLength += frac[i]->length(); dashedDist += d / 4; }
            }
            ok &= check(QStringLiteral("bond order 1.5: dashed length = 0.5 * bond length"), approxEqual(dashedLength, 0.7), QString::number(dashedLength));
            ok &= check(QStringLiteral("bond order 1.5: dashed cylinders inside the ring"), dashedDist < solidDist,
                QStringLiteral("mean distance to center: dashed %1, solid %2").arg(dashedDist).arg(solidDist));
        }
        else ok &= check(QStringLiteral("bond order 1.5"), false, QStringLiteral("wrong count"));

        ok &= check(QStringLiteral("pick info maps sub-object 23 to bond 5"), pickInfo && pickInfo->bondIndexFromSubObjectID(23) == 5,
            pickInfo ? QString::number(pickInfo->bondIndexFromSubObjectID(23)) : QStringLiteral("no pick info"));
    }

    qInfo().noquote() << "Bonds vis self-test: 3 filled segments (central segment split, 4 cylinders per dashed bond)";
    {
        OORef<BondsVis> vis = OORef<BondsVis>::create();
        vis->setBondWidth(w);
        vis->setFilledSegments(3);
        vis->setFilledFraction(0.5);
        OORef<BondPickInfo> pickInfo;
        std::vector<Cylinder> cylinders = renderBonds(buildRing(ringOrders), vis, pickInfo);
        auto frac = ofBond(cylinders, 3);
        double dashedLength = 0;
        for(size_t i = 2; i < frac.size(); i++) dashedLength += frac[i]->length();
        ok &= check(QStringLiteral("cylinders"), cylinders.size() == 24 && frac.size() == 6 && approxEqual(dashedLength, 0.7),
            QStringLiteral("%1 total, %2 for bond 3, dashed length %3").arg(cylinders.size()).arg(frac.size()).arg(dashedLength));
    }

    qInfo().noquote() << "Bonds vis self-test: plain rendering (option off, or all bonds single)";
    for(int variant = 0; variant < 2; variant++) {
        OORef<BondsVis> vis = OORef<BondsVis>::create();
        vis->setBondWidth(w);
        vis->setVisualizeBondOrder(variant != 0);
        OORef<BondPickInfo> pickInfo;
        std::vector<Cylinder> cylinders = renderBonds(buildRing(variant == 0 ? ringOrders : std::vector<double>(6, 1.0)), vis, pickInfo);
        bool plain = cylinders.size() == 12;
        for(size_t i = 0; i < cylinders.size(); i++) plain &= cylinders[i].bond == i / 2 && approxEqual(cylinders[i].width, w);
        ok &= check(variant == 0 ? QStringLiteral("visualizeBondOrder = false") : QStringLiteral("all bond orders 1"), plain,
            QStringLiteral("%1 cylinders").arg(cylinders.size()));
    }

    qInfo().noquote() << "Bonds vis self-test: bonds added to existing ones with a Bond Order property are single bonds";
    {
        DataOORef<DataCollection> data = buildRing(ringOrders);
        Particles* particles = data->expectMutableObject<Particles>();
        Bonds* bonds = particles->makeBondsMutable();
        const size_t added = bonds->addBonds({Bond{0, 3, Vector3I::Zero()}}, nullptr, particles, {}, {});
        std::vector<FloatType> orders(bonds->elementCount());
        bonds->getProperty(QString::fromLatin1(BondsVis::bondOrderPropertyName))->copyComponentTo(orders.begin(), 0);
        ok &= check(QStringLiteral("new bond order"), added == 1 && orders.size() == 7 && orders[6] == 1 && orders[1] == 2,
            QStringLiteral("added %1, orders[6] = %2, orders[1] = %3").arg(added).arg(orders.back()).arg(orders.size() > 1 ? orders[1] : -1));
    }

    qInfo().noquote() << "Bonds vis self-test: bonds added to an emptied Bonds object (Discard existing bonds) are single bonds";
    {
        DataOORef<DataCollection> data = buildRing(ringOrders);
        Particles* particles = data->expectMutableObject<Particles>();
        Bonds* bonds = particles->makeBondsMutable();
        bonds->setElementCount(0);   // What CreateBondsModifier does for discardExistingBonds.
        const size_t added = bonds->addBonds({Bond{0, 3, Vector3I::Zero()}}, nullptr, particles, {}, {});
        std::vector<FloatType> orders(bonds->elementCount());
        bonds->getProperty(QString::fromLatin1(BondsVis::bondOrderPropertyName))->copyComponentTo(orders.begin(), 0);
        ok &= check(QStringLiteral("order after discard"), added == 1 && orders.size() == 1 && orders[0] == 1,
            QStringLiteral("orders[0] = %1").arg(orders.empty() ? -1 : orders[0]));
    }

    qInfo().noquote() << (ok ? "Bonds vis self-test: ALL PASSED" : "Bonds vis self-test: FAILURES");
    return ok;
}

}   // End of namespace
