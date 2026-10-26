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
#include <ovito/particles/objects/Particles.h>
#include <ovito/mesh/surface/SurfaceMeshBuilder.h>
#include <ovito/mesh/surface/SurfaceMeshVis.h>
#include <ovito/stdobj/simcell/SimulationCell.h>
#include <ovito/stdobj/properties/Property.h>
#include <ovito/core/dataset/pipeline/Modifier.h>

namespace Ovito {

/*
 * Constructs a surface mesh enclosing the particle model.
 */
class OVITO_PARTICLES_EXPORT ConstructSurfaceModifier : public Modifier
{
    /// Give this modifier class its own metaclass.
    class OOMetaClass : public Modifier::OOMetaClass
    {
    public:

        /// Inherit constructor from base metaclass.
        using Modifier::OOMetaClass::OOMetaClass;

        /// Asks the metaclass whether the modifier can be applied to the given input data.
        virtual bool isApplicableTo(const DataCollection& input) const override;
    };

    OVITO_CLASS_META(ConstructSurfaceModifier, OOMetaClass)

public:

    /// The different methods supported by this modifier for constructing the surface.
    enum SurfaceMethod {
        AlphaShape,
        GaussianDensity,
    };
    Q_ENUM(SurfaceMethod);

public:

    /// Constructor.
    void initializeObject(ObjectInitializationFlags flags);

    /// Is called by the pipeline system before a new modifier evaluation begins.
    virtual void preevaluateModifier(const ModifierEvaluationRequest& request, PipelineEvaluationResult::EvaluationTypes& evaluationTypes, TimeInterval& validityInterval) const override;

    /// Modifies the input data.
    virtual Future<PipelineFlowState> evaluateModifier(const ModifierEvaluationRequest& request, PipelineFlowState&& state) override;

    /// Indicates that a preliminary viewport update will be performed immediately after this modifier
	/// has computed new results.
    virtual bool shouldRefreshViewportsAfterEvaluation() override { return true; }

    /// Asks the modifier to replace a visual element owned by this modifier with a new compatible object, which is created
    /// on demand by the provided callback function. The callback function accepts an optional human-readable title, which will
    /// be used for the new visual element.
    virtual void replaceVisualElement(DataVis* visElement, const std::function<OORef<DataVis>(const QString&)>& getReplacement) override;

private:

    /// Abstract base class for computation engines that build the surface mesh.
    class ConstructSurfaceEngineBase
    {
    public:

        /// Constructor.
        ConstructSurfaceEngineBase(ConstPropertyPtr positions, ConstPropertyPtr selection,
                                   SurfaceMesh* mesh, bool identifyRegions,
                                   bool computeSurfaceDistance, std::vector<ConstPropertyPtr> particleProperties,
                                   OOWeakRef<const PipelineNode> createdByNode)
            : _positions(positions),
              _selection(std::move(selection)),
              _mesh(std::move(mesh)),
              _particleProperties(std::move(particleProperties)),
              _identifyRegions(identifyRegions),
              _surfaceDistances(computeSurfaceDistance ? Particles::OOClass().createUserProperty(DataBuffer::Uninitialized, positions->size(), Property::FloatDefault, 1, QStringLiteral("Surface Distance")) : nullptr),
              _createdByNode(std::move(createdByNode))
        {
        }

        /// Destructor.
        virtual ~ConstructSurfaceEngineBase() = default;

        /// Computes the modifier's results and stores them in this object for later retrieval.
        virtual void perform() = 0;

        /// Injects the computed results into the data pipeline.
        virtual void applyResults(PipelineFlowState& state);

        /// Returns the computed total surface area.
        FloatType surfaceArea() const { return (FloatType)_totalSurfaceArea; }

        /// Adds a summation contribution to the total surface area.
        void addSurfaceArea(FloatType a) { _totalSurfaceArea += a; }

        /// Returns the generated surface mesh.
        SurfaceMesh* mesh() const { return _mesh; }

        // Returns the identify regions value
        bool identifyRegions() const { return _identifyRegions; }

        /// Returns the input particle positions.
        const ConstPropertyPtr& positions() const { return _positions; }

        /// Returns the input particle selection.
        const ConstPropertyPtr& selection() const { return _selection; }

