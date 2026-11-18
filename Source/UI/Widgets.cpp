#include "Widgets.h"

namespace dlay::ui
{

namespace
{
    constexpr float kPiF = juce::MathConstants<float>::pi;

    /** A chevron pointing left or right, drawn as a rounded stroke. */
    void drawChevron (juce::Graphics& g, juce::Point<float> centre, float size, bool pointsRight, juce::Colour colour, float thickness = 1.7f)
    {
        const float d = pointsRight ? 1.0f : -1.0f;
        juce::Path p;
        p.startNewSubPath (centre.x - d * size * 0.45f, centre.y - size);
        p.lineTo (centre.x + d * size * 0.55f, centre.y);
        p.lineTo (centre.x - d * size * 0.45f, centre.y + size);
        g.setColour (colour);
        g.strokePath (p, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

void showPopupMenu (juce::PopupMenu& menu, juce::Component& target, std::function<void (int)> callback)
{
    menu.setLookAndFeel (&target.getLookAndFeel());

    auto options = juce::PopupMenu::Options().withTargetComponent (&target);

    if (auto* editor = target.findParentComponentOfClass<juce::AudioProcessorEditor>())
        options = options.withParentComponent (editor);

    menu.showMenuAsync (options, [cb = std::move (callback)] (int result)
    {
        if (result != 0 && cb != nullptr)
            cb (result);
    });
}

// ============================================================================
//  Knob
// ============================================================================
class Knob::Dial : public juce::Slider
{
public:
    Dial (Knob& o, bool darkBody)
        : juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox), owner (o), dark (darkBody)
    {
        setRotaryParameters (kPiF * 1.25f, kPiF * 2.75f, true);
        setMouseDragSensitivity (190);
        setPopupMenuEnabled (false);
    }

    void paint (juce::Graphics& g) override
    {
        const float centre = (getMinimum() < 0.0 && getMaximum() > 0.0) ? (float) valueToProportionOfLength (0.0) : -1.0f;
        drawKnob (g, getLocalBounds().toFloat(), (float) valueToProportionOfLength (getValue()), centre,
                  dark, isMouseOverOrDragging(), isEnabled());
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        setMouseDragSensitivity (e.mods.isShiftDown() ? 1200 : 190);       // shift = fine
        juce::Slider::mouseDown (e);
        owner.repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override       { juce::Slider::mouseUp (e);    owner.repaint(); }
    void mouseEnter (const juce::MouseEvent& e) override    { juce::Slider::mouseEnter (e); owner.repaint(); }
    void mouseExit (const juce::MouseEvent& e) override     { juce::Slider::mouseExit (e);  owner.repaint(); }
    void valueChanged() override                            { owner.repaint(); }

private:
    Knob& owner;
    bool dark;
};

Knob::Knob (APVTS& state, const juce::String& id, const juce::String& legendText, Size s, Body body)
    : paramId (id), legend (legendText), size (s)
{
    setPaintingIsUnclipped (true);          // long legends may overhang a small dial

    dial = std::make_unique<Dial> (*this, body == Body::dark);
    addAndMakeVisible (*dial);
    attachment = std::make_unique<APVTS::SliderAttachment> (state, paramId, *dial);
}

Knob::~Knob() = default;

juce::Slider& Knob::slider() noexcept
{
    return *dial;
}

void Knob::setLegend (const juce::String& text)
{
    if (legend != text)
    {
        legend = text;
        repaint();
    }
}

void Knob::setTooltip (const juce::String& text)
{
    dial->setTooltip (text);
}

int Knob::legendHeightFor (Size s) noexcept
{
    return s == Size::large ? 24 : (s == Size::medium ? 22 : 20);
}

juce::Font Knob::legendFontFor (Size s)
{
    return font (FontKind::semiBold, s == Size::large ? 15.0f : (s == Size::medium ? 14.0f : 13.0f), 0.005f);
}

int Knob::legendHeight() const noexcept
{
    return legendHeightFor (size);
}

void Knob::resized()
{
    auto area = getLocalBounds();
    area.removeFromBottom (legendHeight());
    const int d = juce::jmin (area.getWidth(), area.getHeight());
    dial->setBounds (area.withSizeKeepingCentre (d, d));

    if (entry != nullptr)
        entry->setBounds (getLocalBounds().removeFromBottom (legendHeight() + 6).expanded (14, 0));
}

void Knob::paint (juce::Graphics& g)
{
    if (entry != nullptr)
        return;

    const auto textArea = getLocalBounds().removeFromBottom (legendHeight()).toFloat().expanded (30.0f, 0.0f);
    const bool touching = dial->isMouseOverOrDragging() || dial->isMouseButtonDown();

    if (touching)
    {
        g.setFont (monoFont (size == Size::large ? 13.5f : 12.5f));
        g.setColour (col::violet);
        g.drawText (dial->getTextFromValue (dial->getValue()), textArea, juce::Justification::centred, false);
    }
    else
    {
        g.setFont (legendFontFor (size));
        g.setColour (col::ink.withMultipliedAlpha (isEnabled() ? 1.0f : 0.4f));
        g.drawText (legend, textArea, juce::Justification::centred, false);
    }
}

void Knob::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.y >= getHeight() - legendHeight() - 2)
        beginTextEntry();
}

void Knob::beginTextEntry()
{
    entry = std::make_unique<juce::TextEditor>();
    entry->setFont (monoFont (12.5f));
    entry->setJustification (juce::Justification::centred);
    entry->setIndents (2, 1);
    entry->setText (dial->getTextFromValue (dial->getValue()), false);
    entry->onReturnKey = [this] { endTextEntry (true); };
    entry->onEscapeKey = [this] { endTextEntry (false); };
    entry->onFocusLost = [this] { endTextEntry (true); };

    addAndMakeVisible (*entry);
    resized();
    entry->selectAll();

    if (entry->isShowing())
        entry->grabKeyboardFocus();

    repaint();
}

void Knob::endTextEntry (bool commit)
{
    if (entry == nullptr)
        return;

    if (commit)
    {
        const auto text = entry->getText().trim();
        if (text.isNotEmpty())
            dial->setValue (dial->getValueFromText (text), juce::sendNotificationSync);
    }

    // the editor may be in the middle of one of its own callbacks: let it finish first
    std::shared_ptr<juce::TextEditor> dying (entry.release());
    dying->setVisible (false);
    dying->onFocusLost = nullptr;
    dying->onReturnKey = nullptr;
    dying->onEscapeKey = nullptr;
    juce::MessageManager::callAsync ([dying] {});
    repaint();
}

// ============================================================================
//  ToggleSwitch
// ============================================================================
ToggleSwitch::ToggleSwitch (APVTS& state, const juce::String& paramId, const juce::String& legendText)
    : juce::Button (legendText), legend (legendText), attachment (state, paramId, *this)
{
    setClickingTogglesState (true);
}

void ToggleSwitch::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    const auto b = getLocalBounds().toFloat();
    const bool on = getToggleState();
    const float legendH = (float) Knob::legendHeightFor (Knob::Size::compact);

