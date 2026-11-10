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
#include <ovito/particles/util/NearestNeighborFinder.h>
#include <ovito/particles/util/CutoffNeighborFinder.h>
#include <ovito/particles/objects/Particles.h>
#include <ovito/stdobj/simcell/SimulationCell.h>
#include <ovito/delaunay/DelaunayTessellation.h>
#include <ovito/core/dataset/pipeline/ModificationNode.h>
#include <ovito/core/utilities/units/UnitsManager.h>
#include "DelaunayVacancyModifier.h"

#include <array>
#include <set>

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(DelaunayVacancyModifier);
OVITO_CLASSINFO(DelaunayVacancyModifier, "DisplayName", "Vacancy analysis (Delaunay)");
OVITO_CLASSINFO(DelaunayVacancyModifier, "Description", "Detect vacancies without a reference configuration from Delaunay voids and a locally fitted lattice.");
OVITO_CLASSINFO(DelaunayVacancyModifier, "ModifierCategory", "Analysis");
DEFINE_PROPERTY_FIELD(DelaunayVacancyModifier, lattice);
DEFINE_PROPERTY_FIELD(DelaunayVacancyModifier, latticeConstant);
DEFINE_PROPERTY_FIELD(DelaunayVacancyModifier, refineWithLocalLattice);
DEFINE_PROPERTY_FIELD(DelaunayVacancyModifier, outputVacancySites);
SET_PROPERTY_FIELD_LABEL(DelaunayVacancyModifier, lattice, "Lattice");
SET_PROPERTY_FIELD_LABEL(DelaunayVacancyModifier, latticeConstant, "Lattice constant (0 = auto)");
SET_PROPERTY_FIELD_LABEL(DelaunayVacancyModifier, refineWithLocalLattice, "Separate vacancy clusters (local lattice fit)");
SET_PROPERTY_FIELD_LABEL(DelaunayVacancyModifier, outputVacancySites, "Output vacancy sites");
SET_PROPERTY_FIELD_UNITS_AND_MINIMUM(DelaunayVacancyModifier, latticeConstant, WorldParameterUnit, 0);

namespace {

/// Geometry of a cubic lattice with basis, expressed in units of the lattice constant a0.
struct LatticeInfo
{
    int numBasis;
    Vector3 basis[4];
    FloatType nnDistance;   ///< First-neighbor distance.
    FloatType holeRadius;   ///< Radius of the largest interstitial hole of the perfect lattice.
};

LatticeInfo latticeInfo(DelaunayVacancyModifier::LatticeType type)
{
    if(type == DelaunayVacancyModifier::BCC)
        return { 2, {Vector3(0, 0, 0), Vector3(0.5, 0.5, 0.5)}, FloatType(std::sqrt(3.0) / 2), FloatType(std::sqrt(5.0) / 4) };
    return { 4, {Vector3(0, 0, 0), Vector3(0.5, 0.5, 0), Vector3(0.5, 0, 0.5), Vector3(0, 0.5, 0.5)}, FloatType(1.0 / std::sqrt(2.0)), FloatType(0.5) };
}

/// Length scales of the method relative to a0 (the values of the Python prototype at a0 = 2.862 A).
constexpr FloatType NMS_RADIUS = 0.7;           ///< Minimum separation of two reported voids/vacancies.
constexpr FloatType LINK_EXTRA = 0.175;         ///< Void centers closer than nn distance + this are one group.
constexpr FloatType LOCAL_RADIUS = 2.1;         ///< Radius of atoms used to fit the local lattice.
constexpr FloatType SITE_RADIUS = 1.157;        ///< Lattice sites within this distance of a group are tested.

struct DetectionParams
{
    DelaunayVacancyModifier::LatticeType lattice;
    FloatType a0;
    bool refine;
};

struct DetectionResult
{
    std::vector<Point3> sites;  ///< Detected vacancy positions (wrapped into the cell).
    size_t numVoids = 0;        ///< Voids after merging, before the local lattice refinement.
    FloatType a0 = 0;           ///< Lattice constant used.
};

/// Wraps a list of points into a position property (the neighbor finders operate on properties).
ConstPropertyPtr makePositionProperty(const std::vector<Point3>& points)
{
    PropertyPtr prop = Particles::OOClass().createStandardProperty(DataBuffer::Uninitialized, points.size(), Particles::PositionProperty);
    BufferWriteAccess<Point3, access_mode::discard_write> acc(prop);
    std::copy(points.begin(), points.end(), acc.begin());
    return prop;
}

/// Greedy non-maximum suppression: visits points by descending score and drops all points within
/// the given radius of an accepted one. Returns the indices of the accepted points.
std::vector<size_t> suppressNonMaxima(const std::vector<Point3>& points, const std::vector<FloatType>& score, const SimulationCellData& cell, FloatType radius)
{
    std::vector<size_t> kept;
    if(points.empty())
        return kept;
    ConstPropertyPtr posProperty = makePositionProperty(points);
    CutoffNeighborFinder finder(radius, posProperty, cell, {});
    std::vector<size_t> order(points.size());
    std::iota(order.begin(), order.end(), size_t(0));
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return score[a] > score[b]; });
    std::vector<char> dead(points.size(), 0);
    for(size_t i : order) {
        if(dead[i]) continue;
        kept.push_back(i);
        for(CutoffNeighborFinder::Query q(finder, i); !q.atEnd(); q.next())
            dead[q.current()] = 1;
    }
    return kept;
}

