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


#include <ovito/stdmod/StdMod.h>
#include <ovito/core/dataset/pipeline/Modifier.h>
#include <ovito/core/dataset/pipeline/ModificationNode.h>

namespace Ovito {

/**
 * \brief Computes the mean of one or more input quantities over the frames of the trajectory.
 *
 * Each entry of operateOn() selects one quantity, using one of these forms:
 *   - "attribute:<name>"                 a global attribute
 *   - "table:<identifier>"               all columns of a data table
 *   - "property:<container path>/<name>" an element-wise property, e.g. "property:particles/Position"
 *   - "cell"                             the simulation cell matrix
 *
 * Element-wise properties are matched across frames by their unique identifiers when the container
 * has them (e.g. particle IDs), otherwise by index. Averages are output as new quantities with the
 * suffix " Average" (tables: identifier suffix "-average"; cell: a "cell-average" table), or they
 * overwrite the original values if overwrite() is set (integers are then rounded).
 */
class OVITO_STDMOD_EXPORT TimeAveragingModifier : public Modifier
{
    OVITO_CLASS(TimeAveragingModifier)

public:

    /// Is called by the pipeline system before a new modifier evaluation begins.
    virtual void preevaluateModifier(const ModifierEvaluationRequest& request, PipelineEvaluationResult::EvaluationTypes& evaluationTypes, TimeInterval& validityInterval) const override;

    /// Modifies the input data.
    virtual Future<PipelineFlowState> evaluateModifier(const ModifierEvaluationRequest& request, PipelineFlowState&& state) override;

    /// Returns a short piece of information to be displayed next to the modifier's title in the pipeline editor list.
    virtual QVariant getPipelineEditorShortInfo(Scene* scene, ModificationNode* node) const override { return operateOn().join(QStringLiteral(", ")); }

private:

    /// The input quantities to be averaged (see class documentation for the syntax).
    DECLARE_MODIFIABLE_PROPERTY_FIELD(QStringList{}, operateOn, setOperateOn);

    /// Replace the original values with the averages instead of outputting new quantities.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(bool{false}, overwrite, setOverwrite);

    /// Sample only every N-th frame of the trajectory.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(int{1}, samplingFrequency, setSamplingFrequency);

    /// Restricts the averaging to a sub-range of the trajectory.
    DECLARE_MODIFIABLE_PROPERTY_FIELD(bool{false}, useCustomInterval, setUseCustomInterval);

    /// First frame of the custom averaging interval.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(int{0}, customIntervalStart, setCustomIntervalStart, PROPERTY_FIELD_MEMORIZE);

    /// Last frame of the custom averaging interval.
    DECLARE_MODIFIABLE_PROPERTY_FIELD_FLAGS(int{0}, customIntervalEnd, setCustomIntervalEnd, PROPERTY_FIELD_MEMORIZE);
};

/**
 * Accumulates the sums needed by the TimeAveragingModifier over the whole trajectory.
 */
class OVITO_STDMOD_EXPORT TimeAveragingModificationNode : public ModificationNode
{
    OVITO_CLASS(TimeAveragingModificationNode)

public:

    /// Running sums for one input quantity.
    struct Accumulator {
        enum Kind { Attribute, Table, ElementProperty, Cell };
        Kind kind = Attribute;
        QString name;                   ///< Attribute name, table identifier or property name.
        QString containerPath;          ///< Data path of the property container (ElementProperty only).
        std::vector<double> sums;       ///< One running sum per value (element x component, or 12 cell matrix entries).
        size_t sampleCount = 0;         ///< Number of frames that contributed.
        size_t componentCount = 1;
        std::unordered_map<qlonglong, size_t> idToIndex;  ///< Element identifier -> slot in sums (if identifiers exist).
        std::vector<std::pair<QString, size_t>> columns;  ///< Table columns: name and offset into sums.
        double intervalStartSum = 0, intervalEndSum = 0;  ///< Running sums of the table's x range (Table only).
        double firstIntervalStart = 0, firstIntervalEnd = 0;  ///< The table's x range in the first sampled frame (Table only).
        bool intervalChanged = false;   ///< The table's x range is not the same in all frames (Table only).
    };

    /// Indicates whether all frames of the averaging interval have been processed.
    bool averagingComplete() const { return _averagingComplete; }

    /// Returns the accumulated sums, one per entry of TimeAveragingModifier::operateOn().
    const std::vector<Accumulator>& accumulators() const { return _accumulators; }

    /// Steps through the trajectory (if not already done) and accumulates the sums.
    SharedFuture<void> averageTrajectory(const ModifierEvaluationRequest& request);

    /// Writes the averaged quantities into the given pipeline state.
    void outputAverages(const ModifierEvaluationRequest& request, PipelineFlowState& state) const;

    /// Parses one operateOn() entry.
    static Accumulator parseInputKey(const QString& key);

protected:

    /// Sends an event to all dependents of this RefTarget.
    virtual void notifyDependentsImpl(const ReferenceEvent& event) noexcept override;

    /// Rescales the times of all animation keys from the old animation interval to the new interval.
    virtual void rescaleTime(const TimeInterval& oldAnimationInterval, const TimeInterval& newAnimationInterval) override;

    /// Throws away the accumulated sums and interrupts any averaging in progress.
    void invalidateAverages();

private:

    /// The background operation stepping through the trajectory; kept to deduplicate concurrent requests.
    WeakSharedFuture<void> _averagingWeakFuture;

    /// True once all frames of the averaging interval have been processed.
    bool _averagingComplete = false;

    /// One accumulator per input quantity.
    std::vector<Accumulator> _accumulators;

    /// Incremented whenever the sums are invalidated, so that an averaging operation started earlier,
    /// which may still be running, cannot add to the new sums or mark them as complete.
    unsigned int _generation = 0;

    /// Working object that adds the values of each trajectory frame to the running sums.
    struct FrameAccumulator {
        TimeAveragingModificationNode* _node;
        unsigned int _generation;
        std::unique_ptr<TaskProgress> _progress;

        /// Adds the values of one trajectory frame.
        void operator()(int frame, const PipelineFlowState& state);
    };
};

}   // End of namespace
