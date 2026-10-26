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
#include <ovito/stdobj/simcell/SimulationCell.h>
#include <ovito/mesh/surface/SurfaceMesh.h>
#include <ovito/mesh/surface/SurfaceMeshBuilder.h>
#include <ovito/core/dataset/pipeline/ModificationNode.h>
#include "RenderLAMMPSRegionsModifier.h"

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(RenderLAMMPSRegionsModifier);
OVITO_CLASSINFO(RenderLAMMPSRegionsModifier, "DisplayName", "Render LAMMPS regions");
OVITO_CLASSINFO(RenderLAMMPSRegionsModifier, "Description", "Visualize the geometric regions defined by LAMMPS region commands.");
OVITO_CLASSINFO(RenderLAMMPSRegionsModifier, "ModifierCategory", "Visualization");
DEFINE_PROPERTY_FIELD(RenderLAMMPSRegionsModifier, commands);
DEFINE_PROPERTY_FIELD(RenderLAMMPSRegionsModifier, colorByRegion);
DEFINE_PROPERTY_FIELD(RenderLAMMPSRegionsModifier, selectParticles);
DEFINE_REFERENCE_FIELD(RenderLAMMPSRegionsModifier, regionsVis);
SET_PROPERTY_FIELD_LABEL(RenderLAMMPSRegionsModifier, commands, "Region commands");
SET_PROPERTY_FIELD_LABEL(RenderLAMMPSRegionsModifier, colorByRegion, "Color by region");
SET_PROPERTY_FIELD_LABEL(RenderLAMMPSRegionsModifier, selectParticles, "Select interior atoms");
SET_PROPERTY_FIELD_LABEL(RenderLAMMPSRegionsModifier, regionsVis, "LAMMPS Regions");

namespace {

using Style = RenderLAMMPSRegionsModifier::Style;
using Region = RenderLAMMPSRegionsModifier::Region;

/// Number of segments of cone and cylinder meshes, and icosphere subdivision levels (2562 and 10242 vertices).
constexpr int CircleSegments = 50;
constexpr int SphereSubdivisions = 4;
constexpr int EllipsoidSubdivisions = 5;

/// Name, argument count (after the axis letter for cones and cylinders) and argument names of each style.
struct StyleInfo { const char* name; Style style; bool hasAxis; int argCount; const char* usage; int maxOpenFace; };
const StyleInfo styleInfos[] = {
    {"block",     Style::Block,     false, 6, "<xlo> <xhi> <ylo> <yhi> <zlo> <zhi>", 6},
    {"cone",      Style::Cone,      true,  6, "<dim> <c1> <c2> <radlo> <radhi> <lo> <hi>", 3},
    {"cylinder",  Style::Cylinder,  true,  5, "<dim> <c1> <c2> <radius> <lo> <hi>", 3},
    {"ellipsoid", Style::Ellipsoid, false, 6, "<x> <y> <z> <a> <b> <c>", 0},
    {"plane",     Style::Plane,     false, 6, "<px> <py> <pz> <nx> <ny> <nz>", 0},
    {"prism",     Style::Prism,     false, 9, "<xlo> <xhi> <ylo> <yhi> <zlo> <zhi> <xy> <xz> <yz>", 6},
    {"sphere",    Style::Sphere,    false, 4, "<x> <y> <z> <radius>", 0},
};

/// The two coordinates perpendicular to a cone/cylinder axis, ordered so that (u, v, axis) is a right-handed frame
/// except for the y axis, where LAMMPS orders them (x, z).
void perpendicularDims(int axis, int& u, int& v)
{
    u = (axis == 0) ? 1 : 0;
    v = (axis == 2) ? 1 : 2;
}

/// Vertices and faces (outward, counter-clockwise) of one region in region coordinates.
struct Polyhedron {
    std::vector<Point3> vertices;
    std::vector<std::vector<int>> faces;
    std::vector<int> faceIds;   ///< LAMMPS face index (1-based) for 'open', 0 if the face cannot be opened.
};

/// Unit icosphere obtained by repeated subdivision of an icosahedron.
const Polyhedron& unitIcosphere(int subdivisions)
{
    static std::map<int, Polyhedron> cache;
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);
    if(auto iter = cache.find(subdivisions); iter != cache.end())
        return iter->second;

