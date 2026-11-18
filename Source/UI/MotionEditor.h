#pragma once

#include "Widgets.h"
#include "../PluginProcessor.h"

namespace dlay::ui
{

/** The MOTION page: six lanes (speed, pan, feedback, filter, pitch, mix), each an LFO or a
    hand-drawn curve with its own depth, rate / tempo length, phase and left-right offset.

    The curve view shows three things at once: the shape at full scale (and its handles
    when it is a custom curve), the modulation that actually reaches the engine after depth
    (filled), and a glowing trace of what the lane really put out on the last pass.

    It draws no background of its own: it lives on the lower card of the editor, and leaves
    the left end of its top row free for that card's tabs.
*/
class MotionEditor : public juce::Component
{
public:
    explicit MotionEditor (DlayAudioProcessor&);
    ~MotionEditor() override;

    void tick (float dtSeconds);
    void setLane (int laneIndex);
    int getLane() const noexcept            { return lane; }

    /** Width at the left of the top row that belongs to the card's tabs. */
    void setHeaderInset (int pixels)        { headerInset = pixels; resized(); }

    void resized() override;

private:
    class CurveView;
    struct LaneControls;

    DlayAudioProcessor& proc;
    int lane = 0, headerInset = 330;
    std::array<std::atomic<float>*, (size_t) kNumLanes> laneDepth {};

    std::array<std::unique_ptr<KeyButton>, (size_t) kNumLanes> tabs;
    std::unique_ptr<CurveView> curve;
    std::unique_ptr<LaneControls> controls;
    KeyButton drawButton { "DRAW" }, resetButton { "RESET" }, invertButton { "INVERT" };

    juce::Rectangle<int> curveArea, controlArea;
};

} // namespace dlay::ui