        /// Returns the list of particle properties to copy over to the generated mesh.
        const std::vector<ConstPropertyPtr>& particleProperties() const { return _particleProperties; }

        /// Returns the output surface distance property.
        const PropertyPtr& surfaceDistances() const { return _surfaceDistances; }

    protected:

        /// Compute the distance of each input particle from the constructed surface.
        void computeSurfaceDistances(const SurfaceMeshBuilder& mesh, TaskProgress& progress);

        /// Controls the identification of disconnected spatial regions (filled and empty).
        const bool _identifyRegions;

        /// Struct that holds the computeAggregateVolumes output values
        SurfaceMeshBuilder::AggregateVolumes _aggregateVolumes = {};

        /// Volume of the filled regions that lies outside the simulation cell along its non-periodic
        /// directions (zero for a fully periodic cell).
        double _filledVolumeOutsideCell = 0;

        /// The computed total surface area.
        double _totalSurfaceArea = 0;

        /// The pipeline node that created this engine.
        OOWeakRef<const PipelineNode> _createdByNode;

    private:

        /// The input particle coordinates.
        ConstPropertyPtr _positions;

        /// The input particle selection flags.
        ConstPropertyPtr _selection;

        /// The generated surface mesh.
        SurfaceMesh* _mesh;

        /// The computed distance of each particle from the constructed surface.
        PropertyPtr _surfaceDistances;

        /// The list of particle properties to copy over to the generated mesh.
        std::vector<ConstPropertyPtr> _particleProperties;
    };

    /// Compute engine building the surface mesh using the alpha shape method.
    class AlphaShapeEngine : public ConstructSurfaceEngineBase
    {
    public:

        /// Constructor.
        AlphaShapeEngine(ConstPropertyPtr positions, ConstPropertyPtr selection,
                         ConstPropertyPtr particleGrains, SurfaceMesh* mesh, FloatType probeSphereRadius,
                         int smoothingLevel, bool selectSurfaceParticles, bool identifyRegions, bool mapParticlesToRegions,
                         bool computeSurfaceDistance, std::vector<ConstPropertyPtr> particleProperties,
                         OOWeakRef<const PipelineNode> createdByNode)
            : ConstructSurfaceEngineBase(std::move(positions), std::move(selection), std::move(mesh), identifyRegions,
                                         computeSurfaceDistance, std::move(particleProperties), std::move(createdByNode)),
              _particleGrains(std::move(particleGrains)),
              _probeSphereRadius(probeSphereRadius),
              _smoothingLevel(smoothingLevel),
              _mapParticlesToRegions(mapParticlesToRegions),
              _surfaceParticleSelection(
                  selectSurfaceParticles
                      ? Particles::OOClass().createStandardProperty(DataBuffer::Initialized,
                            this->positions()->size(), Particles::SelectionProperty)
                      : nullptr)
        {
        }

        /// Computes the modifier's results and stores them in this object for later retrieval.
        virtual void perform() override;

        /// Injects the computed results into the data pipeline.
        virtual void applyResults(PipelineFlowState& state) override;

        /// Returns the input particle grain IDs.
        const ConstPropertyPtr& particleGrains() const { return _particleGrains; }

        /// Returns the selection set containing the particles at the constructed surfaces.
        const PropertyPtr& surfaceParticleSelection() const { return _surfaceParticleSelection; }

        /// Returns the assignment of input particles to volumetric regions.
        const PropertyPtr& particleRegionIds() const { return _particleRegionIds; }

        /// Returns the value of the probe sphere radius parameter.
        FloatType probeSphereRadius() const { return _probeSphereRadius; }

    private:

        /// The radius of the virtual probe sphere (alpha-shape parameter).
        const FloatType _probeSphereRadius;

        /// The number of iterations of the smoothing algorithm to apply to the surface mesh.
        const int _smoothingLevel;

        /// The input particle grain property.
        ConstPropertyPtr _particleGrains;

        /// The selection set of particles located right on the constructed surfaces.
        PropertyPtr _surfaceParticleSelection;

        /// The assignment of input particles to a volumetric region.
        PropertyPtr _particleRegionIds;