/// Estimates the lattice constant from the median (over atoms) of the mean distance to the atom's first-shell neighbors
/// (8 for BCC, 12 for FCC). The mean over the whole shell is used because the distance to the single closest neighbor
/// is biased low by thermal noise and by the lattice distortion of alloys.
FloatType estimateLatticeConstant(const BufferReadAccess<Point3>& positions, const SimulationCellData& cell, const LatticeInfo& lat)
{
    const int shellSize = (lat.numBasis == 2) ? 8 : 12;
    if(positions.size() <= (size_t)shellSize)
        throw Exception(DelaunayVacancyModifier::tr("Cannot estimate the lattice constant: the system has too few particles."));
    NearestNeighborFinder finder(shellSize, positions, cell, {});
    const size_t n = positions.size();
    const size_t stride = std::max<size_t>(1, n / 20000);
    std::vector<FloatType> shellMean;
    for(size_t i = 0; i < n; i += stride) {
        NearestNeighborFinder::Query<32> query(finder);
        query.findNeighbors(i);
        if((int)query.results().size() < shellSize)
            continue;
        FloatType sum = 0;
        for(const auto& neighbor : query.results())
            sum += std::sqrt(neighbor.distanceSq);
        shellMean.push_back(sum / query.results().size());
    }
    if(shellMean.empty())
        throw Exception(DelaunayVacancyModifier::tr("Cannot estimate the lattice constant: the system has too few particles."));
    std::nth_element(shellMean.begin(), shellMean.begin() + shellMean.size() / 2, shellMean.end());
    return shellMean[shellMean.size() / 2] / lat.nnDistance;
}

/// Index of a lattice site: integer cell coordinates plus basis index.
using SiteKey = std::array<int, 4>;

/// Finds the lattice site of the lattice (origin, a0) that is closest to the point p (given relative to the origin).
SiteKey nearestSite(const Vector3& p, FloatType a0, const LatticeInfo& lat)
{
    SiteKey best{};
    FloatType bestDist = std::numeric_limits<FloatType>::max();
    for(int b = 0; b < lat.numBasis; b++) {
        Vector3 f = p / a0 - lat.basis[b];
        Vector3 r;
        SiteKey key{0, 0, 0, b};
        for(int d = 0; d < 3; d++) {
            FloatType rounded = std::round(f[d]);
            key[d] = (int)rounded;
            r[d] = f[d] - rounded;
        }
        FloatType dist = r.squaredLength();
        if(dist < bestDist) { bestDist = dist; best = key; }
    }
    return best;
}