    const auto area = b.withTrimmedBottom (legendH);
    const float tw = juce::jmin (46.0f, b.getWidth()), th = 26.0f;
    const auto track = juce::Rectangle<float> (tw, th).withCentre (area.getCentre());
    const float corner = th * 0.5f;

    if (on)
    {
        g.setColour (col::magenta.withAlpha (0.25f));
        g.fillRoundedRectangle (track.translated (0.0f, 2.5f), corner);
        g.setGradientFill (accentGradient (track));
        g.fillRoundedRectangle (track, corner);
    }
    else
    {
        g.setColour (highlighted ? col::track.darker (0.06f) : col::track);
        g.fillRoundedRectangle (track, corner);

        // pressed into the card
        juce::Graphics::ScopedSaveState state (g);
        juce::Path clip;
        clip.addRoundedRectangle (track, corner);
        g.reduceClipRegion (clip);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0x22201c38), 0.0f, track.getY(), juce::Colours::transparentBlack, 0.0f, track.getY() + 7.0f, false));
        g.fillRect (track.withHeight (8.0f));
    }

    const float r = corner - 3.0f;
    const juce::Point<float> c (on ? track.getRight() - corner : track.getX() + corner, track.getCentreY());

    g.setColour (juce::Colour (0x38201c38));
    g.fillEllipse (c.x - r, c.y - r + 1.6f, 2.0f * r, 2.0f * r);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white, c.x, c.y - r, juce::Colour (0xffededf3), c.x, c.y + r, false));
    g.fillEllipse (c.x - r, c.y - r, 2.0f * r, 2.0f * r);

    g.setFont (Knob::legendFontFor (Knob::Size::compact));
    g.setColour (col::ink);
    g.drawText (legend, b.withTop (b.getBottom() - legendH).expanded (20.0f, 0.0f), juce::Justification::centred, false);
}

