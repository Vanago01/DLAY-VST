#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include "MotionEngine.h"

namespace dlay
{

/** One breakpoint of a hand-drawn MOTION curve. x and y are 0..1, curve bends the segment
    that follows the point (-1 = eases out, 0 = straight, +1 = eases in). */
struct CurvePoint
{
    float x = 0.0f, y = 0.5f, curve = 0.0f;
};

/** The six custom MOTION curves.

    The breakpoints live in a ValueTree child of the plugin state ("MOTION"), so they are
    saved with the session, stored in presets and covered by undo/redo. Whenever the tree
    changes, the affected curve is rasterised into the engine's lock-free 256-point table.
*/
class MotionCurves : private juce::ValueTree::Listener
{
public:
    MotionCurves (juce::ValueTree& stateRoot, juce::UndoManager* undo, MotionEngine& engine);
    ~MotionCurves() override;

    std::vector<CurvePoint> getPoints (int lane) const;
    void setPoints (int lane, const std::vector<CurvePoint>& points, bool undoable = true);

    /** Replaces all six curves from a stored MOTION tree (or resets them when it is invalid). */
    void restoreFrom (const juce::ValueTree& motionTree, bool undoable);

    /** A detached copy of the MOTION tree, for presets. */
    juce::ValueTree createCopy() const;

    /** The most breakpoints a lane holds: one per entry of the engine's table. The editor
        makes a few dozen at most; a file with more has them thinned out. */
    static constexpr int kMaxPoints = 256;

    /** The MOTION element of a session or a preset file, as the tree DLAY would have made
        of its own writing: the six lanes once each, every value a number inside its range,
        no more points than a lane holds, and nothing else. */
    static juce::ValueTree cleaned (const juce::XmlElement& motionXml);

    /** Called after the plugin state has been replaced wholesale. */
    void stateReplaced();

    /** Curve value in 0..1 at x in 0..1. */
    static float evaluate (const std::vector<CurvePoint>& points, float x) noexcept;

    static std::vector<CurvePoint> defaultCurve (int lane);

    static const juce::Identifier motionType, laneType, pointType, propX, propY, propCurve, propLane;

    std::function<void (int lane)> onCurveChanged;      // message thread

private:
    juce::ValueTree getMotionTree (bool createIfMissing);
    juce::ValueTree getLaneTree (int lane, bool createIfMissing);
    void rasterise (int lane);
    void rasteriseAll();
    int laneOf (const juce::ValueTree& tree) const;

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeRedirected (juce::ValueTree&) override;

    juce::ValueTree& root;
    juce::UndoManager* undoManager;
    MotionEngine& motion;
    int suspend = 0;
};

} // namespace dlay