/// Refines one group of void centers: fits a lattice to the atoms around it and returns the unoccupied sites.
void refineGroup(const std::vector<Point3>& centers, const CutoffNeighborFinder& atomFinder, const SimulationCellData& cell,
                 FloatType a0, const LatticeInfo& lat, std::vector<Point3>& outSites)
{
    // Unwrap the group around its first center.
    std::vector<Point3> cu(centers.size());
    cu[0] = centers[0];
    for(size_t j = 1; j < centers.size(); j++)
        cu[j] = centers[0] + cell.wrapVector(centers[j] - centers[0]);
    Vector3 midVec = Vector3::Zero();
    for(const Point3& p : cu) midVec += p - Point3::Origin();
    const Point3 mid = Point3::Origin() + midVec / (FloatType)cu.size();
    FloatType rad = 0;
    for(const Point3& p : cu) rad = std::max(rad, (p - mid).length());

    // Collect the atoms around the group (union of the spheres around each center), unwrapped consistently.
    struct NearAtom { size_t index; Point3 pos; };
    std::vector<NearAtom> near;
    for(const Point3& c : cu) {
        for(CutoffNeighborFinder::Query q(atomFinder, cell.wrapPoint(c)); !q.atEnd(); q.next())
            near.push_back({q.current(), c + q.delta()});
    }
    std::sort(near.begin(), near.end(), [](const NearAtom& a, const NearAtom& b) { return a.index < b.index; });
    std::vector<Point3> atoms;
    const FloatType eps2 = 1e-6 * a0 * a0;
    for(size_t i = 0; i < near.size(); i++) {
        bool duplicate = false;
        for(size_t k = i; k-- > 0 && near[k].index == near[i].index;) {
            if((near[k].pos - near[i].pos).squaredLength() < eps2) { duplicate = true; break; }
        }
        if(!duplicate) atoms.push_back(near[i].pos);
    }
    if(atoms.empty())
        return;

    // Lattice translation: start at the atom closest to the group, refine with the mean residual.
    Point3 t = atoms[0];
    FloatType closest = std::numeric_limits<FloatType>::max();
    for(const Point3& a : atoms) {
        FloatType d2 = (a - mid).squaredLength();
        if(d2 < closest) { closest = d2; t = a; }
    }
    for(int iter = 0; iter < 5; iter++) {
        Vector3 meanResidual = Vector3::Zero();
        for(const Point3& a : atoms) {
            Vector3 bestR = Vector3::Zero();
            FloatType bestDist = std::numeric_limits<FloatType>::max();
            for(int b = 0; b < lat.numBasis; b++) {
                Vector3 f = (a - t) / a0 - lat.basis[b];
                Vector3 r;
                for(int d = 0; d < 3; d++) r[d] = f[d] - std::round(f[d]);
                if(r.squaredLength() < bestDist) { bestDist = r.squaredLength(); bestR = r; }
            }
            meanResidual += bestR;
        }
        t += a0 * meanResidual / (FloatType)atoms.size();
    }

    // Lattice sites close to the group.
    const FloatType siteRadius = SITE_RADIUS * a0;
    Vector3 shift = mid - t;
    Point3 base = t;
    for(int d = 0; d < 3; d++) base[d] += a0 * std::round(shift[d] / a0);
    const int n = (int)std::ceil((rad + siteRadius) / a0) + 1;

    // Occupied sites, in the same lattice frame (key relative to base).
    std::set<SiteKey> occupied;
    for(const Point3& a : atoms)
        occupied.insert(nearestSite(a - base, a0, lat));

    for(int ix = -n; ix <= n; ix++) for(int iy = -n; iy <= n; iy++) for(int iz = -n; iz <= n; iz++) {
        for(int b = 0; b < lat.numBasis; b++) {
            if(occupied.count(SiteKey{ix, iy, iz, b})) continue;
            Point3 site = base + a0 * (Vector3(ix, iy, iz) + lat.basis[b]);
            bool closeToGroup = false;
            for(const Point3& c : cu) {
                if((site - c).squaredLength() < siteRadius * siteRadius) { closeToGroup = true; break; }
            }
            if(closeToGroup)
                outSites.push_back(cell.wrapPoint(site));
        }
    }
}