// ============================================================================
//  KeyButton
// ============================================================================
KeyButton::KeyButton (const juce::String& buttonName)
    : juce::Button (buttonName)
{
}

void KeyButton::attach (APVTS& state, const juce::String& paramId)
{
    setClickingTogglesState (true);
    attachment = std::make_unique<APVTS::ButtonAttachment> (state, paramId, *this);
}

void KeyButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto b = getLocalBounds().toFloat().reduced (1.5f, 2.0f);

    if (look == Look::round)
    {
        const float d = juce::jmin (b.getWidth(), b.getHeight());
        b = b.withSizeKeepingCentre (d, d);
    }

    const float corner = b.getHeight() * 0.5f;
    const bool on = getToggleState() && onFill != OnFill::none;
    const float enabledAlpha = isEnabled() ? 1.0f : 0.38f;

    // ---- body
    if (on)
    {
        if (onFill == OnFill::dark)
        {
            g.setColour (juce::Colour (0x2a15131c));
            g.fillRoundedRectangle (b.translated (0.0f, 2.0f), corner);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2b2836), 0.0f, b.getY(), col::dark, 0.0f, b.getBottom(), false));
            g.fillRoundedRectangle (b, corner);
        }
        else
        {
            const bool ice = onFill == OnFill::ice;
            g.setColour ((ice ? col::purple : col::magenta).withAlpha (0.30f));
            g.fillRoundedRectangle (b.translated (0.0f, 3.0f), corner);

            if (ice)
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xff59b8ff), b.getX(), 0.0f, col::purple, b.getRight(), 0.0f, false));
            else
                g.setGradientFill (accentGradient (b));

            g.fillRoundedRectangle (b, corner);
        }

        g.setColour (juce::Colours::white.withAlpha (0.16f));
        g.drawRoundedRectangle (b.reduced (0.7f), corner - 0.7f, 1.0f);
    }
    else
    {
        g.setColour (juce::Colour (0x16201c38).withMultipliedAlpha (enabledAlpha));
        g.fillRoundedRectangle (b.translated (0.0f, 1.6f), corner);

        g.setColour (down ? col::cardLow : (highlighted ? juce::Colour (0xfff5f5fa) : juce::Colours::white));
        g.fillRoundedRectangle (b, corner);
        g.setColour (highlighted && isEnabled() ? col::track : col::line);
        g.drawRoundedRectangle (b.reduced (0.5f), corner - 0.5f, 1.0f);
    }

    const auto textColour = (on ? juce::Colours::white : col::ink).withMultipliedAlpha (enabledAlpha);
    auto textArea = b;

    if (showDot)
    {
        const juce::Point<float> c (b.getX() + juce::jmin (corner, 15.0f), b.getCentreY());

        if (on && lit < 0.02f)
        {
            g.setColour (juce::Colours::white.withAlpha (0.28f));
            g.fillEllipse (c.x - 3.0f, c.y - 3.0f, 6.0f, 6.0f);
        }
        else
        {
            drawLed (g, c, 3.0f, on ? juce::Colour (0xffff8cc4) : col::pink, lit);
        }

        // the name sits in what is left of the key, clear of the light
        const float inset = c.x - b.getX() + 7.0f;
        textArea = b.withTrimmedLeft (inset).withTrimmedRight (juce::jmin (inset, 9.0f));
    }

    if (drawIcon != nullptr)
    {
        drawIcon (g, textArea, textColour);
        return;
    }

    g.setFont (font (FontKind::semiBold, fontSize, 0.045f));
    g.setColour (textColour);
    g.drawText (getButtonText(), textArea, juce::Justification::centred, false);
}

// ============================================================================
//  SegmentBar / SegmentSwitch
// ============================================================================
SegmentBar::SegmentBar (juce::StringArray names)
    : labels (std::move (names))
{
}

