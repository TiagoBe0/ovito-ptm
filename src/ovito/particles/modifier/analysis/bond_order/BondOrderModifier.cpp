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
#include <ovito/stdobj/properties/ElementType.h>
#include <ovito/core/dataset/pipeline/ModificationNode.h>
#include "BondOrderModifier.h"

namespace Ovito {

IMPLEMENT_CREATABLE_OVITO_CLASS(BondOrderModifier);
OVITO_CLASSINFO(BondOrderModifier, "DisplayName", "Bond order");
OVITO_CLASSINFO(BondOrderModifier, "Description", "Assign bond orders and identify aromatic rings and functional groups in molecules.");
OVITO_CLASSINFO(BondOrderModifier, "ModifierCategory", "Analysis");
DEFINE_PROPERTY_FIELD(BondOrderModifier, fragmentSizeLimit);
DEFINE_PROPERTY_FIELD(BondOrderModifier, redistributeBondOrders);
SET_PROPERTY_FIELD_LABEL(BondOrderModifier, fragmentSizeLimit, "Molecular fragment size limit");
SET_PROPERTY_FIELD_LABEL(BondOrderModifier, redistributeBondOrders, "Redistribute bond orders");
SET_PROPERTY_FIELD_UNITS_AND_RANGE(BondOrderModifier, fragmentSizeLimit, IntegerParameterUnit, 3, 100);

namespace {

constexpr int Unlisted = -1;

/// Atomic valence penalties, measured on OVITO Pro 3.16.1 (an exact linear fit over ~24000 molecules).
/// Index: valence 0..8. Amide carbons have their own entry for valence 5, see atomPenalty().
struct PenaltyRow {
    int z;
    int connectivity;
    std::array<int, 9> penalty;
};
constexpr int X = Unlisted;
constexpr PenaltyRow penaltyTable[] = {
    {6, 1, {X, 128, X, 32, X, X, X, X, X}},
    {6, 2, {X, X, 64, 32, 0, X, X, X, X}},
    {6, 3, {X, X, X, 32, 0, X, X, X, X}},
    {6, 4, {X, X, X, X, 0, X, X, X, X}},
    {7, 1, {X, 64, 2, 0, X, X, X, X, X}},
    {7, 2, {X, X, 4, 0, 2, X, X, X, X}},
    {7, 3, {X, X, X, 0, 1, X, X, X, X}},
    {7, 4, {X, X, X, X, 0, X, X, X, X}},
    {8, 1, {X, 2, 0, 16, X, X, X, X, X}},
    {8, 2, {X, X, 0, 16, X, X, X, X, X}},
    {8, 3, {X, X, X, 0, X, X, X, X, X}},
    {14, 1, {X, 8, 4, 2, X, X, X, X, X}},
    {14, 2, {X, X, 4, 2, 0, X, X, X, X}},
    {14, 3, {X, X, X, 2, 0, X, X, X, X}},
    {14, 4, {X, X, X, X, 0, X, X, X, X}},
    {15, 1, {X, 64, 2, 0, X, X, X, X, X}},
    {15, 2, {X, X, 4, 0, 2, X, X, X, X}},
    {15, 3, {X, X, X, 0, 1, 2, X, X, X}},
    {15, 4, {X, X, X, X, 1, 0, X, X, X}},
    {16, 1, {X, 2, 0, X, X, X, X, X, X}},
    {16, 2, {X, X, 0, 32, 1, X, X, X, X}},
    {16, 3, {X, X, X, 1, 0, 2, 2, X, X}},
    {16, 4, {X, X, X, X, 4, 2, 0, X, X}},
};

const char* const elementSymbols[] = { "",
    "H", "He", "Li", "Be", "B", "C", "N", "O", "F", "Ne", "Na", "Mg", "Al", "Si", "P", "S", "Cl", "Ar", "K", "Ca",
    "Sc", "Ti", "V", "Cr", "Mn", "Fe", "Co", "Ni", "Cu", "Zn", "Ga", "Ge", "As", "Se", "Br", "Kr", "Rb", "Sr", "Y", "Zr",
    "Nb", "Mo", "Tc", "Ru", "Rh", "Pd", "Ag", "Cd", "In", "Sn", "Sb", "Te", "I", "Xe", "Cs", "Ba", "La", "Ce", "Pr", "Nd",
    "Pm", "Sm", "Eu", "Gd", "Tb", "Dy", "Ho", "Er", "Tm", "Yb", "Lu", "Hf", "Ta", "W", "Re", "Os", "Ir", "Pt", "Au", "Hg",
    "Tl", "Pb", "Bi", "Po", "At", "Rn", "Fr", "Ra", "Ac", "Th", "Pa", "U", "Np", "Pu", "Am", "Cm", "Bk", "Cf", "Es", "Fm",
    "Md", "No", "Lr" };

/// Exact optimization of the bond orders of one connected component, with a local search fallback.
///
/// The variables are the excess orders e = order - 1 in {0, 1, 2} of the component's bonds. Atom i costs f_i(x), x
/// being the summed excess of its bonds in the component (its other bonds are single). The branch and bound bounds
/// each atom by the cheapest f_i still reachable from its partial sum.
class ComponentSolver
{
public:

