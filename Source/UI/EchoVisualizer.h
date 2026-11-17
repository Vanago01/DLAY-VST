#pragma once

#include "Widgets.h"
#include "../PluginProcessor.h"

namespace dlay::ui
{

/** The centrepiece: a live picture of the delay's tape loop.

    Left: the tape itself. Sound is written at the REC head on the left and travels to the
    right; where it passes a PLAY head you hear it, and the feedback rail on top carries it
    back to the start. Extra heads show the taps, TIME COLLAPSE bounces, GHOST voices,
    reverse heads, grains and slices exactly where the engine is reading. This is the
    engine's real buffer (a decimated envelope of it), not an animation.

    Right: the ECHO ORBIT radar - where the echoes currently sit in the stereo field, the
    path they travel, and a fading trail of the last repeats.

    Drag the tape sideways to change the delay time and up/down to change the feedback.
    Drag inside the radar to set the orbit size; double-click it to step through the shapes.
*/
class EchoVisualizer : public juce::Component,
                       public juce::SettableTooltipClient
{
public:
    explicit EchoVisualizer (DlayAudioProcessor&);

    /** Advance the animation; called by the editor at the UI frame rate. */
    void tick (float dtSeconds);

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    struct HeadSnap { float delayMs = 0.0f, pan = 0.0f, gain = 0.0f; int kind = 0; };
    struct Blip     { float x = 0.0f, y = 0.0f, life = 0.0f, size = 1.0f; };
    enum class Drag { none, tape, orbit };

    void paintBackground (juce::Graphics&);
    void paintOverlay (juce::Graphics&);
    void ensureLayers (float scale);
    void paintTape (juce::Graphics&);
    void paintRadar (juce::Graphics&);
    void paintStatus (juce::Graphics&);
    float envelopeAt (const std::array<std::atomic<float>, (size_t) VisualData::kRingSize>& ring, float delayMs, float spanMs) const;
    float xForDelay (float delayMs) const noexcept;
    juce::Point<float> orbitPoint (int shape, float phase, float amount) const;
    void setParam (const juce::String& paramId, float realValue);
    void gesture (const juce::String& paramId, bool begin);

    DlayAudioProcessor& proc;
    VisualData& vis;

    // raw parameter values (looked up once; reading them is a plain atomic load)
    std::atomic<float>* pOrbit = nullptr, *pOrbitShape = nullptr, *pOrbitSync = nullptr, *pOrbitDiv = nullptr,
                      * pOrbitRate = nullptr, *pInfinite = nullptr, *pSync = nullptr, *pTime = nullptr,
                      * pDivision = nullptr, *pFeedback = nullptr;

    // cached static layers
    juce::Image backgroundLayer, overlayLayer;
    float layerScale = 0.0f;

    // geometry (design units)
    juce::Rectangle<float> screen, statusBar, field, radarBox;
    float tapeX0 = 0.0f, tapeX1 = 1.0f;

    // animation state
    float viewMs = 800.0f, flow = 0.0f, glowL = 0.0f, glowR = 0.0f, recGlow = 0.0f;
    float wetSmooth = 0.0f, time = 0.0f;
    float chainL = 0.0f, chainR = 0.0f;         // time the sound spends in the processing chain before it reaches the tape
    std::array<HeadSnap, (size_t) VisualData::kMaxHeads> heads;
    int numHeads = 0;
    uint32_t ringWrite = 0, ringValid = 0;
    float hopMs = 1.333f;

    std::array<juce::Point<float>, 40> trail {};
    int trailPos = 0;
    std::array<Blip, 24> blips {};
    int blipPos = 0;
    float blipCooldown = 0.0f;

    // interaction
    Drag drag = Drag::none;
    float dragStartTime = 0.0f, dragStartFeedback = 0.0f;
    int dragStartDivision = 0;
    bool dragSynced = false, hoverTape = false, hoverRadar = false;
};

} // namespace dlay::ui