void SegmentBar::setIndex (int newIndex, bool notify)
{
    newIndex = juce::jlimit (0, juce::jmax (0, labels.size() - 1), newIndex);

    if (newIndex != current)
    {
        current = newIndex;
        repaint();

        if (notify && onChange != nullptr)
            onChange (current);
    }
}

void SegmentBar::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced (0.5f);
    const int n = juce::jmax (1, labels.size());
    const float corner = b.getHeight() * 0.5f;
    const float pad = 3.0f;
    const float cell = (b.getWidth() - 2.0f * pad) / (float) n;

    drawField (g, b, corner);

    for (int i = 0; i < n; ++i)
    {
        const auto r = juce::Rectangle<float> (b.getX() + pad + cell * (float) i, b.getY() + pad, cell, b.getHeight() - 2.0f * pad);
        const bool selected = (i == current);

        if (selected)
        {
            const float c = r.getHeight() * 0.5f;
            g.setColour (juce::Colour (0x24201c38));
            g.fillRoundedRectangle (r.translated (0.0f, 1.5f), c);
            g.setColour (juce::Colours::white);
            g.fillRoundedRectangle (r, c);
            g.setColour (col::line);
            g.drawRoundedRectangle (r.reduced (0.5f), c - 0.5f, 0.8f);
        }

        const auto colour = selected ? col::ink : col::inkDim;

        if (drawIcon != nullptr)
        {
            drawIcon (g, r.reduced (4.0f, 4.0f), i, colour);
        }
        else
        {
            g.setFont (font (FontKind::semiBold, juce::jmin (fontSize, b.getHeight() * 0.46f), 0.045f));
            g.setColour (colour);
            g.drawText (labels[i], r, juce::Justification::centred, false);
        }
    }
}

void SegmentBar::mouseDown (const juce::MouseEvent& e)
{
    const int n = juce::jmax (1, labels.size());
    const int index = juce::jlimit (0, n - 1, (int) ((float) e.x / ((float) getWidth() / (float) n)));

    if (index != current)
        choose (index);
}

SegmentSwitch::SegmentSwitch (APVTS& state, const juce::String& paramId, juce::StringArray names)
    : SegmentBar (std::move (names)),
      attachment (*state.getParameter (paramId), [this] (float v) { setIndex (juce::roundToInt (v), true); }, state.undoManager)
{
    attachment.sendInitialUpdate();
}

void SegmentSwitch::choose (int index)
{
    attachment.setValueAsCompleteGesture ((float) index);
}

// ============================================================================
//  ChoiceDisplay
// ============================================================================
ChoiceDisplay::ChoiceDisplay (APVTS& state, const juce::String& paramId)
    : attachment (*state.getParameter (paramId),
                  [this] (float v)
                  {
                      current = juce::roundToInt (v);
                      repaint();
                      if (onChange != nullptr) onChange (current);
                  },
                  state.undoManager)
{
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramId)))
        choices = choice->choices;

    attachment.sendInitialUpdate();
}

void ChoiceDisplay::setCaption (const juce::String& c, Knob::Size rowSize)
{
    caption = c;
    captionSize = rowSize;
    repaint();
}

void ChoiceDisplay::setIndex (int newIndex)
{
    const int n = choices.size();
    if (n == 0) return;

    newIndex = ((newIndex % n) + n) % n;

    if (newIndex != current)
    {
        attachment.setValueAsCompleteGesture ((float) newIndex);
        if (onUserChange != nullptr) onUserChange (newIndex);
    }
}

juce::Rectangle<float> ChoiceDisplay::fieldBounds() const
{
    const auto b = getLocalBounds().toFloat();

    if (caption.isEmpty())
        return b.reduced (1.0f);

    const auto area = b.withTrimmedBottom ((float) Knob::legendHeightFor (captionSize));
    return juce::Rectangle<float> (b.getWidth() - 2.0f, juce::jmin (36.0f, area.getHeight())).withCentre (area.getCentre());
}

