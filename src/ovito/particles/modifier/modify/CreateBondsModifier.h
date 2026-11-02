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

#pragma once


#include <ovito/particles/Particles.h>
#include <ovito/particles/objects/BondsVis.h>
#include <ovito/particles/objects/BondType.h>
#include <ovito/stdobj/simcell/SimulationCell.h>
#include <ovito/core/dataset/pipeline/Modifier.h>

/// This comparison operator is required for using QVariant as key-type in a QMap as done by CreateBondsModifier.
/// The < operator for QVariant, which is part of the key-type, has been removed in Qt 6. Redefining it here is an ugly hack and should be
/// solved in a different way in the future.
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    inline bool operator<(const QPair<QVariant, QVariant>& key1, const QPair<QVariant, QVariant>& key2)
    {
        return key1.first.toString() < key2.first.toString() || (!(key2.first.toString() < key1.first.toString()) && key1.second.toString() < key2.second.toString());
    }
#else
    QT_BEGIN_NAMESPACE
    template<> inline bool qMapLessThanKey<QPair<QVariant, QVariant>>(const QPair<QVariant, QVariant>& key1, const QPair<QVariant, QVariant>& key2)
    {
        return key1.first.toString() < key2.first.toString() || (!(key2.first.toString() < key1.first.toString()) && key1.second.toString() < key2.second.toString());
    }
    QT_END_NAMESPACE
#endif

namespace Ovito {

/**
 * \brief A modifier that creates bonds between pairs of particles based on their distance.
 */
class OVITO_PARTICLES_EXPORT CreateBondsModifier : public Modifier
{
    /// Give this modifier class its own metaclass.
    class CreateBondsModifierClass : public ModifierClass
    {
    public:

        /// Inherit constructor from base class.
        using ModifierClass::ModifierClass;

        /// Asks the metaclass whether the modifier can be applied to the given input data.
        virtual bool isApplicableTo(const DataCollection& input) const override;
    };

    OVITO_CLASS_META(CreateBondsModifier, CreateBondsModifierClass)

public:

    enum CutoffMode {
        UniformCutoff,      ///< A uniform distance cutoff for all pairs of particles.
        PairCutoff,         ///< Individual cutoff for each pair-wise combination of particle types.
        TypeRadiusCutoff,   ///< Cutoff based on Van der Waals radii of the two particle types involved.
        CovalentRadiusCutoff,  ///< Cutoff based on covalent radii of the two particle types involved, accounting for maximum coordination
                               ///< numbers (Artemova et al. 2016). Ported from OVITO 3.16.
    };
    Q_ENUM(CutoffMode);

    /// The container type used to store the pair-wise cutoffs.
    using PairwiseCutoffsList = QMap<QPair<QVariant,QVariant>, FloatType>;

public:

    /// Constructor.
    void initializeObject(ObjectInitializationFlags flags);

    /// Gets called by the system when the modifier is being inserted into a data pipeline.
    virtual void initializeModifier(const ModifierInitializationRequest& request) override;

    /// Modifies the input data.
    virtual Future<PipelineFlowState> evaluateModifier(const ModifierEvaluationRequest& request, PipelineFlowState&& state) override;

    /// Indicates whether the interactive viewports should be updated after a parameter of the the modifier has
    /// been changed and before the entire pipeline is recomputed.
    virtual bool shouldRefreshViewportsAfterChange() override { return true; }

    /// Sets the cutoff radius for a pair of particle types.
    void setPairwiseCutoff(const QVariant& typeA, const QVariant& typeB, FloatType cutoff);

    /// Sets the cutoff radius for a pair of particle types.
    void setPairwiseCutoff(const std::variant<int,QString>& typeA, const std::variant<int,QString>& typeB, FloatType cutoff) {
        // Convert parameters from std::variant to QVariant:
        setPairwiseCutoff(
            std::visit([](auto&& arg) { return QVariant::fromValue(arg); }, typeA),
            std::visit([](auto&& arg) { return QVariant::fromValue(arg); }, typeB),
            cutoff);
    }