    ComponentSolver(std::vector<std::pair<uint32_t, uint32_t>> bonds, std::vector<std::vector<int64_t>> cost)
        : _bonds(std::move(bonds)), _cost(std::move(cost)), _cur(_cost.size(), 0), _rem(_cost.size(), 0), _excess(_bonds.size(), 0)
    {
        _incident.resize(_cost.size());
        for(uint32_t b = 0; b < _bonds.size(); b++) {
            _incident[_bonds[b].first].push_back(b);
            _incident[_bonds[b].second].push_back(b);
        }
    }

    /// Returns true if the exact search finished within the node budget.
    bool solve(int fragmentSizeLimit, size_t nodeBudget) {
        std::vector<uint32_t> all(_bonds.size());
        std::iota(all.begin(), all.end(), 0);
        std::vector<uint32_t> atoms(_cost.size());
        std::iota(atoms.begin(), atoms.end(), 0);
        for(uint32_t a : atoms) _rem[a] = (int)_incident[a].size();
        Search search = branchAndBound(all, atoms, std::numeric_limits<int64_t>::max(), std::max(nodeBudget, 4 * _bonds.size() + 16));
        _excess = std::move(search.excess);
        if(search.complete)
            return true;
        std::vector<int8_t> greedy = greedyAssignment();
        if(value(greedy) < value(_excess))
            _excess = std::move(greedy);
        // Bound of the whole component: every atom at its own optimum.
        int64_t lowerBound = 0;
        for(const auto& f : _cost) lowerBound += *std::min_element(f.begin(), f.end());
        localSearch(fragmentSizeLimit, lowerBound);
        return value() == lowerBound;
    }

    const std::vector<int8_t>& excess() const { return _excess; }

private:

    struct Search {
        int64_t value;
        std::vector<int8_t> excess;     // Indexed by bond (only the searched bonds are meaningful).
        bool complete;
    };

    /// Cheapest cost still reachable by atom a.
    int64_t bound(uint32_t a) const {
        const auto& f = _cost[a];
        int64_t m = f[_cur[a]];
        for(int x = _cur[a] + 1; x <= _cur[a] + 2 * _rem[a]; x++)
            m = std::min(m, f[x]);
        return m;
    }

    int64_t value() const { return value(_excess); }

    int64_t value(const std::vector<int8_t>& excess) const {
        std::vector<int> sum(_cost.size(), 0);
        for(size_t b = 0; b < _bonds.size(); b++) { sum[_bonds[b].first] += excess[b]; sum[_bonds[b].second] += excess[b]; }
        int64_t v = 0;
        for(size_t a = 0; a < _cost.size(); a++) v += _cost[a][sum[a]];
        return v;
    }

    /// Assigns the bonds one by one, each with the order that keeps the bounds of its two atoms lowest.
    /// Expects _cur = 0 and _rem = degree and restores that state.
    std::vector<int8_t> greedyAssignment() {
        std::vector<int8_t> excess(_bonds.size(), 0);
        for(size_t b = 0; b < _bonds.size(); b++) {
            auto [i, j] = _bonds[b];
            _rem[i]--; _rem[j]--;
            int8_t bestChoice = 0;
            int64_t bestBound = std::numeric_limits<int64_t>::max();
            for(int8_t x : {2, 1, 0}) {
                _cur[i] += x; _cur[j] += x;
                const int64_t v = bound(i) + bound(j);
                _cur[i] -= x; _cur[j] -= x;
                if(v < bestBound) { bestBound = v; bestChoice = x; }
            }
            _cur[i] += bestChoice; _cur[j] += bestChoice;
            excess[b] = bestChoice;
        }
        for(size_t a = 0; a < _cost.size(); a++) { _cur[a] = 0; _rem[a] = (int)_incident[a].size(); }
        return excess;
    }

    /// Optimizes the given bonds (in this order). On entry _rem holds the number of these bonds at each atom and _cur
    /// the excess of the atom's other bonds. Only solutions strictly below 'upperBound' are accepted.
    Search branchAndBound(const std::vector<uint32_t>& bonds, const std::vector<uint32_t>& atoms, int64_t upperBound, size_t budget) {
        static constexpr int8_t choices[3] = {2, 1, 0};
        Search best{upperBound, std::vector<int8_t>(_bonds.size(), 0), true};
        int64_t total = 0;
        for(uint32_t a : atoms) total += bound(a);
        const int64_t lowerBound = total;
        if(bonds.empty()) {
            if(total < upperBound) best.value = total;
            return best;
        }
        std::vector<int8_t> choice(bonds.size(), 0);
        std::vector<int8_t> current(bonds.size(), 0);
        auto apply = [&](size_t t, int x) {
            auto [a, b] = _bonds[bonds[t]];
            total -= bound(a) + bound(b);
            _rem[a]--; _rem[b]--; _cur[a] += x; _cur[b] += x;
            total += bound(a) + bound(b);
            current[t] = x;
        };
        auto undo = [&](size_t t) {
            auto [a, b] = _bonds[bonds[t]];
            total -= bound(a) + bound(b);
            _rem[a]++; _rem[b]++; _cur[a] -= current[t]; _cur[b] -= current[t];
            total += bound(a) + bound(b);
        };
        size_t nodes = 0;
        ptrdiff_t t = 0;
        choice[0] = 0;
        while(t >= 0) {
            if(choice[t] == 3) {
                if(--t >= 0) { undo(t); choice[t]++; }
                continue;
            }
            apply(t, choices[choice[t]]);
            if(total >= best.value) { undo(t); choice[t]++; continue; }
            if(t + 1 == (ptrdiff_t)bonds.size()) {
                best.value = total;
                for(size_t k = 0; k < bonds.size(); k++) best.excess[bonds[k]] = current[k];
                undo(t);
                if(best.value == lowerBound) {
                    // Nothing can be better: unwind.
                    while(--t >= 0) undo(t);
                    break;
                }
                choice[t]++;
                continue;
            }
            if(++nodes > budget) {
                best.complete = false;
                while(t >= 0) undo(t--);
                break;
            }
            choice[++t] = 0;
        }
        // Without any accepted solution keep the old assignment.
        if(best.value == upperBound)
            for(uint32_t b : bonds) best.excess[b] = _excess[b];
        return best;
    }