void ChoiceDisplay::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const auto field = fieldBounds();
    const bool hover = isMouseOver (true);

    drawField (g, field, juce::jmin (11.0f, field.getHeight() * 0.5f));

    const auto arrowColour = hover ? col::ink : col::inkFaint;
    const float ah = juce::jmin (4.6f, field.getHeight() * 0.16f);
    drawChevron (g, { field.getX() + 13.0f, field.getCentreY() }, ah, false, arrowColour);
    drawChevron (g, { field.getRight() - 13.0f, field.getCentreY() }, ah, true, arrowColour);

    g.setFont (font (FontKind::semiBold, fontSize, 0.02f));
    g.setColour (col::ink);
    g.drawText (choices[current], field.reduced (23.0f, 0.0f), juce::Justification::centred, true);

    if (caption.isNotEmpty())
    {
        g.setFont (Knob::legendFontFor (captionSize));
        g.setColour (col::ink);
        g.drawText (caption, b.withTop (b.getBottom() - (float) Knob::legendHeightFor (captionSize)).expanded (20.0f, 0.0f),
                    juce::Justification::centred, false);
    }
}

void ChoiceDisplay::showMenu()
{
    juce::PopupMenu menu;

    if (buildMenu != nullptr)
    {
        buildMenu (menu, current);
    }
    else
    {
        for (int i = 0; i < choices.size(); ++i)
            menu.addItem (i + 1, choices[i], true, i == current);
    }

    juce::Component::SafePointer<ChoiceDisplay> safe (this);
    showPopupMenu (menu, *this, [safe] (int result)
    {
        if (safe != nullptr)
            safe->setIndex (result - 1);
    });
}

void ChoiceDisplay::mouseDown (const juce::MouseEvent& e)
{
    const auto field = fieldBounds();

    if (e.position.x < field.getX() + 24.0f)            { setIndex (current - 1); return; }
    if (e.position.x > field.getRight() - 24.0f)        { setIndex (current + 1); return; }

    showMenu();
}

void ChoiceDisplay::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    wheelAccumulator += (double) wheel.deltaY * (wheel.isReversed ? -1.0 : 1.0);

    if (std::abs (wheelAccumulator) >= 0.08)
    {
        const int next = juce::jlimit (0, choices.size() - 1, current + (wheelAccumulator > 0.0 ? 1 : -1));
        wheelAccumulator = 0.0;
        setIndex (next);
    }
}

// ============================================================================
//  LevelMeter
// ============================================================================
namespace
{
    constexpr float kMeterFloorDb = -48.0f, kMeterTopDb = 3.0f;
}

void LevelMeter::push (float peak, float dt)
{
    const float db = juce::Decibels::gainToDecibels (peak, -90.0f);
    const float target = juce::jlimit (0.0f, 1.0f, juce::jmap (db, kMeterFloorDb, kMeterTopDb, 0.0f, 1.0f));

    level += (target - level) * (1.0f - std::exp (-dt / (target > level ? 0.012f : 0.28f)));

    if (target >= hold)
    {
        hold = target;
        holdTime = 0.8f;
    }
    else
    {
        holdTime -= dt;
        if (holdTime <= 0.0f)
            hold = juce::jmax (level, hold - dt * 0.7f);
    }

    clip = peak >= 0.999f ? 1.0f : juce::jmax (0.0f, clip - dt / 1.2f);

    if (std::abs (level - shown) > 0.002f || std::abs (hold - shownHold) > 0.002f || std::abs (clip - shownClip) > 0.01f)
    {
        shown = level;
        shownHold = hold;
        shownClip = clip;
        repaint();
    }
}

void LevelMeter::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const float labelW = 15.0f, lampW = 13.0f, h = juce::jmin (6.0f, b.getHeight());

    g.setFont (monoFont (11.0f));
    g.setColour (col::inkFaint);
    g.drawText (name, b.withWidth (labelW), juce::Justification::centredLeft, false);

    const juce::Rectangle<float> bar (b.getX() + labelW, b.getCentreY() - h * 0.5f, b.getWidth() - labelW - lampW, h);
    const float corner = h * 0.5f;

    g.setColour (col::track);
    g.fillRoundedRectangle (bar, corner);

    const float value = juce::jlimit (0.0f, 1.0f, level);

    if (value > 0.004f)
    {
        g.setGradientFill (accentGradient (bar));
        g.fillRoundedRectangle (bar.withWidth (juce::jmax (h, bar.getWidth() * value)), corner);
    }

    // 0 dB
    const float zero = bar.getX() + bar.getWidth() * (0.0f - kMeterFloorDb) / (kMeterTopDb - kMeterFloorDb);
    g.setColour (juce::Colours::white.withAlpha (0.95f));
    g.fillRect (zero - 0.6f, bar.getY(), 1.2f, bar.getHeight());

    if (hold > 0.02f)
    {
        const float x = bar.getX() + (bar.getWidth() - 2.0f) * juce::jlimit (0.0f, 1.0f, hold);
        g.setColour (col::ink.withAlpha (0.6f));
        g.fillRoundedRectangle (x, bar.getY() - 1.5f, 2.0f, bar.getHeight() + 3.0f, 1.0f);
    }

    drawLed (g, { b.getRight() - 4.5f, b.getCentreY() }, 2.8f, col::hot, clip);
}