    /// Returns the pair-wise cutoff radius for a pair of particle types.
    FloatType getPairwiseCutoff(const QVariant& typeA, const QVariant& typeB) const;

    /// Returns the pair-wise cutoff radius for a pair of particle types.
    FloatType getPairwiseCutoff(const std::variant<int,QString>& typeA, const std::variant<int,QString>& typeB) const {
        // Convert parameters from std::variant to QVariant:
        return getPairwiseCutoff(
            std::visit([](auto&& arg) { return QVariant::fromValue(arg); }, typeA),
            std::visit([](auto&& arg) { return QVariant::fromValue(arg); }, typeB));
    }

    /// Asks the modifier to replace a visual element owned by this modifier with a new compatible object, which is created
    /// on demand by the provided callback function. The callback function accepts an optional human-readable title, which will
    /// be used for the new visual element.
    virtual void replaceVisualElement(DataVis* visElement, const std::function<OORef<DataVis>(const QString&)>& getReplacement) override;

protected:

    /// Is called when a RefTarget referenced by this object generated an event.
    virtual bool referenceEvent(RefTarget* source, const ReferenceEvent& event) override;

    /// Looks up a particle type in the type list based on the name or the numeric ID.
    static const ElementType* lookupParticleType(const Property* typeProperty, const QVariant& typeSpecification);

private:

    /// The mode of determing the bond cutoff.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(CutoffMode{UniformCutoff}, cutoffMode, setCutoffMode, PROPERTY_FIELD_MEMORIZE);

    /// The uniform cutoff distance for bond generation.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(FloatType{3.2}, uniformCutoff, setUniformCutoff, PROPERTY_FIELD_MEMORIZE);

    /// The minimum bond length.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(FloatType{0}, minimumCutoff, setMinimumCutoff);

    /// The prefactor to be used for computing the cutoff distance from the Van der Waals radii.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(FloatType{0.6}, vdwPrefactor, setVdwPrefactor); // Note: Value 0.6 has been adopted from VMD source code.

    /// The cutoff radii for pairs of particle types.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(PairwiseCutoffsList{}, pairwiseCutoffs, setPairwiseCutoffs);

    /// If true, all bonds already existing in the modifier's input are discarded before new bonds are created.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(bool{false}, discardExistingBonds, setDiscardExistingBonds, PROPERTY_FIELD_MEMORIZE);

    /// If true, bonds will only be created between atoms from the same molecule.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(bool{false}, onlyIntraMoleculeBonds, setOnlyIntraMoleculeBonds, PROPERTY_FIELD_MEMORIZE);

    /// If true, no bonds will be created between two particles of type "H".
    /// This option is only applied in mode TypeRadiusCutoff,
    DECLARE_MODIFIABLE_PROPERTY_FIELD(bool{true}, skipHydrogenHydrogenBonds, setSkipHydrogenHydrogenBonds);

    /// The bond type object that will be assigned to the newly created bonds.
    DECLARE_MODIFIABLE_REFERENCE_FIELD_FLAGS(OORef<BondType>, bondType, setBondType, PROPERTY_FIELD_MEMORIZE | PROPERTY_FIELD_OPEN_SUBEDITOR);

    /// The vis element for rendering the bonds.
    DECLARE_MODIFIABLE_REFERENCE_FIELD_FLAGS(OORef<BondsVis>, bondsVis, setBondsVis, PROPERTY_FIELD_DONT_PROPAGATE_MESSAGES | PROPERTY_FIELD_MEMORIZE | PROPERTY_FIELD_OPEN_SUBEDITOR);

    /// Controls whether the modifier should automatically turn off the display in case the number of bonds is unusually large.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(bool{true}, autoDisableBondDisplay, setAutoDisableBondDisplay, PROPERTY_FIELD_NO_CHANGE_MESSAGE | PROPERTY_FIELD_NO_UNDO);
};

}   // End of namespace