/// The detection algorithm.
DetectionResult detectVacancies(const BufferReadAccess<Point3>& positions, const SimulationCell* simCell, const DetectionParams& params, const ConstPropertyPtr& positionProperty, TaskProgress& progress)
{
    const SimulationCellData cell(simCell);
    const LatticeInfo lat = latticeInfo(params.lattice);

    DetectionResult result;
    result.a0 = params.a0 > 0 ? params.a0 : estimateLatticeConstant(positions, cell, lat);
    const FloatType a0 = result.a0;
    if(!(a0 > 0) || positions.size() < 4)
        return result;

    // Stage 1: Delaunay voids. The ghost layer must exceed the largest circumsphere of interest.
    const FloatType threshold = FloatType(0.5) * a0 * (lat.holeRadius + lat.nnDistance);
    DelaunayTessellation tessellation;
    tessellation.generateTessellation(simCell, positions.cbegin(), positions.size(), LOCAL_RADIUS * a0, false, nullptr, progress);

    std::vector<Point3> centers;
    std::vector<FloatType> radii;
    for(DelaunayTessellation::CellHandle c : tessellation.cells()) {
        this_task::throwIfCanceled();
        if(!tessellation.isFiniteCell(c))
            continue;
        const Point3 p0 = tessellation.vertexPosition(tessellation.cellVertex(c, 0));
        const Vector3 a = tessellation.vertexPosition(tessellation.cellVertex(c, 1)) - p0;
        const Vector3 b = tessellation.vertexPosition(tessellation.cellVertex(c, 2)) - p0;
        const Vector3 d = tessellation.vertexPosition(tessellation.cellVertex(c, 3)) - p0;
        const Vector3 bxd = b.cross(d);
        const FloatType det = a.dot(bxd);
        if(std::abs(det) < 1e-6 * a0 * a0 * a0)
            continue;   // Sliver.
        const Vector3 offset = (a.squaredLength() * bxd + b.squaredLength() * d.cross(a) + d.squaredLength() * a.cross(b)) / (2 * det);
        const FloatType radius = offset.length();
        if(radius <= threshold)
            continue;
        const Point3 center = p0 + offset;
        // Keep the circumcenters of the primary image only.
        const Point3 reduced = cell.absoluteToReduced(center);
        if(reduced.x() < 0 || reduced.x() >= 1 || reduced.y() < 0 || reduced.y() >= 1 || reduced.z() < 0 || reduced.z() >= 1)
            continue;
        centers.push_back(center);
        radii.push_back(radius);
    }

    // Stage 2: merge voids that are closer than NMS_RADIUS * a0.
    std::vector<Point3> voids;
    for(size_t i : suppressNonMaxima(centers, radii, cell, NMS_RADIUS * a0))
        voids.push_back(centers[i]);
    result.numVoids = voids.size();

    if(!params.refine || voids.empty()) {
        result.sites = std::move(voids);
        return result;
    }

    // Stage 3: group the (unmerged) void centers and fit a lattice around each group.
    const FloatType linkDistance = (lat.nnDistance + LINK_EXTRA) * a0;
    std::vector<size_t> parent(centers.size());
    std::iota(parent.begin(), parent.end(), size_t(0));
    auto findRoot = [&](size_t i) { while(parent[i] != i) { parent[i] = parent[parent[i]]; i = parent[i]; } return i; };
    {
        ConstPropertyPtr centerProperty = makePositionProperty(centers);
        CutoffNeighborFinder linkFinder(linkDistance, centerProperty, cell, {});
        for(size_t i = 0; i < centers.size(); i++) {
            for(CutoffNeighborFinder::Query q(linkFinder, i); !q.atEnd(); q.next())
                parent[findRoot(i)] = findRoot(q.current());
        }
    }
    std::map<size_t, std::vector<Point3>> groups;
    for(size_t i = 0; i < centers.size(); i++)
        groups[findRoot(i)].push_back(centers[i]);

    CutoffNeighborFinder atomFinder(LOCAL_RADIUS * a0, positionProperty, cell, {});
    std::vector<Point3> sites;
    for(const auto& group : groups) {
        this_task::throwIfCanceled();
        refineGroup(group.second, atomFinder, cell, a0, lat, sites);
    }

    // Neighboring groups can report the same site.
    std::vector<FloatType> zeroScore(sites.size(), 0);
    for(size_t i : suppressNonMaxima(sites, zeroScore, cell, NMS_RADIUS * a0))
        result.sites.push_back(sites[i]);
    return result;
}

}   // End of anonymous namespace