    /// Alternates path moves and fragment re-optimization until neither lowers the total penalty.
    void localSearch(int fragmentSizeLimit, int64_t lowerBound) {
        int64_t total = value();
        bool improved = true;
        while(improved && total > lowerBound) {
            improved = improveAlongPaths(total, lowerBound);
            for(int size = 3; size <= std::max(3, fragmentSizeLimit) && total > lowerBound; size++)
                while(total > lowerBound && improveFragments(size, total, lowerBound))
                    improved = true;
        }
    }

    /// Moves of arbitrary length, as augmenting paths in matching: bond orders change by +1/-1 alternately along a
    /// path, so only the two end atoms change their valence. Searched breadth-first from every atom that is not at
    /// its own lowest penalty.
    bool improveAlongPaths(int64_t& total, int64_t lowerBound) {
        bool improved = false;
        std::vector<int> sum(_cost.size(), 0);
        auto computeSums = [&]() {
            std::ranges::fill(sum, 0);
            for(size_t b = 0; b < _bonds.size(); b++) { sum[_bonds[b].first] += _excess[b]; sum[_bonds[b].second] += _excess[b]; }
        };
        computeSums();
        auto change = [&](uint32_t a, int delta) -> int64_t {
            const int x = sum[a] + delta;
            return (x < 0 || x >= (int)_cost[a].size()) ? std::numeric_limits<int64_t>::max() / 4 : _cost[a][x] - _cost[a][sum[a]];
        };
        // State: atom and the sign of the change of the next bond (0: +1, 1: -1).
        std::vector<int64_t> parentBond(2 * _cost.size());
        std::vector<uint32_t> parentState(2 * _cost.size());
        std::vector<char> visited(2 * _cost.size());
        for(uint32_t start = 0; start < _cost.size() && total > lowerBound; start++) {
            if(_cost[start][sum[start]] == *std::min_element(_cost[start].begin(), _cost[start].end())) continue;
            for(int firstSign : {0, 1}) {
                const int firstDelta = firstSign == 0 ? 1 : -1;
                std::ranges::fill(visited, 0);
                std::deque<uint32_t> queue{2 * start + firstSign};
                visited[2 * start + firstSign] = 1;
                parentBond[2 * start + firstSign] = -1;
                bool applied = false;
                while(!queue.empty() && !applied) {
                    const uint32_t state = queue.front();
                    queue.pop_front();
                    const uint32_t atom = state / 2;
                    const int delta = (state % 2 == 0) ? 1 : -1;
                    for(uint32_t b : _incident[atom]) {
                        if(_excess[b] + delta < 0 || _excess[b] + delta > 2) continue;
                        const uint32_t other = _bonds[b].first == atom ? _bonds[b].second : _bonds[b].first;
                        // End the path at 'other' (its valence changes by delta)?
                        const int64_t gain = (other == start) ? change(start, firstDelta + delta) : change(start, firstDelta) + change(other, delta);
                        if(gain < 0) {
                            // Collect the path and check that no bond leaves 0..2 (a walk may reuse a bond).
                            std::vector<std::pair<uint32_t, int>> steps{{b, delta}};
                            for(uint32_t s = state; parentBond[s] >= 0; s = parentState[s])
                                steps.emplace_back((uint32_t)parentBond[s], (parentState[s] % 2 == 0) ? 1 : -1);
                            std::unordered_map<uint32_t, int> net;
                            for(auto [bond, d] : steps) net[bond] += d;
                            if(std::ranges::all_of(net, [&](const auto& e) { return _excess[e.first] + e.second >= 0 && _excess[e.first] + e.second <= 2; })) {
                                for(auto [bond, d] : net) _excess[bond] += d;
                                const int64_t before = total;
                                total = value();
                                if(total < before) { improved = applied = true; computeSums(); break; }
                                for(auto [bond, d] : net) _excess[bond] -= d;   // Not better after all (repeated atoms).
                                total = before;
                            }
                        }
                        // Continue the path through 'other' with the opposite change.
                        const uint32_t next = 2 * other + ((delta == 1) ? 1 : 0);
                        if(!visited[next]) {
                            visited[next] = 1;
                            parentBond[next] = b;
                            parentState[next] = state;
                            queue.push_back(next);
                        }
                    }
                }
                if(applied) break;
            }
        }
        return improved;
    }