// ============================================================================
//  FilterScope
// ============================================================================
void FilterScope::set (float lowCutHz, float highCutHz, float resonance)
{
    if (std::abs (std::log (lowCutHz / hp)) > 0.01f || std::abs (std::log (highCutHz / lp)) > 0.01f || std::abs (resonance - res) > 0.005f)
    {
        hp = lowCutHz;
        lp = highCutHz;
        res = resonance;
        repaint();
    }
}

void FilterScope::paint (juce::Graphics& g)
{
    g.fillAll (col::card);

    const auto b = getLocalBounds().toFloat().reduced (1.5f);
    const float corner = 12.0f;
    drawScreen (g, b, corner, 0.9f);

    juce::Graphics::ScopedSaveState state (g);
    juce::Path clip;
    clip.addRoundedRectangle (b, corner);
    g.reduceClipRegion (clip);

    drawStars (g, b, 0x5C09Eu, 16, 0.75f);

    const auto plot = b.reduced (10.0f, 7.0f);
    const float logLo = std::log (20.0f), logHi = std::log (20000.0f);
    const auto xFor = [&] (float hz) { return plot.getX() + plot.getWidth() * (std::log (hz) - logLo) / (logHi - logLo); };
    const auto yFor = [&] (float db) { return juce::jmap (juce::jlimit (-36.0f, 18.0f, db), 18.0f, -36.0f, plot.getY(), plot.getBottom()); };

    g.setColour (juce::Colours::white.withAlpha (0.07f));
    for (const float hz : { 100.0f, 1000.0f, 10000.0f })
        g.fillRect (xFor (hz) - 0.5f, b.getY(), 1.0f, b.getHeight());
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.fillRect (b.getX(), yFor (0.0f) - 0.5f, b.getWidth(), 1.0f);

    const float q = 0.707f * std::pow (8.5f, res);
    const bool lpOn = lp < 19500.0f, hpOn = hp > 21.0f;

    juce::Path curve;
    constexpr int steps = 96;

    for (int i = 0; i <= steps; ++i)
    {
        const float hz = std::exp (logLo + (logHi - logLo) * (float) i / (float) steps);
        float magSq = 1.0f;

        if (lpOn)
        {
            const float r = hz / lp;
            magSq *= 1.0f / ((1.0f - r * r) * (1.0f - r * r) + (r / q) * (r / q));
        }

        if (hpOn)
        {
            const float r = hz / hp;
            magSq *= (r * r * r * r) / ((1.0f - r * r) * (1.0f - r * r) + (r / q) * (r / q));
        }

        const float y = yFor (10.0f * std::log10 (juce::jmax (1.0e-9f, magSq)));
        const float x = plot.getX() + plot.getWidth() * (float) i / (float) steps;

        if (i == 0) curve.startNewSubPath (x, y);
        else        curve.lineTo (x, y);
    }

    juce::Path fill (curve);
    fill.lineTo (plot.getRight(), b.getBottom());
    fill.lineTo (plot.getX(), b.getBottom());
    fill.closeSubPath();

    g.setGradientFill (juce::ColourGradient (col::magenta.withAlpha (0.30f), 0.0f, plot.getY(), col::violet.withAlpha (0.02f), 0.0f, b.getBottom(), false));
    g.fillPath (fill);

    const juce::PathStrokeType glow (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    const juce::PathStrokeType line (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    g.setGradientFill (accentGradient (plot, 0.22f));
    g.strokePath (curve, glow);
    g.setGradientFill (accentGradient (plot));
    g.strokePath (curve, line);
}

} // namespace dlay::ui