        /// The full assignment of particles to regions, which takes into account that particles can be part of multiple regions simultaneously.
        QVariant _regionParticleLists;

        /// Indicates that the "Regions" particle property should be created.
        bool _mapParticlesToRegions;
    };

    /// Compute engine building the surface mesh using the Gaussian density method.
    class GaussianDensityEngine : public ConstructSurfaceEngineBase
    {
    public:

        /// Constructor.
        GaussianDensityEngine(ConstPropertyPtr positions, ConstPropertyPtr selection,
                              SurfaceMesh* mesh, FloatType radiusFactor, FloatType isoLevel, int gridResolution,
                              bool identifyRegions, bool computeSurfaceDistance,
                              ConstPropertyPtr radii, std::vector<ConstPropertyPtr> particleProperties,
                              OOWeakRef<const PipelineNode> createdByNode)
            : ConstructSurfaceEngineBase(std::move(positions), std::move(selection), std::move(mesh), identifyRegions,
                                         computeSurfaceDistance, std::move(particleProperties), std::move(createdByNode)),
              _radiusFactor(radiusFactor),
              _isoLevel(isoLevel),
              _gridResolution(gridResolution),
              _particleRadii(std::move(radii))
        {
        }

        /// Computes the modifier's results and stores them in this object for later retrieval.
        virtual void perform() override;

    private:

        /// Scaling factor applied to atomic radii.
        const FloatType _radiusFactor;

        /// The threshold for constructing the isosurface of the density field.
        const FloatType _isoLevel;

        /// The number of voxels in the density grid.
        const int _gridResolution;

        /// The atomic input radii.
        ConstPropertyPtr _particleRadii;
    };

    /// The vis element for rendering the surface.
    DECLARE_MODIFIABLE_REFERENCE_FIELD_FLAGS(OORef<SurfaceMeshVis>, surfaceMeshVis, setSurfaceMeshVis, PROPERTY_FIELD_DONT_PROPAGATE_MESSAGES | PROPERTY_FIELD_MEMORIZE | PROPERTY_FIELD_OPEN_SUBEDITOR);

    /// Surface construction method to use.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(SurfaceMethod{AlphaShape}, method, setMethod, PROPERTY_FIELD_MEMORIZE);

    /// Controls the radius of the probe sphere (alpha-shape method).
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(FloatType{4}, probeSphereRadius, setProbeSphereRadius, PROPERTY_FIELD_MEMORIZE);

    /// Controls the amount of smoothing (alpha-shape method).
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(int{8}, smoothingLevel, setSmoothingLevel, PROPERTY_FIELD_MEMORIZE);

    /// Controls whether only selected particles should be taken into account.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(bool{false}, onlySelectedParticles, setOnlySelectedParticles);

    /// Controls whether the modifier should select surface particles (alpha-shape method).
    DECLARE_MODIFIABLE_PROPERTY_FIELD(bool{false}, selectSurfaceParticles, setSelectSurfaceParticles);

    /// Controls whether the algorithm should identify disconnected spatial regions.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(bool{false}, identifyRegions, setIdentifyRegions);

    /// Controls whether property values should be copied over from the input particles to the generated surface vertices (alpha-shape method / density field method).
    DECLARE_MODIFIABLE_PROPERTY_FIELD(bool{false}, transferParticleProperties, setTransferParticleProperties);

    /// Controls the number of grid cells along the largest cell dimension (density field method).
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(int{50}, gridResolution, setGridResolution, PROPERTY_FIELD_MEMORIZE);

    /// The scaling factor applied to atomic radii (density field method).
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(FloatType{1}, radiusFactor, setRadiusFactor, PROPERTY_FIELD_MEMORIZE);

    /// The threshold value for constructing the isosurface of the density field (density field method).
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(FloatType{0.6}, isoValue, setIsoValue, PROPERTY_FIELD_MEMORIZE);

    /// Controls whether the algorithm should compute the shortest distance of each particle from the constructed surface.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(bool{false}, computeSurfaceDistance, setComputeSurfaceDistance);

    /// Controls whether the algorithm assigns each particle to one of the identified spatial regions.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(bool{false}, mapParticlesToRegions, setMapParticlesToRegions);
};

}   // End of namespace