    const double t = (1.0 + std::sqrt(5.0)) / 2.0;
    std::vector<Vector3> verts = {
        {-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t},
        {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
    for(Vector3& v : verts) v.normalize();
    std::vector<std::array<int, 3>> tris = {
        {0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
        {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9}, {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}};
    for(int level = 0; level < subdivisions; level++) {
        std::map<std::pair<int, int>, int> midpoints;
        auto midpoint = [&](int a, int b) {
            auto key = std::minmax(a, b);
            auto [iter, inserted] = midpoints.emplace(key, (int)verts.size());
            if(inserted) verts.push_back((verts[a] + verts[b]).normalized());
            return iter->second;
        };
        std::vector<std::array<int, 3>> refined;
        refined.reserve(tris.size() * 4);
        for(const auto& [a, b, c] : tris) {
            const int ab = midpoint(a, b), bc = midpoint(b, c), ca = midpoint(c, a);
            refined.push_back({a, ab, ca});
            refined.push_back({b, bc, ab});
            refined.push_back({c, ca, bc});
            refined.push_back({ab, bc, ca});
        }
        tris = std::move(refined);
    }
    Polyhedron& sphere = cache[subdivisions];
    for(const Vector3& v : verts) sphere.vertices.push_back(Point3::Origin() + v);
    for(auto [a, b, c] : tris) {
        // Make each triangle point away from the center.
        if((verts[b] - verts[a]).cross(verts[c] - verts[a]).dot(verts[a] + verts[b] + verts[c]) < 0)
            std::swap(b, c);
        sphere.faces.push_back({a, b, c});
        sphere.faceIds.push_back(0);
    }
    return sphere;
}

/// Parallelepiped spanned by a, b, c from the origin o, with the faces in LAMMPS order xlo, xhi, ylo, yhi, zlo, zhi.
Polyhedron parallelepiped(const Point3& o, const Vector3& a, const Vector3& b, const Vector3& c)
{
    Polyhedron p;
    for(int k = 0; k < 2; k++) for(int j = 0; j < 2; j++) for(int i = 0; i < 2; i++)
        p.vertices.push_back(o + (FloatType)i * a + (FloatType)j * b + (FloatType)k * c);
    p.faces = {{0, 4, 6, 2}, {1, 3, 7, 5}, {0, 1, 5, 4}, {2, 6, 7, 3}, {0, 2, 3, 1}, {4, 5, 7, 6}};
    p.faceIds = {1, 2, 3, 4, 5, 6};
    return p;
}

/// Truncated cone (or cylinder) along the given axis, with the faces in LAMMPS order lo cap, hi cap, side.
Polyhedron frustum(int axis, double c1, double c2, double radlo, double radhi, double lo, double hi)
{
    int u, v;
    perpendicularDims(axis, u, v);
    Polyhedron p;
    for(int ring = 0; ring < 2; ring++) {
        const double r = (ring == 0) ? radhi : radlo;
        for(int k = 0; k < CircleSegments; k++) {
            const double angle = 2 * M_PI * k / CircleSegments;
            Point3 pt;
            pt[u] = c1 + r * std::cos(angle);
            pt[v] = c2 + r * std::sin(angle);
            pt[axis] = (ring == 0) ? hi : lo;
            p.vertices.push_back(pt);
        }
    }
    // Faces for a right-handed (u, v, axis) frame; vertices 0..49 form the hi ring, 50..99 the lo ring.
    std::vector<int> loCap, hiCap;
    for(int k = 0; k < CircleSegments; k++) {
        hiCap.push_back(k);
        loCap.push_back(2 * CircleSegments - 1 - k);
    }
    p.faces.push_back(loCap); p.faceIds.push_back(1);
    p.faces.push_back(hiCap); p.faceIds.push_back(2);
    for(int k = 0; k < CircleSegments; k++) {
        const int k1 = (k + 1) % CircleSegments;
        p.faces.push_back({CircleSegments + k, CircleSegments + k1, k1, k});
        p.faceIds.push_back(3);
    }
    if(axis == 1) {   // (x, z, y) is left-handed.
        for(auto& f : p.faces) std::reverse(f.begin(), f.end());
    }
    return p;
}

/// The corners of a simulation cell.
std::array<Point3, 8> cellCorners(const SimulationCell* cell)
{
    std::array<Point3, 8> corners;
    for(int i = 0; i < 8; i++)
        corners[i] = cell->cellOrigin() + (FloatType)(i & 1) * cell->cellVector1() + (FloatType)((i >> 1) & 1) * cell->cellVector2() + (FloatType)((i >> 2) & 1) * cell->cellVector3();
    return corners;
}

/// Polygon in which a plane intersects the simulation cell, counter-clockwise about the normal.
std::vector<Point3> planeCellPolygon(const Point3& p, const Vector3& n, const SimulationCell* cell)
{
    const std::array<Point3, 8> corners = cellCorners(cell);
    std::vector<Point3> points;
    auto addPoint = [&](const Point3& q) {
        const double eps = 1e-9 * std::max<FloatType>(1, (corners[7] - corners[0]).length());
        for(const Point3& existing : points)
            if((existing - q).length() <= eps) return;
        points.push_back(q);
    };
    for(int i = 0; i < 8; i++) {
        for(int d = 0; d < 3; d++) {
            const int j = i | (1 << d);
            if(j == i) continue;
            const double si = n.dot(corners[i] - p), sj = n.dot(corners[j] - p);
            if(si == 0) addPoint(corners[i]);
            if(sj == 0) addPoint(corners[j]);
            if((si < 0 && sj > 0) || (si > 0 && sj < 0))
                addPoint(corners[i] + (corners[j] - corners[i]) * (FloatType)(si / (si - sj)));
        }
    }
    if(points.size() < 3)
        return {};
    Vector3 centroid = Vector3::Zero();
    for(const Point3& q : points) centroid += q - Point3::Origin();
    centroid /= (FloatType)points.size();
    const Vector3 normal = n.normalized();
    Vector3 e1 = (std::abs(normal.x()) < 0.9) ? Vector3(1, 0, 0) : Vector3(0, 1, 0);
    e1 = (e1 - normal * normal.dot(e1)).normalized();
    const Vector3 e2 = normal.cross(e1);
    std::ranges::sort(points, {}, [&](const Point3& q) {
        const Vector3 d = q - (Point3::Origin() + centroid);
        return std::atan2(d.dot(e2), d.dot(e1));
    });
    return points;
}

/// Splits the commands into statements (comments removed, '&' continuations joined), each with its first line number.
std::vector<std::pair<int, QString>> splitStatements(const QString& text)
{
    std::vector<std::pair<int, QString>> statements;
    QString pending;
    int startLine = 0;
    const QStringList lines = text.split(QChar('\n'));
    for(int i = 0; i < lines.size(); i++) {
        QString line = lines[i];
        if(int hash = line.indexOf(QChar('#')); hash >= 0)
            line.truncate(hash);
        line = line.trimmed();
        if(pending.isEmpty()) startLine = i + 1;
        if(line.endsWith(QChar('&'))) {
            line.chop(1);
            pending += line + QChar(' ');
            continue;
        }
        pending += line;
        if(!pending.trimmed().isEmpty())
            statements.emplace_back(startLine, pending.trimmed());
        pending.clear();
    }
    if(!pending.trimmed().isEmpty())
        statements.emplace_back(startLine, pending.trimmed());
    return statements;
}

}   // End of anonymous namespace

/******************************************************************************
* Parses the region commands.
******************************************************************************/
std::vector<Region> RenderLAMMPSRegionsModifier::parseCommands(const QString& text, const SimulationCell* cell, QStringList& warnings)
{
    std::vector<Region> regions;
    for(const auto& [lineNumber, statement] : splitStatements(text)) {
        const QStringList tokens = statement.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        auto error = [lineNumber](const QString& msg) { return Exception(tr("Line %1: %2").arg(lineNumber).arg(msg)); };
        if(tokens[0] != QStringLiteral("region"))
            throw error(tr("Only LAMMPS region commands are supported, found '%1'.").arg(tokens[0]));
        if(tokens.size() < 3)
            throw error(tr("Expected 'region <ID> <style> <args> [keyword values ...]'."));

        Region region;
        region.id = tokens[1];
        for(const Region& other : regions)
            if(other.id == region.id)
                throw error(tr("Region ID '%1' is used more than once.").arg(region.id));

        const StyleInfo* info = nullptr;
        for(const StyleInfo& s : styleInfos)
            if(tokens[2] == QLatin1String(s.name)) info = &s;
        if(!info) {
            QStringList names;
            for(const StyleInfo& s : styleInfos) names.push_back(QString::fromLatin1(s.name));
            throw error(tr("Unknown or unsupported region style '%1'. Supported styles are: %2.").arg(tokens[2]).arg(names.join(QStringLiteral(", "))));
        }
        region.style = info->style;
        int pos = 3;
        const int needed = info->argCount + (info->hasAxis ? 1 : 0);
        const QString usage = tr("Expected %1 after style '%2'.").arg(QString::fromLatin1(info->usage)).arg(QString::fromLatin1(info->name));
        if(tokens.size() < pos + needed)
            throw error(usage);

        // A literal number (equal-style variables are not supported).
        auto number = [&](const QString& token) {
            if(token.startsWith(QStringLiteral("v_")))
                throw error(tr("LAMMPS equal-style variables ('%1') are not supported. Please replace them with literal values.").arg(token));
            bool ok;
            const double value = token.toDouble(&ok);
            if(!ok || !std::isfinite(value))
                throw error(tr("Invalid number '%1'. %2").arg(token).arg(usage));
            return value;
        };
        // A box bound that may be INF or EDGE.
        auto bound = [&](const QString& token, int dim, bool upper) {
            if(token != QStringLiteral("INF") && token != QStringLiteral("EDGE"))
                return number(token);
            if(!cell)
                throw error(tr("%1 requires a simulation cell.").arg(token));
            const std::array<Point3, 8> corners = cellCorners(cell);
            if(token == QStringLiteral("EDGE")) {
                double value = corners[0][dim];
                for(const Point3& c : corners) value = upper ? std::max<double>(value, c[dim]) : std::min<double>(value, c[dim]);
                return value;
            }
            const Vector3 diagonal = cell->cellVector1() + cell->cellVector2() + cell->cellVector3();
            const double center = cell->cellOrigin()[dim] + diagonal[dim] / 2;
            return upper ? center + diagonal.length() / 2 : center - diagonal.length() / 2;
        };

        if(info->hasAxis) {
            const QString& dim = tokens[pos++];
            if(dim == QStringLiteral("x")) region.axis = 0;
            else if(dim == QStringLiteral("y")) region.axis = 1;
            else if(dim == QStringLiteral("z")) region.axis = 2;
            else throw error(tr("Invalid axis '%1'. %2").arg(dim).arg(usage));
        }
        for(int i = 0; i < info->argCount; i++, pos++) {
            const QString& token = tokens[pos];
            switch(info->style) {
            case Style::Block:
            case Style::Prism:
                region.params.push_back(i < 6 ? bound(token, i / 2, i % 2) : number(token));
                break;
            case Style::Cone:
                region.params.push_back(i >= 4 ? bound(token, region.axis, i == 5) : number(token));
                break;
            case Style::Cylinder:
                region.params.push_back(i >= 3 ? bound(token, region.axis, i == 4) : number(token));
                break;
            default:
                region.params.push_back(number(token));
            }
        }

        // Validate the geometry.
        const std::vector<double>& a = region.params;
        switch(region.style) {
        case Style::Block:
        case Style::Prism:
            for(int d = 0; d < 3; d++)
                if(a[2 * d] >= a[2 * d + 1])
                    throw error(tr("The lower bound must be smaller than the upper bound in each direction."));
            break;
        case Style::Cone:
            if(a[2] < 0 || a[3] < 0 || (a[2] == 0 && a[3] == 0) || a[4] >= a[5])
                throw error(tr("The cone radii must not be negative or both zero, and lo must be smaller than hi."));
            break;
        case Style::Cylinder:
            if(a[2] <= 0 || a[3] >= a[4])
                throw error(tr("The cylinder radius must be positive and lo must be smaller than hi."));
            break;
        case Style::Ellipsoid:
            if(a[3] <= 0 || a[4] <= 0 || a[5] <= 0)
                throw error(tr("The ellipsoid semi-axes must be positive."));
            break;
        case Style::Plane:
            if(a[3] == 0 && a[4] == 0 && a[5] == 0)
                throw error(tr("The plane normal must not be zero."));
            break;
        case Style::Sphere:
            if(a[3] <= 0)
                throw error(tr("The sphere radius must be positive."));
            break;
        }

        // Keywords.
        Vector3 move = Vector3::Zero();
        AffineTransformation rotation = AffineTransformation::Identity();
        auto expect = [&](int count, const QString& keyword) {
            if(pos + count > tokens.size())
                throw error(tr("Keyword '%1' expects %2 value(s).").arg(keyword).arg(count));
        };
        while(pos < tokens.size()) {
            const QString keyword = tokens[pos++];
            if(keyword == QStringLiteral("side")) {
                expect(1, keyword);
                const QString value = tokens[pos++];
                if(value == QStringLiteral("in")) region.sideOut = false;
                else if(value == QStringLiteral("out")) region.sideOut = true;
                else throw error(tr("Invalid value '%1' for keyword 'side' (expected 'in' or 'out').").arg(value));
            }
            else if(keyword == QStringLiteral("units")) {
                expect(1, keyword);
                const QString value = tokens[pos++];
                if(value == QStringLiteral("lattice"))
                    warnings.push_back(tr("Line %1: lattice units are not supported, using box units instead.").arg(lineNumber));
                else if(value != QStringLiteral("box"))
                    throw error(tr("Invalid value '%1' for keyword 'units' (expected 'box' or 'lattice').").arg(value));
            }
            else if(keyword == QStringLiteral("move")) {
                expect(3, keyword);
                for(int d = 0; d < 3; d++, pos++)
                    move[d] = (tokens[pos] == QStringLiteral("NULL")) ? 0.0 : number(tokens[pos]);
            }
            else if(keyword == QStringLiteral("rotate")) {
                expect(7, keyword);
                const double theta = number(tokens[pos]);
                const Point3 center(number(tokens[pos + 1]), number(tokens[pos + 2]), number(tokens[pos + 3]));
                const Vector3 axis(number(tokens[pos + 4]), number(tokens[pos + 5]), number(tokens[pos + 6]));
                pos += 7;
                if(axis.isZero())
                    throw error(tr("The rotation axis must not be zero."));
                rotation = AffineTransformation::translation(center - Point3::Origin()) * AffineTransformation::rotation(Rotation(axis, theta))
                    * AffineTransformation::translation(-(center - Point3::Origin()));
            }
            else if(keyword == QStringLiteral("open")) {
                expect(1, keyword);
                if(info->maxOpenFace == 0)
                    throw error(tr("The region style '%1' does not support the 'open' keyword.").arg(QString::fromLatin1(info->name)));
                bool ok;
                const int face = tokens[pos++].toInt(&ok);
                if(!ok || face < 1 || face > info->maxOpenFace)
                    throw error(tr("Invalid face index for keyword 'open' (expected 1 to %1).").arg(info->maxOpenFace));
                region.open.set(face - 1);
            }
            else {
                throw error(tr("Unknown or unsupported keyword '%1'. Supported keywords are: side, open, move, rotate, units.").arg(keyword));
            }
        }
        // Rotation is applied before the displacement.
        region.transform = AffineTransformation::translation(move) * rotation;
        region.inverse = region.transform.inverse();
        regions.push_back(std::move(region));
    }
    return regions;
}

/******************************************************************************
* Returns whether a point lies inside the region.
******************************************************************************/
bool RenderLAMMPSRegionsModifier::isInside(const Region& region, const Point3& point)
{
    const Point3 q = region.inverse * point;
    const std::vector<double>& a = region.params;
    bool inside = false;
    switch(region.style) {
    case Style::Block:
        inside = q.x() >= a[0] && q.x() <= a[1] && q.y() >= a[2] && q.y() <= a[3] && q.z() >= a[4] && q.z() <= a[5];
        break;
    case Style::Prism: {
        // Fractional coordinates in the parallelepiped spanned by (xhi-xlo, 0, 0), (xy, yhi-ylo, 0), (xz, yz, zhi-zlo).
        const double lz = a[5] - a[4], ly = a[3] - a[2], lx = a[1] - a[0];
        const double fz = (q.z() - a[4]) / lz;
        const double fy = (q.y() - a[2] - fz * a[8]) / ly;
        const double fx = (q.x() - a[0] - fy * a[6] - fz * a[7]) / lx;
        inside = fx >= 0 && fx <= 1 && fy >= 0 && fy <= 1 && fz >= 0 && fz <= 1;
        break;
    }
    case Style::Sphere: {
        const double dx = q.x() - a[0], dy = q.y() - a[1], dz = q.z() - a[2];
        inside = dx * dx + dy * dy + dz * dz <= a[3] * a[3];
        break;
    }
    case Style::Ellipsoid: {
        const double dx = (q.x() - a[0]) / a[3], dy = (q.y() - a[1]) / a[4], dz = (q.z() - a[2]) / a[5];
        inside = dx * dx + dy * dy + dz * dz <= 1;
        break;
    }
    case Style::Cylinder:
    case Style::Cone: {
        int u, v;
        perpendicularDims(region.axis, u, v);
        const double lo = (region.style == Style::Cone) ? a[4] : a[3];
        const double hi = (region.style == Style::Cone) ? a[5] : a[4];
        const double h = q[region.axis];
        if(h < lo || h > hi) break;
        const double r = (region.style == Style::Cone) ? a[2] + (a[3] - a[2]) * (h - lo) / (hi - lo) : a[2];
        const double du = q[u] - a[0], dv = q[v] - a[1];
        inside = du * du + dv * dv <= r * r;
        break;
    }
    case Style::Plane:
        inside = (q.x() - a[0]) * a[3] + (q.y() - a[1]) * a[4] + (q.z() - a[2]) * a[5] >= 0;
        break;
    }
    return inside != region.sideOut;
}

/******************************************************************************
* Constructor.
******************************************************************************/
void RenderLAMMPSRegionsModifier::initializeObject(ObjectInitializationFlags flags)
{
    Modifier::initializeObject(flags);

    if(!flags.testFlag(ObjectInitializationFlag::DontInitializeObject)) {
        setRegionsVis(OORef<SurfaceMeshVis>::create(flags));
        regionsVis()->setShowCap(true);
        regionsVis()->setSmoothShading(false);
        regionsVis()->setHighlightEdges(false);
        regionsVis()->setClipAtDomainBoundaries(true);
        regionsVis()->setSurfaceColor(Color(1, 1, 1));
        regionsVis()->setObjectTitle(tr("LAMMPS Regions"));
    }
}

/******************************************************************************
* Replaces any references the modifier has to the given visual element with a new compatible object.
******************************************************************************/
void RenderLAMMPSRegionsModifier::replaceVisualElement(DataVis* visElement, const std::function<OORef<DataVis>(const QString&)>& getReplacement)
{
    if(regionsVis() == visElement)
        setRegionsVis(static_object_cast<SurfaceMeshVis>(getReplacement(tr("LAMMPS Regions"))));
}

/******************************************************************************
* Asks the modifier whether it can be applied to the given input data.
******************************************************************************/
bool RenderLAMMPSRegionsModifier::OOMetaClass::isApplicableTo(const DataCollection& input) const
{
    return input.containsObject<SimulationCell>() || input.containsObject<Particles>();
}

/******************************************************************************
* Modifies the input data.
******************************************************************************/
Future<PipelineFlowState> RenderLAMMPSRegionsModifier::evaluateModifier(const ModifierEvaluationRequest& request, PipelineFlowState&& state)
{
    const SimulationCell* cell = state.getObject<SimulationCell>();
    QStringList warnings;
    const std::vector<Region> regions = parseCommands(commands(), cell, warnings);

    // Build the mesh in a non-periodic copy of the cell.
    SurfaceMesh* mesh = state.createObjectWithVis<SurfaceMesh>(QString::fromLatin1(meshIdentifier), request.modificationNode(), regionsVis(), tr("LAMMPS Regions"));
    SurfaceMeshBuilder builder(mesh);
    if(cell) {
        DataOORef<SimulationCell> domain = DataOORef<SimulationCell>::makeCopy(cell);
        domain->setPbcFlags(false, false, false);
        builder.setDomain(std::move(domain));
    }
    builder.createFaceProperty(DataBuffer::Uninitialized, SurfaceMeshFaces::RegionProperty);
    builder.mutableRegions()->setElementCount(regions.size());
    {
        SurfaceMeshBuilder::VertexGrower vertexGrower(builder);
        SurfaceMeshBuilder::FaceGrower faceGrower(builder);
        std::vector<SurfaceMesh::vertex_index> vertexMap, faceVertices;
        for(size_t r = 0; r < regions.size(); r++) {
            const Region& region = regions[r];
            const std::vector<double>& a = region.params;
            Polyhedron poly;
            switch(region.style) {
            case Style::Block:
                poly = parallelepiped(Point3(a[0], a[2], a[4]), Vector3(a[1] - a[0], 0, 0), Vector3(0, a[3] - a[2], 0), Vector3(0, 0, a[5] - a[4]));
                break;
            case Style::Prism:
                poly = parallelepiped(Point3(a[0], a[2], a[4]), Vector3(a[1] - a[0], 0, 0), Vector3(a[6], a[3] - a[2], 0), Vector3(a[7], a[8], a[5] - a[4]));
                break;
            case Style::Cone:
                poly = frustum(region.axis, a[0], a[1], a[2], a[3], a[4], a[5]);
                break;
            case Style::Cylinder:
                poly = frustum(region.axis, a[0], a[1], a[2], a[2], a[3], a[4]);
                break;
            case Style::Sphere:
            case Style::Ellipsoid: {
                const bool sphere = (region.style == Style::Sphere);
                poly = unitIcosphere(sphere ? SphereSubdivisions : EllipsoidSubdivisions);
                const Vector3 scale = sphere ? Vector3(a[3], a[3], a[3]) : Vector3(a[3], a[4], a[5]);
                for(Point3& p : poly.vertices)
                    p = Point3(a[0] + p.x() * scale.x(), a[1] + p.y() * scale.y(), a[2] + p.z() * scale.z());
                break;
            }
            case Style::Plane: {
                if(!cell)
                    throw Exception(tr("Region '%1': a plane region requires a simulation cell.").arg(region.id));
                // The polygon is computed in box coordinates.
                const Point3 p = region.transform * Point3(a[0], a[1], a[2]);
                const Vector3 n = region.transform * Vector3(a[3], a[4], a[5]);
                std::vector<Point3> polygon = planeCellPolygon(p, n, cell);
                if(polygon.empty())
                    warnings.push_back(tr("Region '%1': the plane does not intersect the simulation cell.").arg(region.id));
                for(const Point3& q : polygon)
                    poly.vertices.push_back(region.inverse * q);
                if(!polygon.empty()) {
                    poly.faces.emplace_back(polygon.size());
                    std::iota(poly.faces.back().begin(), poly.faces.back().end(), 0);
                    poly.faceIds.push_back(0);
                }
                break;
            }
            }

            vertexMap.clear();
            for(const Point3& p : poly.vertices)
                vertexMap.push_back(vertexGrower.createVertex(region.transform * p));
            for(size_t f = 0; f < poly.faces.size(); f++) {
                if(poly.faceIds[f] != 0 && region.open.test(poly.faceIds[f] - 1))
                    continue;
                faceVertices.clear();
                for(int v : poly.faces[f]) faceVertices.push_back(vertexMap[v]);
                if(region.sideOut)
                    std::reverse(faceVertices.begin(), faceVertices.end());
                faceGrower.createFace(faceVertices.begin(), faceVertices.end(), (SurfaceMesh::region_index)r);
            }
        }
    }
    builder.connectOppositeHalfedges();

    // Region properties.
    Property* regionIds = builder.createRegionProperty(DataBuffer::Uninitialized, QStringLiteral("Region ID"), Property::Int32);
    {
        BufferWriteAccess<int32_t, access_mode::discard_write> ids(regionIds);
        for(size_t r = 0; r < regions.size(); r++) ids[r] = (int32_t)(r + 1);
    }
    for(size_t r = 0; r < regions.size(); r++)
        regionIds->addNumericType(SurfaceMeshRegions::OOClass(), (int)(r + 1), regions[r].id);
    if(colorByRegion()) {
        BufferWriteAccess<ColorG, access_mode::discard_write> colors(builder.createRegionProperty(DataBuffer::Uninitialized, SurfaceMeshRegions::ColorProperty));
        for(size_t r = 0; r < regions.size(); r++)
            colors[r] = SurfaceMeshRegions::OOClass().getElementTypeDefaultColor({}, regions[r].id, (int)(r + 1), false).toDataType<GraphicsFloatType>();
    }

    // Particle selection: union of all regions.
    if(selectParticles()) {
        Particles* particles = state.expectMutableObject<Particles>();
        BufferReadAccess<Point3> positions(particles->expectProperty(Particles::PositionProperty));
        BufferWriteAccess<SelectionIntType, access_mode::discard_write> selection(particles->createProperty(DataBuffer::Uninitialized, Particles::SelectionProperty));
        size_t count = 0;
        for(size_t i = 0; i < positions.size(); i++) {
            const bool inside = std::ranges::any_of(regions, [&](const Region& region) { return isInside(region, positions[i]); });
            selection[i] = inside ? 1 : 0;
            if(inside) count++;
        }
        state.addAttribute(QStringLiteral("RenderLAMMPSRegions.selected_count"), QVariant::fromValue((qlonglong)count), request.modificationNode());
    }

    QString statusText = tr("Generated %1 region(s).").arg(regions.size());
    if(!warnings.isEmpty())
        state.setStatus(PipelineStatus(PipelineStatus::Warning, statusText + QChar('\n') + warnings.join(QChar('\n'))));
    else
        state.setStatus(PipelineStatus(PipelineStatus::Success, statusText));
    return std::move(state);
}

}   // End of namespace
