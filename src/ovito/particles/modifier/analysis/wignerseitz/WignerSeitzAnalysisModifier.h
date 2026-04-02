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
#include <ovito/stdobj/properties/Property.h>
#include <ovito/stdobj/simcell/SimulationCell.h>
#include <ovito/particles/modifier/analysis/ReferenceConfigurationModifier.h>

namespace Ovito {

/**
 * \brief Performs the Wigner-Seitz cell analysis to identify point defects in crystals.
 */
class OVITO_PARTICLES_EXPORT WignerSeitzAnalysisModifier : public ReferenceConfigurationModifier
{
    OVITO_CLASS(WignerSeitzAnalysisModifier)

protected:

    /// Creates a computation engine that will compute the modifier's results.
    virtual std::unique_ptr<Engine> createEngine(const ModifierEvaluationRequest& request, const PipelineFlowState& input, const PipelineFlowState& referenceState) override;

    /// Adopts existing computation results for an interactive pipeline evaluation.
    virtual Future<PipelineFlowState> reuseCachedState(const ModifierEvaluationRequest& request, Particles* particles, PipelineFlowState&& output, const PipelineFlowState& cachedState) override;

private:

    /// Computes the modifier's results.
    class WignerSeitzAnalysisEngine : public Engine
    {
    public:

        /// Constructor.
        WignerSeitzAnalysisEngine(ConstPropertyPtr positions, const SimulationCell* simCell,
                PipelineFlowState referenceState, ConstPropertyPtr refPositions, const SimulationCell* simCellRef, AffineMappingType affineMapping,
                ConstPropertyPtr typeProperty, int ptypeMinId, int ptypeMaxId, ConstPropertyPtr referenceTypeProperty, ConstPropertyPtr referenceIdentifierProperty,
                bool adaptiveCell, FloatType strainSensitivity,
                OOWeakRef<const PipelineNode> createdByNode) :
            Engine(std::move(positions), simCell, std::move(refPositions), simCellRef,
                nullptr, nullptr, affineMapping, false),
            _typeProperty(std::move(typeProperty)),
            _ptypeMinId(ptypeMinId), _ptypeMaxId(ptypeMaxId),
            _referenceTypeProperty(std::move(referenceTypeProperty)),
            _referenceIdentifierProperty(std::move(referenceIdentifierProperty)),
            _referenceState(std::move(referenceState)),
            _adaptiveCell(adaptiveCell),
            _strainSensitivity(strainSensitivity),
            _createdByNode(std::move(createdByNode)) {}

        /// Performs the actual computation of the modifier's results.
        virtual void perform(PipelineFlowState& state) override;

        /// Returns the number of vacant sites found during the last analysis run.
        size_t vacancyCount() const { return _vacancyCount; }

        /// Increments the number of vacant sites found during the last analysis run.
        void incrementVacancyCount(size_t n = 1) { _vacancyCount += n; }

        /// Returns the number of interstitial atoms found during the last analysis run.
        size_t interstitialCount() const { return _interstitialCount; }

        /// Increments the number of interstitial atoms found during the last analysis run.
        void incrementInterstitialCount(size_t n = 1) { _interstitialCount += n; }

        /// Returns the property storage that contains the computed occupancies.
        const PropertyPtr& occupancyNumbers() const { return _occupancyNumbers; }

        /// Replaces the property storage for the computed occupancies.
        void setOccupancyNumbers(PropertyPtr prop) { _occupancyNumbers = std::move(prop); }

        /// Returns the property storage that contains the type of site each atom has been assigned to.
        const PropertyPtr& siteTypes() const { return _siteTypes; }

        /// Replaces the property storage for the type of the site each atom has been assigned to.
        void setSiteTypes(PropertyPtr prop) { _siteTypes = std::move(prop); }

        /// Returns the property storage that contains the index of the site each atom has been assigned to.
        const PropertyPtr& siteIndices() const { return _siteIndices; }

        /// Replaces the property storage for the index of the site each atom has been assigned to.
        void setSiteIndices(PropertyPtr prop) { _siteIndices = std::move(prop); }

        /// Returns the property storage that contains the identifier of the site each atom has been assigned to.
        const PropertyPtr& siteIdentifiers() const { return _siteIdentifiers; }

        /// Replaces the property storage for the identifier of the site each atom has been assigned to.
        void setSiteIdentifiers(PropertyPtr prop) { _siteIdentifiers = std::move(prop); }

        /// Returns the reference state.
        const PipelineFlowState& referenceState() const { return _referenceState; }

        /// Returns the property storage that contains the particle types.
        const ConstPropertyPtr& particleTypes() const { return _typeProperty; }

    private:

        ConstPropertyPtr _typeProperty;
        ConstPropertyPtr _referenceTypeProperty;
        ConstPropertyPtr _referenceIdentifierProperty;
        int _ptypeMinId;
        int _ptypeMaxId;
        const PipelineFlowState _referenceState;
        bool _adaptiveCell;
        FloatType _strainSensitivity;
        PropertyPtr _occupancyNumbers;
        PropertyPtr _siteTypes;
        PropertyPtr _siteIndices;
        PropertyPtr _siteIdentifiers;
        size_t _vacancyCount = 0;
        size_t _interstitialCount = 0;
        OOWeakRef<const PipelineNode> _createdByNode;
    };

    /// Enables per-type occupancy numbers.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(bool{false}, perTypeOccupancy, setPerTypeOccupancy, PROPERTY_FIELD_MEMORIZE)

    /// Enables output of displaced atomic configuration instead of reference configuration.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(bool{false}, outputCurrentConfig, setOutputCurrentConfig, PROPERTY_FIELD_MEMORIZE)

    /// Enables the adaptive Wigner-Seitz cell algorithm based on local strain.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(bool{false}, adaptiveCell, setAdaptiveCell, PROPERTY_FIELD_MEMORIZE)

    /// Controls the sensitivity of the adaptive cell to local strain (scale factor for power diagram weights).
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(FloatType{1.0}, strainSensitivity, setStrainSensitivity, PROPERTY_FIELD_MEMORIZE)
};

}   // End of namespace