    /// Re-optimizes fragments of up to 'size' atoms around every atom. Returns true if the total penalty decreased.
    bool improveFragments(int size, int64_t& total, int64_t lowerBound) {
        bool improved = false;
        for(uint32_t seed = 0; seed < _cost.size() && total > lowerBound; seed++) {
            // Breadth-first fragment of up to 'size' atoms.
            std::vector<uint32_t> fragment{seed};
            for(size_t k = 0; k < fragment.size() && fragment.size() < (size_t)size; k++)
                for(uint32_t b : _incident[fragment[k]]) {
                    uint32_t other = _bonds[b].first == fragment[k] ? _bonds[b].second : _bonds[b].first;
                    if(std::ranges::find(fragment, other) == fragment.end() && fragment.size() < (size_t)size)
                        fragment.push_back(other);
                }
            std::vector<uint32_t> inner;
            for(uint32_t a : fragment)
                for(uint32_t b : _incident[a]) {
                    uint32_t other = _bonds[b].first == a ? _bonds[b].second : _bonds[b].first;
                    if(a < other && std::ranges::find(fragment, other) != fragment.end())
                        inner.push_back(b);
                }
            if(inner.empty()) continue;
            std::ranges::sort(inner);
            // Fix the bonds leaving the fragment; free the inner ones.
            int64_t before = 0;
            for(uint32_t a : fragment) {
                _cur[a] = 0; _rem[a] = 0;
                for(uint32_t b : _incident[a]) _cur[a] += _excess[b];
                before += _cost[a][_cur[a]];
            }
            for(uint32_t b : inner) {
                _cur[_bonds[b].first] -= _excess[b]; _cur[_bonds[b].second] -= _excess[b];
                _rem[_bonds[b].first]++; _rem[_bonds[b].second]++;
            }
            Search search = branchAndBound(inner, fragment, before, 100000);
            for(uint32_t a : fragment) { _cur[a] = 0; _rem[a] = 0; }
            if(search.value < before) {
                for(uint32_t b : inner) _excess[b] = search.excess[b];
                total += search.value - before;
                improved = true;
            }
        }
        return improved;
    }

