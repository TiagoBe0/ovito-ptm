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

#pragma once

#include <ovito/core/Core.h>

namespace Ovito {

/// Chemical elements, indexed by atomic number ("X" = unspecified, deuterium gets its own index).
/// Same numbering as ParticleType::ChemicalElement of OVITO 3.16, which the fork's ParticleType (3.14) does not have yet.
// clang-format off
enum class ChemicalElement: uint_fast8_t
{
        X, H, He, Li, Be, B, C, N, O, F, Ne, Na, Mg, Al, Si, P, S, Cl, Ar, K,
        Ca, Sc, Ti, V, Cr, Mn, Fe, Co, Ni, Cu, Zn, Ga, Ge, As, Se, Br, Kr, Rb,
        Sr, Y, Zr, Nb, Mo, Tc, Ru, Rh, Pd, Ag, Cd, In, Sn, Sb, Te, I, Xe, Cs,
        Ba, La, Ce, Pr, Nd, Pm, Sm, Eu, Gd, Tb, Dy, Ho, Er, Tm, Yb, Lu, Hf, Ta,
        W, Re, Os, Ir, Pt, Au, Hg, Tl, Pb, Bi, Po, At, Rn, Fr, Ra, Ac, Th, Pa, U,
        Np, Pu, Am, Cm, Bk, Cf, Es, Fm, Md, No, Lr, Rf, Db, Sg, Bh, Hs, Mt, Ds, Rg,
        Cn, Nh, Fl, Mc, Lv, Ts, Og, D,
        NUMBER_OF_PREDEFINED_CHEMICAL_TYPES
};
// clang-format on

/// Returns the element with the given symbol, or ChemicalElement::X if the symbol is not a chemical element.
inline ChemicalElement chemicalElementFromSymbol(const QString& symbol)
{
    static constexpr const char* symbols[] = { "X", "H", "He", "Li", "Be", "B", "C", "N", "O", "F", "Ne", "Na", "Mg", "Al", "Si", "P", "S", "Cl", "Ar", "K", "Ca", "Sc", "Ti", "V", "Cr", "Mn", "Fe", "Co", "Ni", "Cu", "Zn", "Ga", "Ge", "As", "Se", "Br", "Kr", "Rb", "Sr", "Y", "Zr", "Nb", "Mo", "Tc", "Ru", "Rh", "Pd", "Ag", "Cd", "In", "Sn", "Sb", "Te", "I", "Xe", "Cs", "Ba", "La", "Ce", "Pr", "Nd", "Pm", "Sm", "Eu", "Gd", "Tb", "Dy", "Ho", "Er", "Tm", "Yb", "Lu", "Hf", "Ta", "W", "Re", "Os", "Ir", "Pt", "Au", "Hg", "Tl", "Pb", "Bi", "Po", "At", "Rn", "Fr", "Ra", "Ac", "Th", "Pa", "U", "Np", "Pu", "Am", "Cm", "Bk", "Cf", "Es", "Fm", "Md", "No", "Lr", "Rf", "Db", "Sg", "Bh", "Hs", "Mt", "Ds", "Rg", "Cn", "Nh", "Fl", "Mc", "Lv", "Ts", "Og", "D" };
    static_assert(std::size(symbols) == (size_t)ChemicalElement::NUMBER_OF_PREDEFINED_CHEMICAL_TYPES);
    for(size_t i = 1; i < std::size(symbols); i++)
        if(symbol == QLatin1String(symbols[i]))
            return (ChemicalElement)i;
    return ChemicalElement::X;
}

namespace {
consteval auto getCovalentRadii()
{
    // Data from doi.org/10.1002/jcc.540120716
    // "Determination of Molecular Topology and Atomic Hybridization States from Heavy Atom Coordinates",
    // Elaine C. Meng* and Richard A. Lewis, 1991
    std::array<FloatType, (size_t)ChemicalElement::NUMBER_OF_PREDEFINED_CHEMICAL_TYPES> covRadii{
        std::numeric_limits<FloatType>::quiet_NaN()};
    covRadii[(size_t)ChemicalElement::Ac] = 1.88;
    covRadii[(size_t)ChemicalElement::Er] = 1.73;
    covRadii[(size_t)ChemicalElement::Na] = 0.97;
    covRadii[(size_t)ChemicalElement::Sb] = 1.46;
    covRadii[(size_t)ChemicalElement::Ag] = 1.59;
    covRadii[(size_t)ChemicalElement::Eu] = 1.99;
    covRadii[(size_t)ChemicalElement::Nb] = 1.48;
    covRadii[(size_t)ChemicalElement::Sc] = 1.44;
    covRadii[(size_t)ChemicalElement::Al] = 1.35;
    covRadii[(size_t)ChemicalElement::F] = 0.64;
    covRadii[(size_t)ChemicalElement::Nd] = 1.81;
    covRadii[(size_t)ChemicalElement::Se] = 1.22;
    covRadii[(size_t)ChemicalElement::Am] = 1.51;
    covRadii[(size_t)ChemicalElement::Fe] = 1.34;
    covRadii[(size_t)ChemicalElement::Ni] = 1.50;
    covRadii[(size_t)ChemicalElement::Si] = 1.20;
    covRadii[(size_t)ChemicalElement::As] = 1.21;
    covRadii[(size_t)ChemicalElement::Ga] = 1.22;
    covRadii[(size_t)ChemicalElement::Np] = 1.55;
    covRadii[(size_t)ChemicalElement::Sm] = 1.80;
    covRadii[(size_t)ChemicalElement::Au] = 1.50;
    covRadii[(size_t)ChemicalElement::Gd] = 1.79;
    covRadii[(size_t)ChemicalElement::O] = 0.68;
    covRadii[(size_t)ChemicalElement::Sn] = 1.46;
    covRadii[(size_t)ChemicalElement::B] = 0.83;
    covRadii[(size_t)ChemicalElement::Ge] = 1.17;
    covRadii[(size_t)ChemicalElement::Os] = 1.37;
    covRadii[(size_t)ChemicalElement::Sr] = 1.12;
    covRadii[(size_t)ChemicalElement::Ba] = 1.34;
    covRadii[(size_t)ChemicalElement::H] = 0.23;
    covRadii[(size_t)ChemicalElement::P] = 1.05;
    covRadii[(size_t)ChemicalElement::Ta] = 1.43;
    covRadii[(size_t)ChemicalElement::Be] = 0.35;
    covRadii[(size_t)ChemicalElement::Hf] = 1.57;
    covRadii[(size_t)ChemicalElement::Pa] = 1.61;
    covRadii[(size_t)ChemicalElement::Tb] = 1.76;
    covRadii[(size_t)ChemicalElement::Bi] = 1.54;
    covRadii[(size_t)ChemicalElement::Hg] = 1.70;
    covRadii[(size_t)ChemicalElement::Pb] = 1.54;
    covRadii[(size_t)ChemicalElement::Tc] = 1.35;
    covRadii[(size_t)ChemicalElement::Br] = 1.21;
    covRadii[(size_t)ChemicalElement::Ho] = 1.74;
    covRadii[(size_t)ChemicalElement::Pd] = 1.50;
    covRadii[(size_t)ChemicalElement::Te] = 1.47;
    covRadii[(size_t)ChemicalElement::C] = 0.68;
    covRadii[(size_t)ChemicalElement::I] = 1.40;
    covRadii[(size_t)ChemicalElement::Pm] = 1.80;
    covRadii[(size_t)ChemicalElement::Th] = 1.79;
    covRadii[(size_t)ChemicalElement::Ca] = 0.99;
    covRadii[(size_t)ChemicalElement::In] = 1.63;
    covRadii[(size_t)ChemicalElement::Po] = 1.68;
    covRadii[(size_t)ChemicalElement::Ti] = 1.47;
    covRadii[(size_t)ChemicalElement::Cd] = 1.69;
    covRadii[(size_t)ChemicalElement::Ir] = 1.32;
    covRadii[(size_t)ChemicalElement::Pr] = 1.82;
    covRadii[(size_t)ChemicalElement::Tl] = 1.55;
    covRadii[(size_t)ChemicalElement::Ce] = 1.83;
    covRadii[(size_t)ChemicalElement::K] = 1.33;
    covRadii[(size_t)ChemicalElement::Pt] = 1.50;
    covRadii[(size_t)ChemicalElement::Tm] = 1.72;
    covRadii[(size_t)ChemicalElement::Cl] = 0.99;
    covRadii[(size_t)ChemicalElement::La] = 1.87;
    covRadii[(size_t)ChemicalElement::Pu] = 1.53;
    covRadii[(size_t)ChemicalElement::U] = 1.58;
    covRadii[(size_t)ChemicalElement::Co] = 1.33;
    covRadii[(size_t)ChemicalElement::Li] = 0.68;
    covRadii[(size_t)ChemicalElement::Ra] = 1.90;
    covRadii[(size_t)ChemicalElement::V] = 1.33;
    covRadii[(size_t)ChemicalElement::Cr] = 1.35;
    covRadii[(size_t)ChemicalElement::Lu] = 1.72;
    covRadii[(size_t)ChemicalElement::Rb] = 1.47;
    covRadii[(size_t)ChemicalElement::W] = 1.37;
    covRadii[(size_t)ChemicalElement::Cs] = 1.67;
    covRadii[(size_t)ChemicalElement::Mg] = 1.10;
    covRadii[(size_t)ChemicalElement::Re] = 1.35;
    covRadii[(size_t)ChemicalElement::Y] = 1.78;
    covRadii[(size_t)ChemicalElement::Cu] = 1.52;
    covRadii[(size_t)ChemicalElement::Mn] = 1.35;
    covRadii[(size_t)ChemicalElement::Rh] = 1.45;
    covRadii[(size_t)ChemicalElement::Yb] = 1.94;
    covRadii[(size_t)ChemicalElement::Mo] = 1.47;
    covRadii[(size_t)ChemicalElement::Ru] = 1.40;
    covRadii[(size_t)ChemicalElement::Zn] = 1.45;
    covRadii[(size_t)ChemicalElement::D] = 0.23;
    covRadii[(size_t)ChemicalElement::Dy] = 1.75;
    covRadii[(size_t)ChemicalElement::N] = 0.68;
    covRadii[(size_t)ChemicalElement::S] = 1.02;
    covRadii[(size_t)ChemicalElement::Zr] = 1.56;
    return covRadii;
}
inline constexpr auto _covalentRadii = getCovalentRadii();

consteval auto getMaxiumCoordination()
{
    // Adapted from supplementary material of doi.org/10.1002/jcc.24309
    // https://onlinelibrary.wiley.com/action/downloadSupplement?doi=10.1002%2Fjcc.24309&file=jcc24309-sup-0001-suppinfo.pdf
    // Automatic Molecular Structure Perception for the Universal Force Field
    // Svetlana Artemova, Leonard Jaillet, and Stephane Redon
    // Journal of Computational Chemistry 2016, 37, 1191–1205
    std::array<uint8_t, (size_t)ChemicalElement::NUMBER_OF_PREDEFINED_CHEMICAL_TYPES> maxCoord{0};
    maxCoord[(size_t)ChemicalElement::H] = 1;
    maxCoord[(size_t)ChemicalElement::He] = 4;
    maxCoord[(size_t)ChemicalElement::Li] = 1;
    maxCoord[(size_t)ChemicalElement::Be] = 4;
    maxCoord[(size_t)ChemicalElement::B] = 4;
    maxCoord[(size_t)ChemicalElement::C] = 4;
    maxCoord[(size_t)ChemicalElement::N] = 4;
    maxCoord[(size_t)ChemicalElement::O] = 2;
    maxCoord[(size_t)ChemicalElement::F] = 1;
    maxCoord[(size_t)ChemicalElement::Ne] = 4;
    maxCoord[(size_t)ChemicalElement::Na] = 1;
    maxCoord[(size_t)ChemicalElement::Mg] = 4;
    maxCoord[(size_t)ChemicalElement::Al] = 4;
    maxCoord[(size_t)ChemicalElement::Si] = 4;
    maxCoord[(size_t)ChemicalElement::P] = 4;
    maxCoord[(size_t)ChemicalElement::S] = 4;
    maxCoord[(size_t)ChemicalElement::Cl] = 1;
    maxCoord[(size_t)ChemicalElement::Ar] = 4;
    maxCoord[(size_t)ChemicalElement::K] = 1;
    maxCoord[(size_t)ChemicalElement::Ca] = 6;
    maxCoord[(size_t)ChemicalElement::Sc] = 4;
    maxCoord[(size_t)ChemicalElement::Ti] = 6;
    maxCoord[(size_t)ChemicalElement::V] = 4;
    maxCoord[(size_t)ChemicalElement::Cr] = 6;
    maxCoord[(size_t)ChemicalElement::Mn] = 6;
    maxCoord[(size_t)ChemicalElement::Fe] = 6;
    maxCoord[(size_t)ChemicalElement::Co] = 6;
    maxCoord[(size_t)ChemicalElement::Ni] = 4;
    maxCoord[(size_t)ChemicalElement::Cu] = 4;
    maxCoord[(size_t)ChemicalElement::Zn] = 4;
    maxCoord[(size_t)ChemicalElement::Ga] = 4;
    maxCoord[(size_t)ChemicalElement::Ge] = 4;
    maxCoord[(size_t)ChemicalElement::As] = 4;
    maxCoord[(size_t)ChemicalElement::Se] = 4;
    maxCoord[(size_t)ChemicalElement::Br] = 1;
    maxCoord[(size_t)ChemicalElement::Kr] = 4;
    maxCoord[(size_t)ChemicalElement::Rb] = 1;
    maxCoord[(size_t)ChemicalElement::Sr] = 6;
    maxCoord[(size_t)ChemicalElement::Y] = 4;
    maxCoord[(size_t)ChemicalElement::Zr] = 4;
    maxCoord[(size_t)ChemicalElement::Nb] = 4;
    maxCoord[(size_t)ChemicalElement::Mo] = 6;
    maxCoord[(size_t)ChemicalElement::Tc] = 6;
    maxCoord[(size_t)ChemicalElement::Ru] = 6;
    maxCoord[(size_t)ChemicalElement::Rh] = 6;
    maxCoord[(size_t)ChemicalElement::Pd] = 4;
    maxCoord[(size_t)ChemicalElement::Ag] = 2;
    maxCoord[(size_t)ChemicalElement::Cd] = 4;
    maxCoord[(size_t)ChemicalElement::In] = 4;
    maxCoord[(size_t)ChemicalElement::Sn] = 4;
    maxCoord[(size_t)ChemicalElement::Sb] = 4;
    maxCoord[(size_t)ChemicalElement::Te] = 4;
    maxCoord[(size_t)ChemicalElement::I] = 1;
    maxCoord[(size_t)ChemicalElement::Xe] = 4;
    maxCoord[(size_t)ChemicalElement::Cs] = 1;
    maxCoord[(size_t)ChemicalElement::Ba] = 6;
    maxCoord[(size_t)ChemicalElement::Hf] = 4;
    maxCoord[(size_t)ChemicalElement::Ta] = 4;
    maxCoord[(size_t)ChemicalElement::W] = 6;
    maxCoord[(size_t)ChemicalElement::Re] = 6;
    maxCoord[(size_t)ChemicalElement::Os] = 6;
    maxCoord[(size_t)ChemicalElement::Ir] = 6;
    maxCoord[(size_t)ChemicalElement::Pt] = 4;
    maxCoord[(size_t)ChemicalElement::Au] = 4;
    maxCoord[(size_t)ChemicalElement::Hg] = 2;
    maxCoord[(size_t)ChemicalElement::Tl] = 1;
    maxCoord[(size_t)ChemicalElement::Pb] = 4;
    maxCoord[(size_t)ChemicalElement::Bi] = 3;
    maxCoord[(size_t)ChemicalElement::Po] = 4;
    maxCoord[(size_t)ChemicalElement::At] = 1;
    maxCoord[(size_t)ChemicalElement::Rn] = 4;
    maxCoord[(size_t)ChemicalElement::Fr] = 1;
    maxCoord[(size_t)ChemicalElement::Ra] = 6;
    maxCoord[(size_t)ChemicalElement::La] = 4;
    maxCoord[(size_t)ChemicalElement::Ce] = 6;
    maxCoord[(size_t)ChemicalElement::Pr] = 6;
    maxCoord[(size_t)ChemicalElement::Nd] = 6;
    maxCoord[(size_t)ChemicalElement::Pm] = 6;
    maxCoord[(size_t)ChemicalElement::Sm] = 6;
    maxCoord[(size_t)ChemicalElement::Eu] = 6;
    maxCoord[(size_t)ChemicalElement::Gd] = 6;
    maxCoord[(size_t)ChemicalElement::Tb] = 6;
    maxCoord[(size_t)ChemicalElement::Dy] = 6;
    maxCoord[(size_t)ChemicalElement::Ho] = 6;
    maxCoord[(size_t)ChemicalElement::Er] = 6;
    maxCoord[(size_t)ChemicalElement::Tm] = 6;
    maxCoord[(size_t)ChemicalElement::Yb] = 6;
    maxCoord[(size_t)ChemicalElement::Lu] = 6;
    maxCoord[(size_t)ChemicalElement::Ac] = 6;
    maxCoord[(size_t)ChemicalElement::Th] = 6;
    maxCoord[(size_t)ChemicalElement::Pa] = 6;
    maxCoord[(size_t)ChemicalElement::U] = 6;
    maxCoord[(size_t)ChemicalElement::Np] = 6;
    maxCoord[(size_t)ChemicalElement::Pu] = 6;
    maxCoord[(size_t)ChemicalElement::Am] = 6;
    maxCoord[(size_t)ChemicalElement::Cm] = 6;
    maxCoord[(size_t)ChemicalElement::Bk] = 6;
    maxCoord[(size_t)ChemicalElement::Cf] = 6;
    maxCoord[(size_t)ChemicalElement::Es] = 6;
    maxCoord[(size_t)ChemicalElement::Fm] = 6;
    maxCoord[(size_t)ChemicalElement::Md] = 6;
    maxCoord[(size_t)ChemicalElement::No] = 6;
    maxCoord[(size_t)ChemicalElement::Lr] = 6;
    return maxCoord;
}

inline constexpr auto _maximumCoordination = getMaxiumCoordination();
};  // namespace

namespace CovalentRadii {
constexpr std::optional<FloatType> get(ChemicalElement element)
{
    OVITO_ASSERT((int)element >= 0 && (int)element < (int)ChemicalElement::NUMBER_OF_PREDEFINED_CHEMICAL_TYPES);
    return (element == ChemicalElement::X || std::isnan(_covalentRadii[(size_t)element]))
               ? std::nullopt
               : std::optional<FloatType>{_covalentRadii[(size_t)element]};
}
constexpr std::optional<FloatType> getCleaveDistance(ChemicalElement elementA, ChemicalElement elementB)
{
    const std::optional<FloatType> radiusA = get(elementA);
    const std::optional<FloatType> radiusB = get(elementB);
    if(!radiusA || !radiusB) {
        return std::nullopt;
    }
    if((elementA == ChemicalElement::Al && elementB == ChemicalElement::Al) ||
       (elementA == ChemicalElement::B && elementB == ChemicalElement::B)) {
        return *radiusA + *radiusB - 0.2;
    }
    return *radiusA + *radiusB;
}
};  // namespace CovalentRadii

namespace MaximumCoordination {
consteval uint_fast8_t max() noexcept { return *std::ranges::max_element(_maximumCoordination); }

constexpr std::optional<uint_fast8_t> get(
    ChemicalElement element,
    std::optional<std::pair<ChemicalElement, ChemicalElement>> neighs = std::nullopt)
{
    if(neighs && element == ChemicalElement::H) {
        if(neighs->first == ChemicalElement::B && neighs->second == ChemicalElement::B) {
            return 2;
        }
    }
    OVITO_ASSERT((int)element >= 0 && (int)element < (int)ChemicalElement::NUMBER_OF_PREDEFINED_CHEMICAL_TYPES);
    const uint_fast8_t maxCoordination = _maximumCoordination[(size_t)element];
    return maxCoordination == 0 ? std::nullopt : std::optional<uint_fast8_t>{maxCoordination};
}
};  // namespace MaximumCoordination

namespace MaximumValence {
// Adapted from supplementary material of doi.org/10.1002/jcc.24309
// https://onlinelibrary.wiley.com/action/downloadSupplement?doi=10.1002%2Fjcc.24309&file=jcc24309-sup-0001-suppinfo.pdf
// Automatic Molecular Structure Perception for the Universal Force Field
// Svetlana Artemova, Leonard Jaillet, and Stephane Redon
// Journal of Computational Chemistry 2016, 37, 1191–1205
constexpr std::optional<uint_fast8_t> get(ChemicalElement element, uint_fast8_t coordination)
{
    if(element == ChemicalElement::H) {
        if(coordination == 2)
            return 2;
        else
            return 1;
    }
    if(element == ChemicalElement::P) {
        if(coordination < 3) {
            return 4;
        }
        else {
            return 5;
        }
    }
    if(element == ChemicalElement::S) {
        if(coordination == 2) {
            return 4;
        }
        else {
            return 6;
        }
    }
    if(element == ChemicalElement::C) {
        return 4;
    }
    if(element == ChemicalElement::N) {
        return 4;
    }
    if(element == ChemicalElement::O) {
        return 3;
    }
    if(element == ChemicalElement::Si) {
        return 4;
    }
    return MaximumCoordination::get(element);
}
};  // namespace MaximumValence

};  // namespace Ovito