/******************************************************************************
* Asks the modifier whether it can be applied to the given input data.
******************************************************************************/
bool DelaunayVacancyModifier::OOMetaClass::isApplicableTo(const DataCollection& input) const
{
    return input.containsObject<Particles>() && input.containsObject<SimulationCell>();
}

/******************************************************************************
* Is called by the pipeline system before a new modifier evaluation begins.
******************************************************************************/
void DelaunayVacancyModifier::preevaluateModifier(const ModifierEvaluationRequest& request, PipelineEvaluationResult::EvaluationTypes& evaluationTypes, TimeInterval& validityInterval) const
{
    // Indicate that we will do different computations depending on whether the pipeline is evaluated in interactive mode or not.
    if(request.interactiveMode())
        evaluationTypes = PipelineEvaluationResult::EvaluationType::Interactive;
    else
        evaluationTypes = PipelineEvaluationResult::EvaluationType::Noninteractive;
}

/******************************************************************************
* Modifies the input data.
******************************************************************************/
Future<PipelineFlowState> DelaunayVacancyModifier::evaluateModifier(const ModifierEvaluationRequest& request, PipelineFlowState&& state)
{
    // Get input data.
    Particles* particles = state.expectMutableObject<Particles>();
    particles->verifyIntegrity();
    const SimulationCell* simCell = state.expectObject<SimulationCell>();
    if(simCell->is2D())
        throw Exception(tr("The Delaunay vacancy analysis is not supported for 2D systems."));
    if(simCell->isDegenerate())
        throw Exception(tr("The simulation cell is degenerate."));
    if(latticeConstant() < 0)
        throw Exception(tr("The lattice constant must not be negative."));

    // In interactive mode, do not perform a real computation. Instead, reuse old results if available in the pipeline cache.
    if(request.interactiveMode()) {
        if(PipelineFlowState cachedState = request.modificationNode()->getCachedPipelineNodeOutput(request.time(), true)) {
            if(outputVacancySites()) {
                if(const Particles* cachedParticles = cachedState.getObject<Particles>())
                    state.replaceObject(particles, cachedParticles);
            }
            state.adoptAttributesFrom(cachedState, request.modificationNode());
        }
        return std::move(state);
    }

    const Property* posProperty = particles->expectProperty(Particles::PositionProperty);

    // Perform the calculation in a separate thread.
    return asyncLaunch([
            state = std::move(state),
            particles,
            simCell,
            posProperty,
            params = DetectionParams{lattice(), latticeConstant(), refineWithLocalLattice()},
            outputSites = outputVacancySites(),
            createdByNode = request.modificationNodeWeak()]() mutable
    {
        TaskProgress progress(this_task::ui());
        progress.setText(tr("Detecting vacancies (Delaunay)"));

        DetectionResult result = detectVacancies(BufferReadAccess<Point3>(posProperty), simCell, params, posProperty, progress);

        if(outputSites) {
            DataOORef<Particles> sites = DataOORef<Particles>::create();
            sites->setElementCount(result.sites.size());
            BufferWriteAccess<Point3, access_mode::discard_write> posAcc = sites->createProperty(Particles::PositionProperty);
            std::copy(result.sites.begin(), result.sites.end(), posAcc.begin());
            posAcc.reset();
            state.replaceObject(particles, sites);
        }

        state.addAttribute(QStringLiteral("DelaunayVacancy.vacancy_count"), QVariant::fromValue<qlonglong>(result.sites.size()), createdByNode);
        state.addAttribute(QStringLiteral("DelaunayVacancy.void_count"), QVariant::fromValue<qlonglong>(result.numVoids), createdByNode);
        state.addAttribute(QStringLiteral("DelaunayVacancy.lattice_constant"), QVariant::fromValue(result.a0), createdByNode);
        state.setStatus(PipelineStatus(PipelineStatus::Success, tr("Found %1 vacancies (lattice constant %2)").arg(result.sites.size()).arg(result.a0)));
        return std::move(state);
    });
}

}   // End of namespace
