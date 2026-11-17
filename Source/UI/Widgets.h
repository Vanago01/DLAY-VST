#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Theme.h"

namespace dlay::ui
{

using APVTS = juce::AudioProcessorValueTreeState;

/** Shows a popup menu inside the plugin window, styled by the DLAY look and feel. */
void showPopupMenu (juce::PopupMenu& menu, juce::Component& target, std::function<void (int)> callback);

// ============================================================================
/** Rotary control with a legend that turns into the value read-out while you touch it.
    Shift-drag for fine adjustment, double-click the knob to reset, double-click the legend
    to type a value. */
class Knob : public juce::Component
{
public:
    enum class Size { large, medium, compact };

    Knob (APVTS& state, const juce::String& paramId, const juce::String& legend,
          Size size = Size::medium, juce::Colour accent = col::amber);
    ~Knob() override;

    void setLegend (const juce::String& text);
    void setTooltip (const juce::String& text);
    juce::Slider& slider() noexcept;
    const juce::String& getParamId() const      { return paramId; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override   { repaint(); }
    void mouseExit (const juce::MouseEvent&) override    { repaint(); }

    /** Height reserved for the legend below the dial. */
    int legendHeight() const noexcept;

private:
    class Dial;
    void beginTextEntry();
    void endTextEntry (bool commit);

    juce::String paramId, legend;
    Size size;
    std::unique_ptr<Dial> dial;
    std::unique_ptr<APVTS::SliderAttachment> attachment;
    std::unique_ptr<juce::TextEditor> entry;
};

// ============================================================================
/** Bat-handle toggle switch with LED and legend. */
class LeverSwitch : public juce::Button
{
public:
    LeverSwitch (APVTS& state, const juce::String& paramId, const juce::String& legend, juce::Colour led = col::amber);

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    juce::String legend;
    juce::Colour ledColour;
    APVTS::ButtonAttachment attachment;
};

// ============================================================================
/** Tape-transport style key with an LED slot. Optionally bound to a bool parameter. */
class KeyButton : public juce::Button
{
public:
    explicit KeyButton (const juce::String& buttonName, juce::Colour led = col::amber);

    void attach (APVTS& state, const juce::String& paramId);
    void setLedColour (juce::Colour c)          { ledColour = c; repaint(); }
    void setLit (float amount)                  { if (std::abs (amount - lit) > 0.01f) { lit = amount; repaint(); } }
    void setShowLed (bool b)                    { showLed = b; }
    void setFontHeight (float h)                { fontHeight = h; }

    /** Optional glyph drawn instead of the text (arrows, star, gear...). */
    std::function<void (juce::Graphics&, juce::Rectangle<float>, juce::Colour)> drawIcon;

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    juce::Colour ledColour;
    float lit = -1.0f, fontHeight = 11.5f;      // lit < 0: follow the toggle state
    bool showLed = true;
    std::unique_ptr<APVTS::ButtonAttachment> attachment;
};

// ============================================================================
/** Slide switch for a choice parameter with a few positions. */
class SegmentSwitch : public juce::Component,
                      public juce::SettableTooltipClient
{
public:
    SegmentSwitch (APVTS& state, const juce::String& paramId, juce::StringArray labels);

    /** Optional custom drawing of a position (used for the LFO shape icons). */
    std::function<void (juce::Graphics&, juce::Rectangle<float>, int index, juce::Colour)> drawIcon;
    std::function<void (int)> onChange;

    int getIndex() const noexcept               { return current; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent& e) override     { mouseDown (e); }

private:
    juce::StringArray labels;
    int current = 0;
    juce::ParameterAttachment attachment;
};

// ============================================================================
/** Digital read-out for a choice parameter: arrows to step, click for the full menu,
    mouse wheel to scroll through. */
class ChoiceDisplay : public juce::Component,
                      public juce::SettableTooltipClient
{
public:
    ChoiceDisplay (APVTS& state, const juce::String& paramId);

    std::function<void (juce::PopupMenu&, int currentIndex)> buildMenu;   // optional; item ids are index + 1
    std::function<void (int)> onChange;
    std::function<void (int newIndex)> onUserChange;                       // only for changes made on this control

    void setColour (juce::Colour c)             { colour = c; repaint(); }
    void setFontHeight (float h)                { fontHeight = h; repaint(); }
    void setCaption (const juce::String& c)     { caption = c; repaint(); }
    int getIndex() const noexcept               { return current; }
    int getNumChoices() const noexcept          { return choices.size(); }
    void setIndex (int newIndex);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter (const juce::MouseEvent&) override  { repaint(); }
    void mouseExit (const juce::MouseEvent&) override   { repaint(); }

private:
    juce::StringArray choices;
    juce::String caption;
    juce::Colour colour = col::amber;
    float fontHeight = 15.0f;
    int current = 0;
    double wheelAccumulator = 0.0;
    juce::ParameterAttachment attachment;
};

// ============================================================================
/** Analog VU meter with a cream face and a damped needle. */
class VuMeter : public juce::Component
{
public:
    explicit VuMeter (const juce::String& channelName) : name (channelName)      { setOpaque (true); }

    /** Feed with the latest peak (linear). Call at the UI frame rate. */
    void push (float peak, float dtSeconds);
    void paint (juce::Graphics&) override;

private:
    juce::String name;
    float needle = 0.0f, shown = -1.0f, clipHold = 0.0f;
};

// ============================================================================
/** Small scope showing the feedback filter's response as it is right now (including MOTION). */
class FilterScope : public juce::Component
{
public:
    void set (float lowCutHz, float highCutHz, float resonance);
    void paint (juce::Graphics&) override;

private:
    float hp = 20.0f, lp = 20000.0f, res = 0.0f;
};

} // namespace dlay::ui
