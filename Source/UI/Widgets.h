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
    Shift-drag for fine adjustment, double-click the dial to reset, double-click the legend
    to type a value. */
class Knob : public juce::Component
{
public:
    enum class Size { large, medium, compact };
    enum class Body { light, dark };

    Knob (APVTS& state, const juce::String& paramId, const juce::String& legend,
          Size size = Size::medium, Body body = Body::light);
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

    /** Legend height and type size for a given knob size: other controls that sit in a row
        of knobs use the same, so their names line up. */
    static int legendHeightFor (Size) noexcept;
    static juce::Font legendFontFor (Size);

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
/** On/off switch with its name underneath, the size of a small knob. */
class ToggleSwitch : public juce::Button
{
public:
    ToggleSwitch (APVTS& state, const juce::String& paramId, const juce::String& legend);

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    juce::String legend;
    APVTS::ButtonAttachment attachment;
};

// ============================================================================
/** Pill-shaped key. Optionally bound to a bool parameter; can carry a status light or an
    icon instead of text. */
class KeyButton : public juce::Button
{
public:
    enum class Look   { pill, round };                  // text key, or a circular icon key
    enum class OnFill { dark, accent, ice, none };      // how the key shows that it is on

    explicit KeyButton (const juce::String& buttonName = {});

    void attach (APVTS& state, const juce::String& paramId);
    void setLook (Look l)                       { look = l; repaint(); }
    void setOnFill (OnFill f)                   { onFill = f; repaint(); }
    void setDot (bool shouldShow)               { showDot = shouldShow; repaint(); }
    void setLit (float amount)                  { if (std::abs (amount - lit) > 0.01f) { lit = amount; repaint(); } }
    void setFontSize (float px)                 { fontSize = px; repaint(); }

    /** Optional glyph drawn instead of the text (arrows, heart, power...). */
    std::function<void (juce::Graphics&, juce::Rectangle<float>, juce::Colour)> drawIcon;

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    Look look = Look::pill;
    OnFill onFill = OnFill::dark;
    float lit = 0.0f, fontSize = 12.5f;
    bool showDot = false;
    std::unique_ptr<APVTS::ButtonAttachment> attachment;
};

// ============================================================================
/** A row of positions with one selected: the tabs of the lower panel. */
class SegmentBar : public juce::Component,
                   public juce::SettableTooltipClient
{
public:
    explicit SegmentBar (juce::StringArray labels);

    /** Optional custom drawing of a position (used for the LFO shape icons). */
    std::function<void (juce::Graphics&, juce::Rectangle<float>, int index, juce::Colour)> drawIcon;
    std::function<void (int)> onChange;

    int getIndex() const noexcept               { return current; }
    void setIndex (int newIndex, bool notify);
    void setFontSize (float px)                 { fontSize = px; repaint(); }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent& e) override     { mouseDown (e); }

protected:
    /** A position was clicked. The default selects it. */
    virtual void choose (int index)             { setIndex (index, true); }

    juce::StringArray labels;
    int current = 0;
    float fontSize = 12.5f;
};

/** The same, bound to a choice parameter. */
class SegmentSwitch : public SegmentBar
{
public:
    SegmentSwitch (APVTS& state, const juce::String& paramId, juce::StringArray labels);

private:
    void choose (int index) override;

    juce::ParameterAttachment attachment;
};

// ============================================================================
/** Value field for a choice parameter: arrows to step, click for the full menu, mouse
    wheel to scroll through. With a caption it takes the shape of a knob: the field where
    the dial would be and the caption where the legend would be. */
class ChoiceDisplay : public juce::Component,
                      public juce::SettableTooltipClient
{
public:
    ChoiceDisplay (APVTS& state, const juce::String& paramId);

    std::function<void (juce::PopupMenu&, int currentIndex)> buildMenu;   // optional; item ids are index + 1
    std::function<void (int)> onChange;
    std::function<void (int newIndex)> onUserChange;                       // only for changes made on this control

    void setFontSize (float px)                 { fontSize = px; repaint(); }
    void setCaption (const juce::String& c, Knob::Size rowSize = Knob::Size::medium);
    int getIndex() const noexcept               { return current; }
    int getNumChoices() const noexcept          { return choices.size(); }
    const juce::String& getChoice (int index) const     { return choices.getReference (juce::jlimit (0, juce::jmax (0, choices.size() - 1), index)); }
    void setIndex (int newIndex);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter (const juce::MouseEvent&) override  { repaint(); }
    void mouseExit (const juce::MouseEvent&) override   { repaint(); }

protected:
    void showMenu();
    juce::Rectangle<float> fieldBounds() const;

    juce::StringArray choices;
    juce::String caption;
    Knob::Size captionSize = Knob::Size::medium;
    float fontSize = 13.5f;
    int current = 0;
    double wheelAccumulator = 0.0;
    juce::ParameterAttachment attachment;
};

// ============================================================================
/** Peak level of one channel as a thin bar that fills from the left. */
class LevelMeter : public juce::Component
{
public:
    explicit LevelMeter (const juce::String& channelName) : name (channelName) {}

    /** Feed with the latest peak (linear). Call at the UI frame rate. */
    void push (float peak, float dtSeconds);
    void paint (juce::Graphics&) override;

private:
    juce::String name;
    float level = 0.0f, hold = 0.0f, holdTime = 0.0f, clip = 0.0f;
    float shown = -1.0f, shownHold = -1.0f, shownClip = -1.0f;
};

// ============================================================================
/** Small screen showing the feedback filter's response as it is right now (including MOTION). */
class FilterScope : public juce::Component
{
public:
    FilterScope()       { setOpaque (true); }

    void set (float lowCutHz, float highCutHz, float resonance);
    void paint (juce::Graphics&) override;

private:
    float hp = 20.0f, lp = 20000.0f, res = 0.0f;
};

} // namespace dlay::ui