    std::vector<std::pair<uint32_t, uint32_t>> _bonds;
    std::vector<std::vector<int64_t>> _cost;
    std::vector<std::vector<uint32_t>> _incident;
    std::vector<int> _cur;
    std::vector<int> _rem;
    std::vector<int8_t> _excess;
};

/// Union-find with path halving.
size_t findRoot(std::vector<size_t>& parent, size_t i)
{
    while(parent[i] != i) { parent[i] = parent[parent[i]]; i = parent[i]; }
    return i;
}

}   // End of anonymous namespace

/******************************************************************************
* Returns the atomic number of a chemical symbol, or 0 if it is not an element.
******************************************************************************/
int BondOrderModifier::atomicNumber(const QString& symbol)
{
    for(int z = 1; z < (int)std::size(elementSymbols); z++)
        if(symbol == QLatin1String(elementSymbols[z]))
            return z;
    return 0;
}

/******************************************************************************
* Returns the name of a functional group type.
******************************************************************************/
QString BondOrderModifier::groupName(int group)
{
    switch(group) {
    case Amide: return QStringLiteral("Amide");
    case Aromatic: return QStringLiteral("Aromatic");
    case Carboxylate: return QStringLiteral("Carboxylate");
    case Nitro: return QStringLiteral("Nitro");
    case Amine: return QStringLiteral("Amine");
    default: return QStringLiteral("None");
    }
}

/******************************************************************************
* Returns the color of a functional group type (as in OVITO Pro).
******************************************************************************/
Color BondOrderModifier::groupColor(int group)
{
    switch(group) {
    case Amide: return Color(0.906, 0.298, 0.235);
    case Aromatic: return Color(0.204, 0.596, 0.859);
    case Carboxylate: return Color(0.180, 0.800, 0.443);
    case Nitro: return Color(0.953, 0.612, 0.071);
    case Amine: return Color(0.608, 0.349, 0.714);
    default: return Color(0.949, 0.949, 0.949);
    }
}

/******************************************************************************
* Returns true if the element has a penalty table.
******************************************************************************/
bool BondOrderModifier::hasPenaltyTable(int z)
{
    return z == 6 || z == 7 || z == 8 || z == 14 || z == 15 || z == 16;
}

/******************************************************************************
* Connectivity from which all bonds of an atom are single (measured on OVITO Pro 3.16.1).
******************************************************************************/
int BondOrderModifier::maximumConnectivity(int z)
{
    switch(z) {
    case 1: case 3: case 9: case 11: case 17: case 19: case 35: case 37: case 53: case 55: case 81: case 85: case 87:
        return 1;
    case 47: case 80:
        return 2;
    case 8: case 83:
        return 3;
    case 15:
        return 5;
    case 16: case 20: case 22: case 24: case 25: case 26: case 27: case 38: case 42: case 43: case 44: case 45: case 56:
    case 74: case 75: case 76: case 77: case 88: case 89: case 90: case 91: case 92:
        return 6;
    default:
        if(z >= 58 && z <= 71) return 6;    // Ce..Lu
        if(z >= 93) return 6;               // Np..Lr
        return 4;
    }
}

/******************************************************************************
* Penalty of an atom with the given atomic number, connectivity and valence.
******************************************************************************/
int64_t BondOrderModifier::atomPenalty(int z, int connectivity, int valence, bool amideCarbon)
{
    if(!hasPenaltyTable(z))
        return 0;
    // A carbon with a terminal oxygen and a 3-fold coordinated nitrogen may have valence 5 (penalty 2); other carbons may not.
    if(amideCarbon && valence == 5)
        return 2;
    for(const PenaltyRow& row : penaltyTable) {
        if(row.z == z && row.connectivity == connectivity) {
            if(valence < 0 || valence >= (int)row.penalty.size() || row.penalty[valence] == Unlisted)
                return ForbiddenPenalty;
            return row.penalty[valence];
        }
    }
    return ForbiddenPenalty;
}

/******************************************************************************
* Flags the carbons with three bonds, a terminal oxygen and a 3-fold coordinated nitrogen.
******************************************************************************/
std::vector<bool> BondOrderModifier::amideCarbons(const std::vector<int>& atomicNumbers, const std::vector<std::pair<size_t, size_t>>& bonds)
{
    std::vector<int> connectivity(atomicNumbers.size(), 0);
    for(const auto& [a, b] : bonds)
        if(a != b) { connectivity[a]++; connectivity[b]++; }
    std::vector<bool> terminalOxygen(atomicNumbers.size(), false), nitrogen(atomicNumbers.size(), false);
    for(const auto& [a, b] : bonds) {
        for(auto [i, j] : {std::make_pair(a, b), std::make_pair(b, a)}) {
            if(atomicNumbers[j] == 8 && connectivity[j] == 1) terminalOxygen[i] = true;
            if(atomicNumbers[j] == 7 && connectivity[j] == 3) nitrogen[i] = true;
        }
    }
    std::vector<bool> result(atomicNumbers.size());
    for(size_t i = 0; i < atomicNumbers.size(); i++)
        result[i] = atomicNumbers[i] == 6 && connectivity[i] == 3 && terminalOxygen[i] && nitrogen[i];
    return result;
}

/******************************************************************************
* Assigns bond orders and functional groups.
******************************************************************************/
BondOrderModifier::Result BondOrderModifier::compute(const std::vector<int>& atomicNumbers, const std::vector<std::pair<size_t, size_t>>& bonds,
                                                     int fragmentSizeLimit, bool redistributeBondOrders, size_t nodeBudget)
{
    const size_t atomCount = atomicNumbers.size();
    const size_t bondCount = bonds.size();
    Result result;
    result.integerOrders.assign(bondCount, 1);

    std::vector<int> connectivity(atomCount, 0);
    for(const auto& [a, b] : bonds) {
        if(a >= atomCount || b >= atomCount)
            throw Exception(QStringLiteral("Bond order: bond refers to a non-existent particle."));
        if(a != b) { connectivity[a]++; connectivity[b]++; }
    }
    std::vector<bool> saturated(atomCount);
    for(size_t i = 0; i < atomCount; i++)
        saturated[i] = connectivity[i] >= maximumConnectivity(atomicNumbers[i]);
    const std::vector<bool> amideCarbon = amideCarbons(atomicNumbers, bonds);

    // The C=O bonds of amide carbons are double from the start (as in OVITO Pro).
    std::vector<int> fixedExcess(atomCount, 0);
    for(size_t b = 0; b < bondCount; b++) {
        auto [i, j] = bonds[b];
        if(amideCarbon[j]) std::swap(i, j);
        if(amideCarbon[i] && atomicNumbers[j] == 8 && connectivity[j] == 1) {
            result.integerOrders[b] = 2;
            fixedExcess[i]++;
            fixedExcess[j]++;
        }
    }

    // Optimizable bonds: both atoms below their maximum connectivity, at least one of them with a penalty table.
    auto isFree = [&](size_t b) {
        auto [i, j] = bonds[b];
        return i != j && result.integerOrders[b] == 1 && !saturated[i] && !saturated[j]
            && (hasPenaltyTable(atomicNumbers[i]) || hasPenaltyTable(atomicNumbers[j]));
    };
    std::vector<size_t> parent(atomCount);
    std::iota(parent.begin(), parent.end(), 0);
    std::vector<size_t> freeBonds;
    for(size_t b = 0; b < bondCount; b++) {
        if(!isFree(b)) continue;
        freeBonds.push_back(b);
        parent[findRoot(parent, bonds[b].first)] = findRoot(parent, bonds[b].second);
    }
    // Visit bonds in order of their particle indices (this order decides between equally good assignments).
    std::ranges::sort(freeBonds, [&](size_t b1, size_t b2) {
        auto key = [&](size_t b) { return std::make_tuple(std::min(bonds[b].first, bonds[b].second), std::max(bonds[b].first, bonds[b].second), b); };
        return key(b1) < key(b2);
    });
    std::map<size_t, std::vector<size_t>> components;
    for(size_t b : freeBonds)
        components[findRoot(parent, bonds[b].first)].push_back(b);

    std::vector<int> excessSum(atomCount, 0);
    for(const auto& [root, componentBonds] : components) {
        std::unordered_map<size_t, uint32_t> localIndex;
        std::vector<size_t> globalIndex;
        auto local = [&](size_t i) {
            auto [it, inserted] = localIndex.emplace(i, (uint32_t)globalIndex.size());
            if(inserted) globalIndex.push_back(i);
            return it->second;
        };
        std::vector<std::pair<uint32_t, uint32_t>> localBonds;
        for(size_t b : componentBonds)
            localBonds.emplace_back(local(bonds[b].first), local(bonds[b].second));
        std::vector<int> degree(globalIndex.size(), 0);
        for(const auto& [a, b] : localBonds) { degree[a]++; degree[b]++; }
        std::vector<std::vector<int64_t>> cost(globalIndex.size());
        for(size_t a = 0; a < globalIndex.size(); a++) {
            const size_t i = globalIndex[a];
            for(int x = 0; x <= 2 * degree[a]; x++)
                cost[a].push_back(atomPenalty(atomicNumbers[i], connectivity[i], connectivity[i] + fixedExcess[i] + x, amideCarbon[i]));
        }
        ComponentSolver solver(std::move(localBonds), std::move(cost));
        if(!solver.solve(fragmentSizeLimit, nodeBudget))
            result.heuristicComponents++;
        for(size_t k = 0; k < componentBonds.size(); k++) {
            const size_t b = componentBonds[k];
            result.integerOrders[b] = 1 + solver.excess()[k];
            excessSum[bonds[b].first] += solver.excess()[k];
            excessSum[bonds[b].second] += solver.excess()[k];
        }
    }
    std::vector<int> valence(atomCount);
    for(size_t i = 0; i < atomCount; i++) {
        valence[i] = connectivity[i] + fixedExcess[i] + excessSum[i];
        result.penaltyScore += atomPenalty(atomicNumbers[i], connectivity[i], valence[i], amideCarbon[i]);
    }

    // Neighbor lists (particle, bond).
    std::vector<std::vector<std::pair<size_t, size_t>>> neighbors(atomCount);
    for(size_t b = 0; b < bondCount; b++) {
        auto [i, j] = bonds[b];
        if(i == j) continue;
        neighbors[i].emplace_back(j, b);
        neighbors[j].emplace_back(i, b);
    }
    auto bondBetween = [&](size_t i, size_t j) -> size_t {
        for(const auto& [k, b] : neighbors[i]) if(k == j) return b;
        return std::numeric_limits<size_t>::max();
    };

    // Chordless 5- and 6-rings of C, N, O, S atoms with at most three bonds.
    auto ringCandidate = [&](size_t i) {
        const int z = atomicNumbers[i];
        return (z == 6 || z == 7 || z == 8 || z == 16) && connectivity[i] <= 3;
    };
    std::vector<std::vector<size_t>> rings;
    std::vector<size_t> path;
    std::function<void(size_t)> extend = [&](size_t current) {
        for(const auto& [next, b] : neighbors[current]) {
            if(next == path.front()) {
                if(path.size() >= 5 && path[1] < path.back())
                    rings.push_back(path);
                continue;
            }
            if(next < path.front() || path.size() == 6 || !ringCandidate(next) || std::ranges::find(path, next) != path.end())
                continue;
            path.push_back(next);
            extend(next);
            path.pop_back();
        }
    };
    for(size_t s = 0; s < atomCount; s++) {
        if(!ringCandidate(s)) continue;
        path.assign(1, s);
        extend(s);
    }
    std::erase_if(rings, [&](const std::vector<size_t>& ring) {
        for(size_t p = 0; p < ring.size(); p++)
            for(size_t q = p + 2; q < ring.size(); q++)
                if(!(p == 0 && q == ring.size() - 1) && bondBetween(ring[p], ring[q]) != std::numeric_limits<size_t>::max())
                    return true;
        return false;
    });

    // Hueckel's rule; atoms in a ring found aromatic in an earlier pass contribute one electron.
    std::vector<bool> aromaticAtom(atomCount, false);
    std::vector<bool> aromaticRingBond(bondCount, false);
    std::vector<bool> aromaticRing(rings.size(), false);
    auto piElectrons = [&](const std::vector<size_t>& ring, size_t p) -> int {
        const size_t i = ring[p];
        if(aromaticAtom[i]) return 1;
        const size_t prev = bondBetween(i, ring[(p + ring.size() - 1) % ring.size()]);
        const size_t next = bondBetween(i, ring[(p + 1) % ring.size()]);
        int inner = 0, outer = 0;
        for(const auto& [k, b] : neighbors[i]) {
            const int order = result.integerOrders[b];
            if(order == 3) return -1;
            if(order == 2) (b == prev || b == next) ? inner++ : outer++;
        }
        switch(atomicNumbers[i]) {
        case 6:
            if(valence[i] != 4) return -1;
            if(inner == 1 && outer == 0) return 1;
            if(inner == 0 && outer == 1) return 0;
            return -1;
        case 7:
            if(inner == 1 && outer == 0) return 1;
            if(connectivity[i] == 3 && inner == 0 && outer == 0) return 2;
            return -1;
        default:    // O, S
            return (connectivity[i] == 2 && valence[i] == 2) ? 2 : -1;
        }
    };
    for(bool changed = true; changed; ) {
        changed = false;
        std::vector<size_t> found;
        for(size_t r = 0; r < rings.size(); r++) {
            if(aromaticRing[r]) continue;
            int electrons = 0;
            for(size_t p = 0; p < rings[r].size() && electrons >= 0; p++) {
                const int e = piElectrons(rings[r], p);
                electrons = (e < 0) ? -1 : electrons + e;
            }
            if(electrons >= 2 && electrons % 4 == 2)
                found.push_back(r);
        }
        for(size_t r : found) {
            aromaticRing[r] = true;
            changed = true;
            const auto& ring = rings[r];
            for(size_t p = 0; p < ring.size(); p++) {
                aromaticAtom[ring[p]] = true;
                aromaticRingBond[bondBetween(ring[p], ring[(p + 1) % ring.size()])] = true;
            }
        }
    }

    // Functional groups. Atoms keep the first group assigned.
    result.particleGroups.assign(atomCount, NoGroup);
    auto& groups = result.particleGroups;
    for(size_t i = 0; i < atomCount; i++)
        if(aromaticAtom[i]) groups[i] = Aromatic;
    auto terminalOxygens = [&](size_t i) {
        std::vector<size_t> oxygens;
        for(const auto& [k, b] : neighbors[i])
            if(atomicNumbers[k] == 8 && connectivity[k] == 1) oxygens.push_back(k);
        return oxygens;
    };
    for(size_t i = 0; i < atomCount; i++) {
        if(groups[i] != NoGroup || connectivity[i] != 3 || (atomicNumbers[i] != 6 && atomicNumbers[i] != 7)) continue;
        std::vector<size_t> oxygens = terminalOxygens(i);
        if(oxygens.size() < 2) continue;
        const int32_t group = (atomicNumbers[i] == 6) ? Carboxylate : Nitro;
        groups[i] = group;
        for(size_t k : oxygens) if(groups[k] == NoGroup) groups[k] = group;
    }
    for(size_t i = 0; i < atomCount; i++) {
        // Amide: C(=O)-N with a terminal oxygen.
        if(atomicNumbers[i] != 6 || connectivity[i] != 3 || groups[i] == Carboxylate) continue;
        size_t oxygen = std::numeric_limits<size_t>::max();
        std::vector<size_t> nitrogens;
        for(const auto& [k, b] : neighbors[i]) {
            if(atomicNumbers[k] == 8 && connectivity[k] == 1 && result.integerOrders[b] == 2) oxygen = k;
            else if(atomicNumbers[k] == 7 && connectivity[k] == 3 && result.integerOrders[b] == 1) nitrogens.push_back(k);
        }
        if(oxygen == std::numeric_limits<size_t>::max() || nitrogens.empty()) continue;
        if(groups[i] == NoGroup) groups[i] = Amide;
        if(groups[oxygen] == NoGroup) groups[oxygen] = Amide;
        for(size_t k : nitrogens) if(groups[k] == NoGroup) groups[k] = Amide;
    }
    for(size_t i = 0; i < atomCount; i++) {
        // Amine: NH2 bonded to an aromatic or unsaturated atom.
        if(atomicNumbers[i] != 7 || connectivity[i] != 3 || valence[i] != 3 || groups[i] != NoGroup) continue;
        std::vector<size_t> hydrogens;
        size_t heavy = std::numeric_limits<size_t>::max();
        for(const auto& [k, b] : neighbors[i]) {
            if(atomicNumbers[k] == 1) hydrogens.push_back(k);
            else heavy = k;
        }
        if(hydrogens.size() != 2 || heavy == std::numeric_limits<size_t>::max()) continue;
        if(!aromaticAtom[heavy] && valence[heavy] == connectivity[heavy]) continue;
        groups[i] = Amine;
        for(size_t k : hydrogens) if(groups[k] == NoGroup) groups[k] = Amine;
    }
    result.bondGroups.assign(bondCount, NoGroup);
    for(size_t b = 0; b < bondCount; b++) {
        auto [i, j] = bonds[b];
        if(i != j && groups[i] != NoGroup && groups[i] == groups[j])
            result.bondGroups[b] = groups[i];
    }

    // Fractional bond orders.
    result.bondOrders.assign(result.integerOrders.begin(), result.integerOrders.end());
    if(redistributeBondOrders) {
        for(size_t b = 0; b < bondCount; b++) {
            if(aromaticRingBond[b]) result.bondOrders[b] = 1.5;
            else if(result.bondGroups[b] == Amide) result.bondOrders[b] = 1.41;   // UFF amide bond order, as in OVITO Pro.
        }
        // Carboxylate and nitro: the C-O / N-O bonds share their total order evenly.
        for(size_t i = 0; i < atomCount; i++) {
            if((groups[i] != Carboxylate && groups[i] != Nitro) || (atomicNumbers[i] != 6 && atomicNumbers[i] != 7)) continue;
            int total = 0, count = 0;
            for(const auto& [k, b] : neighbors[i])
                if(result.bondGroups[b] == groups[i]) { total += result.integerOrders[b]; count++; }
            for(const auto& [k, b] : neighbors[i])
                if(result.bondGroups[b] == groups[i]) result.bondOrders[b] = FloatType(total) / count;
        }
    }
    return result;
}

/******************************************************************************
* Asks the metaclass whether the modifier can be applied to the given input data.
******************************************************************************/
bool BondOrderModifier::OOMetaClass::isApplicableTo(const DataCollection& input) const
{
    const Particles* particles = input.getObject<Particles>();
    return particles && particles->bonds();
}

/******************************************************************************
* Is called by the pipeline system before a new modifier evaluation begins.
******************************************************************************/
void BondOrderModifier::preevaluateModifier(const ModifierEvaluationRequest& request, PipelineEvaluationResult::EvaluationTypes& evaluationTypes, TimeInterval& validityInterval) const
{
    if(request.interactiveMode())
        evaluationTypes = PipelineEvaluationResult::EvaluationType::Interactive;
    else
        evaluationTypes = PipelineEvaluationResult::EvaluationType::Noninteractive;
}

/******************************************************************************
* Modifies the input data.
******************************************************************************/
Future<PipelineFlowState> BondOrderModifier::evaluateModifier(const ModifierEvaluationRequest& request, PipelineFlowState&& state)
{
    if(fragmentSizeLimit() < 3)
        throw Exception(tr("The molecular fragment size limit must be at least 3."));

    Particles* particles = state.expectMutableObject<Particles>();
    particles->verifyIntegrity();
    const Bonds* bonds = particles->expectBonds();
    bonds->verifyIntegrity();

    // In interactive mode, do not perform a real computation. Instead, reuse old results if available in the pipeline cache.
    if(request.interactiveMode()) {
        if(PipelineFlowState cachedState = request.modificationNode()->getCachedPipelineNodeOutput(request.time(), true)) {
            if(const Particles* cachedParticles = cachedState.getObject<Particles>()) {
                particles->tryToAdoptProperties(cachedParticles, { cachedParticles->getProperty(QStringLiteral("Functional Group")) }, {particles});
                if(const Bonds* cachedBonds = cachedParticles->bonds()) {
                    if(cachedBonds->elementCount() == bonds->elementCount()) {
                        Bonds* mutableBonds = particles->makeBondsMutable();
                        mutableBonds->tryToAdoptProperties(cachedBonds, { cachedBonds->getProperty(QStringLiteral("Bond Order")),
                            cachedBonds->getProperty(QStringLiteral("Functional Group")) }, {particles, mutableBonds});
                    }
                }
            }
            state.adoptAttributesFrom(cachedState, request.modificationNode());
        }
        return std::move(state);
    }

    // Map the particle types to chemical elements.
    const Property* typeProperty = particles->expectProperty(Particles::TypeProperty);
    std::vector<int> atomicNumbers(particles->elementCount());
    {
        std::map<int, int> typeToElement;
        BufferReadAccess<int32_t> types(typeProperty);
        for(size_t i = 0; i < atomicNumbers.size(); i++) {
            auto it = typeToElement.find(types[i]);
            if(it == typeToElement.end()) {
                const ElementType* type = typeProperty->elementType(types[i]);
                const QString name = type ? type->nameOrNumericId() : QString::number(types[i]);
                const int z = atomicNumber(name);
                if(z == 0)
                    throw Exception(tr("Unknown particle type '%1': All particle type names must exactly match chemical symbols from the periodic table.").arg(name));
                it = typeToElement.emplace(types[i], z).first;
            }
            atomicNumbers[i] = it->second;
        }
    }

    return asyncLaunch([
            state = std::move(state),
            particles,
            bonds,
            atomicNumbers = std::move(atomicNumbers),
            fragmentSizeLimit = fragmentSizeLimit(),
            redistribute = redistributeBondOrders(),
            createdByNode = request.modificationNodeWeak()]() mutable
    {
        TaskProgress progress(this_task::ui());
        progress.setText(tr("Assigning bond orders"));

        std::vector<std::pair<size_t, size_t>> bondList;
        {
            BufferReadAccess<ParticleIndexPair> topology(bonds->expectProperty(Bonds::TopologyProperty));
            bondList.reserve(topology.size());
            for(const ParticleIndexPair& pair : topology)
                bondList.emplace_back((size_t)pair[0], (size_t)pair[1]);
        }
        Result result = compute(atomicNumbers, bondList, fragmentSizeLimit, redistribute);

        auto addGroupTypes = [](Property* property) {
            for(int g = 0; g < NumGroups; g++) {
                DataOORef<ElementType> type = DataOORef<ElementType>::create();
                type->setNumericId(g);
                type->setName(groupName(g));
                type->setColor(groupColor(g));
                property->addElementType(std::move(type));
            }
        };
        Property* particleGroups = particles->createProperty(DataBuffer::Uninitialized, QStringLiteral("Functional Group"), Property::Int32);
        std::ranges::copy(result.particleGroups, BufferWriteAccess<int32_t, access_mode::discard_write>(particleGroups).begin());
        addGroupTypes(particleGroups);

        Bonds* mutableBonds = particles->makeBondsMutable();
        Property* orders = mutableBonds->createProperty(DataBuffer::Uninitialized, QStringLiteral("Bond Order"), Property::FloatDefault);
        std::ranges::copy(result.bondOrders, BufferWriteAccess<FloatType, access_mode::discard_write>(orders).begin());
        Property* bondGroups = mutableBonds->createProperty(DataBuffer::Uninitialized, QStringLiteral("Functional Group"), Property::Int32);
        std::ranges::copy(result.bondGroups, BufferWriteAccess<int32_t, access_mode::discard_write>(bondGroups).begin());
        addGroupTypes(bondGroups);

        state.addAttribute(QStringLiteral("BondOrder.PenaltyScore"), QVariant::fromValue((qlonglong)result.penaltyScore), createdByNode);

        QString text = tr("Penalty score: %1").arg(result.penaltyScore);
        if(result.heuristicComponents != 0) {
            text += tr("\n%1 molecular fragment(s) too large for the exact optimization were optimized locally; "
                       "the assignment there may not be optimal. Increase the fragment size limit to improve it.").arg(result.heuristicComponents);
            state.setStatus(PipelineStatus(PipelineStatus::Warning, std::move(text)));
        }
        else {
            state.setStatus(PipelineStatus(PipelineStatus::Success, std::move(text)));
        }
        return std::move(state);
    });
}

}   // End of namespace
