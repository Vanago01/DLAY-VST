#include "Widgets.h"

namespace dlay::ui
{

namespace
{
    constexpr float kPiF = juce::MathConstants<float>::pi;
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
    Dial (Knob& o, juce::Colour a)
        : juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox), owner (o), accent (a)
    {
        setRotaryParameters (kPiF * 1.25f, kPiF * 2.75f, true);
        setMouseDragSensitivity (190);
        setPopupMenuEnabled (false);
    }

    void paint (juce::Graphics& g) override
    {
        const float centre = (getMinimum() < 0.0 && getMaximum() > 0.0) ? (float) valueToProportionOfLength (0.0) : -1.0f;
        drawKnob (g, getLocalBounds().toFloat(), (float) valueToProportionOfLength (getValue()), centre,
                  accent, isMouseOverOrDragging(), isEnabled());
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
    juce::Colour accent;
};

Knob::Knob (APVTS& state, const juce::String& id, const juce::String& legendText, Size s, juce::Colour accent)
    : paramId (id), legend (legendText), size (s)
{
    setPaintingIsUnclipped (true);          // long legends may overhang a small dial

    dial = std::make_unique<Dial> (*this, accent);
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

int Knob::legendHeight() const noexcept
{
    return size == Size::large ? 17 : (size == Size::medium ? 15 : 14);
}

void Knob::resized()
{
    auto area = getLocalBounds();
    area.removeFromBottom (legendHeight());
    const int d = juce::jmin (area.getWidth(), area.getHeight());
    dial->setBounds (area.withSizeKeepingCentre (d, d));

    if (entry != nullptr)
        entry->setBounds (getLocalBounds().removeFromBottom (legendHeight() + 4).expanded (10, 0));
}

void Knob::paint (juce::Graphics& g)
{
    if (entry != nullptr)
        return;

    const auto textArea = getLocalBounds().removeFromBottom (legendHeight()).toFloat().expanded (24.0f, 0.0f);
    const bool touching = dial->isMouseOverOrDragging() || dial->isMouseButtonDown();

    if (touching)
    {
        g.setFont (monoFont (size == Size::large ? 12.5f : 11.0f));
        g.setColour (col::amber);
        g.drawText (dial->getTextFromValue (dial->getValue()), textArea, juce::Justification::centred, false);
    }
    else
    {
        g.setFont (font (FontKind::semiBold, size == Size::large ? 13.0f : (size == Size::medium ? 11.5f : 10.5f), 0.11f));
        g.setColour (isMouseOver (true) ? col::cream : col::creamDim);
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
    entry->setFont (monoFont (11.5f));
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
//  LeverSwitch
// ============================================================================
LeverSwitch::LeverSwitch (APVTS& state, const juce::String& paramId, const juce::String& legendText, juce::Colour led)
    : juce::Button (legendText), legend (legendText), ledColour (led), attachment (state, paramId, *this)
{
    setClickingTogglesState (true);
}

void LeverSwitch::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    const auto b = getLocalBounds().toFloat();
    const bool on = getToggleState();
    const float cx = b.getCentreX();

    drawLed (g, { cx, b.getY() + 5.0f }, 2.8f, ledColour, on ? 1.0f : 0.0f);

    const float legendH = 13.0f;
    auto plate = juce::Rectangle<float> (cx - 11.0f, b.getY() + 13.0f, 22.0f, b.getHeight() - 13.0f - legendH - 3.0f);
    const auto c = plate.getCentre();

    g.setColour (col::groove);
    g.fillRoundedRectangle (plate, 4.0f);
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.drawRoundedRectangle (plate.expanded (0.6f), 4.6f, 0.8f);

    // collar nut
    const float rn = 7.2f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff6a7078), c.x, c.y - rn, juce::Colour (0xff24272b), c.x, c.y + rn, false));
    g.fillEllipse (c.x - rn, c.y - rn, 2.0f * rn, 2.0f * rn);
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.fillEllipse (c.x - rn * 0.55f, c.y - rn * 0.55f, rn * 1.1f, rn * 1.1f);

    // lever
    const float reach = juce::jmin (13.0f, plate.getHeight() * 0.5f - 1.0f) * (on ? -1.0f : 1.0f);
    const juce::Point<float> tip (c.x, c.y + reach);

    juce::Path lever;
    lever.startNewSubPath (c.x - 2.0f, c.y);
    lever.lineTo (tip.x - 3.3f, tip.y);
    lever.lineTo (tip.x + 3.3f, tip.y);
    lever.lineTo (c.x + 2.0f, c.y);
    lever.closeSubPath();

    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillPath (lever, juce::AffineTransform::translation (1.2f, 1.5f));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe2e5e8), c.x - 3.5f, 0.0f, juce::Colour (0xff6f757c), c.x + 3.5f, 0.0f, false));
    g.fillPath (lever);

    g.setGradientFill (juce::ColourGradient (highlighted ? juce::Colours::white : juce::Colour (0xffe9ecee), tip.x - 2.0f, tip.y - 3.0f,
                                             juce::Colour (0xff596067), tip.x + 3.0f, tip.y + 4.0f, true));
    g.fillEllipse (tip.x - 4.4f, tip.y - 4.4f, 8.8f, 8.8f);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawEllipse (tip.x - 4.4f, tip.y - 4.4f, 8.8f, 8.8f, 0.7f);

    g.setFont (font (FontKind::semiBold, 10.5f, 0.11f));
    g.setColour (on ? col::cream : col::creamDim);
    g.drawText (legend, b.withTop (b.getBottom() - legendH).expanded (16.0f, 0.0f), juce::Justification::centred, false);
}

// ============================================================================
//  KeyButton
// ============================================================================
KeyButton::KeyButton (const juce::String& buttonName, juce::Colour led)
    : juce::Button (buttonName), ledColour (led)
{
}

void KeyButton::attach (APVTS& state, const juce::String& paramId)
{
    setClickingTogglesState (true);
    attachment = std::make_unique<APVTS::ButtonAttachment> (state, paramId, *this);
}

void KeyButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    const bool on = getToggleState();
    const bool pressed = down || on;
    const float glow = lit >= 0.0f ? lit : (on ? 1.0f : 0.0f);

    g.setColour (juce::Colours::black.withAlpha (0.75f));
    g.fillRoundedRectangle (b.translated (0.0f, 1.2f), 3.0f);

    if (pressed)
        b = b.translated (0.0f, 0.8f);

    const auto top    = pressed ? juce::Colour (0xff1b1d20) : (highlighted ? juce::Colour (0xff34383d) : juce::Colour (0xff2b2e33));
    const auto bottom = pressed ? juce::Colour (0xff141518) : juce::Colour (0xff1a1c1f);
    g.setGradientFill (juce::ColourGradient (top, 0.0f, b.getY(), bottom, 0.0f, b.getBottom(), false));
    g.fillRoundedRectangle (b, 3.0f);

    g.setColour (juce::Colours::white.withAlpha (pressed ? 0.04f : 0.10f));
    g.drawLine (b.getX() + 3.0f, b.getY() + 0.8f, b.getRight() - 3.0f, b.getY() + 0.8f, 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.9f));
    g.drawRoundedRectangle (b, 3.0f, 1.0f);

    auto textArea = b;

    if (showLed)
    {
        const float slotW = juce::jmin (18.0f, b.getWidth() * 0.4f);
        const auto slot = juce::Rectangle<float> (b.getCentreX() - slotW * 0.5f, b.getY() + 4.5f, slotW, 3.0f);

        if (glow > 0.02f)
        {
            g.setColour (ledColour.withAlpha (0.22f * glow));
            g.fillRoundedRectangle (slot.expanded (4.0f, 3.5f), 4.0f);
        }

        g.setColour (ledColour.withMultipliedBrightness (0.2f).interpolatedWith (ledColour.interpolatedWith (juce::Colours::white, 0.2f), glow));
        g.fillRoundedRectangle (slot, 1.5f);
        g.setColour (juce::Colours::black.withAlpha (0.8f));
        g.drawRoundedRectangle (slot.expanded (0.6f), 2.0f, 0.8f);

        textArea = b.withTrimmedTop (7.0f);
    }

    const auto textColour = (on || glow > 0.5f ? col::cream : (highlighted ? col::cream : col::creamDim))
                                .withMultipliedAlpha (isEnabled() ? 1.0f : 0.35f);

    if (drawIcon != nullptr)
    {
        drawIcon (g, textArea, textColour);
        return;
    }

    g.setFont (font (FontKind::semiBold, fontHeight, 0.13f));
    g.setColour (textColour);
    g.drawText (getButtonText(), textArea, juce::Justification::centred, false);
}

// ============================================================================
//  SegmentSwitch
// ============================================================================
SegmentSwitch::SegmentSwitch (APVTS& state, const juce::String& paramId, juce::StringArray names)
    : labels (std::move (names)),
      attachment (*state.getParameter (paramId),
                  [this] (float v)
                  {
                      current = juce::roundToInt (v);
                      repaint();
                      if (onChange != nullptr) onChange (current);
                  },
                  state.undoManager)
{
    attachment.sendInitialUpdate();
}

void SegmentSwitch::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const int n = juce::jmax (1, labels.size());
    const float cell = b.getWidth() / (float) n;

    g.setColour (col::groove);
    g.fillRoundedRectangle (b, 4.0f);
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.drawRoundedRectangle (b.reduced (0.4f), 4.0f, 0.8f);

    for (int i = 0; i < n; ++i)
    {
        const auto r = juce::Rectangle<float> (b.getX() + cell * (float) i, b.getY(), cell, b.getHeight());
        const bool selected = (i == current);
        auto colour = col::creamDim;

        if (selected)
        {
            const auto cap = r.reduced (2.0f);
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.fillRoundedRectangle (cap.translated (0.0f, 1.0f), 2.5f);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff2e9d3), 0.0f, cap.getY(), juce::Colour (0xffc9c0a8), 0.0f, cap.getBottom(), false));
            g.fillRoundedRectangle (cap, 2.5f);
            colour = col::ink;
        }
        else if (i > 0 && i != current + 1)
        {
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.drawLine (r.getX(), r.getY() + 4.0f, r.getX(), r.getBottom() - 4.0f, 1.0f);
        }

        if (drawIcon != nullptr)
        {
            drawIcon (g, r.reduced (4.0f, 4.0f), i, colour);
        }
        else
        {
            g.setFont (font (FontKind::semiBold, juce::jmin (11.5f, b.getHeight() * 0.56f), 0.1f));
            g.setColour (colour);
            g.drawText (labels[i], r, juce::Justification::centred, false);
        }
    }
}

void SegmentSwitch::mouseDown (const juce::MouseEvent& e)
{
    const int n = juce::jmax (1, labels.size());
    const int index = juce::jlimit (0, n - 1, (int) ((float) e.x / ((float) getWidth() / (float) n)));

    if (index != current)
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

void ChoiceDisplay::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    drawScreen (g, b.reduced (1.0f), 3.0f);

    const bool hover = isMouseOver (true);
    const float ay = b.getCentreY(), ah = juce::jmin (4.5f, b.getHeight() * 0.2f);

    juce::Path left, right;
    left.addTriangle (b.getX() + 6.0f, ay, b.getX() + 6.0f + ah * 1.3f, ay - ah, b.getX() + 6.0f + ah * 1.3f, ay + ah);
    right.addTriangle (b.getRight() - 6.0f, ay, b.getRight() - 6.0f - ah * 1.3f, ay - ah, b.getRight() - 6.0f - ah * 1.3f, ay + ah);
    g.setColour (hover ? col::creamDim : col::creamFaint);
    g.fillPath (left);
    g.fillPath (right);

    auto textArea = b.reduced (16.0f, 0.0f);

    if (caption.isNotEmpty())
    {
        g.setFont (font (FontKind::semiBold, 8.5f, 0.16f));
        g.setColour (col::creamFaint);
        g.drawText (caption, textArea.removeFromTop (b.getHeight() * 0.36f).translated (0.0f, 2.5f), juce::Justification::centred, false);
        textArea = textArea.translated (0.0f, -1.5f);
    }

    drawGlowText (g, choices[current], textArea, juce::Justification::centred, monoFont (fontHeight), colour);
}

void ChoiceDisplay::mouseDown (const juce::MouseEvent& e)
{
    if (e.x < 18)                       { setIndex (current - 1); return; }
    if (e.x > getWidth() - 18)          { setIndex (current + 1); return; }

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
//  VuMeter
// ============================================================================
void VuMeter::push (float peak, float dt)
{
    const float db = juce::Decibels::gainToDecibels (peak, -60.0f);
    const float target = juce::jlimit (0.0f, 1.0f, juce::jmap (db, -24.0f, 3.0f, 0.0f, 1.0f));

    const float coef = 1.0f - std::exp (-dt / (target > needle ? 0.07f : 0.38f));
    needle += coef * (target - needle);

    clipHold = peak >= 0.98f ? 1.0f : juce::jmax (0.0f, clipHold - dt / 0.9f);

    if (std::abs (needle - shown) > 0.0015f || clipHold > 0.0f)
    {
        shown = needle;
        repaint();
    }
}

void VuMeter::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced (1.0f);

    g.fillAll (col::panel);
    g.setColour (juce::Colours::black);
    g.fillRoundedRectangle (b, 4.0f);

    const auto face = b.reduced (3.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff1e8d2), 0.0f, face.getY(), juce::Colour (0xffd6ccb3), 0.0f, face.getBottom(), false));
    g.fillRoundedRectangle (face, 2.5f);

    juce::Graphics::ScopedSaveState state (g);
    juce::Path clip;
    clip.addRoundedRectangle (face, 2.5f);
    g.reduceClipRegion (clip);

    const juce::Point<float> pivot (face.getCentreX(), face.getBottom() + face.getHeight() * 0.42f);
    const float radius = face.getHeight() * 1.18f;
    constexpr float sweep = 0.74f;                         // radians either side of vertical

    const auto angleFor = [] (float proportion) { return -sweep + 2.0f * sweep * proportion; };
    const auto dbToProp = [] (float db) { return juce::jlimit (0.0f, 1.0f, juce::jmap (db, -24.0f, 3.0f, 0.0f, 1.0f)); };

    // scale arcs
    {
        juce::Path arc, hot;
        arc.addCentredArc (pivot.x, pivot.y, radius, radius, 0.0f, angleFor (0.0f), angleFor (dbToProp (0.0f)), true);
        hot.addCentredArc (pivot.x, pivot.y, radius, radius, 0.0f, angleFor (dbToProp (0.0f)), angleFor (1.0f), true);
        g.setColour (col::ink.withAlpha (0.85f));
        g.strokePath (arc, juce::PathStrokeType (1.1f));
        g.setColour (col::red);
        g.strokePath (hot, juce::PathStrokeType (2.6f));
    }

    static constexpr float marks[] = { -20.0f, -10.0f, -7.0f, -5.0f, -3.0f, -1.0f, 0.0f, 1.0f, 2.0f, 3.0f };
    g.setFont (monoFont (6.8f));

    for (const float db : marks)
    {
        const float a = angleFor (dbToProp (db));
        const float sx = std::sin (a), cy = -std::cos (a);
        const bool major = db == -20.0f || db == -10.0f || db == -5.0f || db == 0.0f || db == 3.0f;

        g.setColour (db > 0.0f ? col::red : col::ink.withAlpha (0.9f));
        g.drawLine (pivot.x + sx * radius, pivot.y + cy * radius,
                    pivot.x + sx * (radius + (major ? 5.5f : 3.5f)), pivot.y + cy * (radius + (major ? 5.5f : 3.5f)), major ? 1.1f : 0.8f);

        if (major)
        {
            const juce::Point<float> p (pivot.x + sx * (radius + 11.0f), pivot.y + cy * (radius + 11.0f));
            g.drawText (juce::String (juce::roundToInt (std::abs (db))), juce::Rectangle<float> (p.x - 8.0f, p.y - 4.0f, 16.0f, 8.0f),
                        juce::Justification::centred, false);
        }
    }

    g.setFont (font (FontKind::bold, 9.0f, 0.1f));
    g.setColour (col::ink.withAlpha (0.8f));
    g.drawText ("VU", juce::Rectangle<float> (face.getCentreX() - 12.0f, face.getBottom() - 17.0f, 24.0f, 10.0f), juce::Justification::centred, false);
    g.setFont (font (FontKind::semiBold, 8.5f, 0.1f));
    g.drawText (name, juce::Rectangle<float> (face.getX() + 5.0f, face.getBottom() - 12.0f, 14.0f, 9.0f), juce::Justification::centredLeft, false);

    // needle
    {
        const float a = angleFor (needle);
        const float sx = std::sin (a), cy = -std::cos (a);
        const juce::Point<float> tip (pivot.x + sx * (radius + 4.0f), pivot.y + cy * (radius + 4.0f));

        g.setColour (juce::Colours::black.withAlpha (0.22f));
        g.drawLine (pivot.x + 1.6f, pivot.y + 1.2f, tip.x + 1.6f, tip.y + 1.2f, 1.6f);
        g.setColour (juce::Colour (0xff1c1a16));
        g.drawLine (pivot.x, pivot.y, tip.x, tip.y, 1.15f);
    }

    // peak lamp
    drawLed (g, { face.getRight() - 8.0f, face.getBottom() - 8.0f }, 2.4f, col::red, clipHold);

    // glass
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.16f), face.getX(), face.getY(),
                                             juce::Colours::transparentWhite, face.getX() + face.getWidth() * 0.55f, face.getBottom(), false));
    g.fillRect (face);
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
    const auto b = getLocalBounds().toFloat().reduced (1.0f);
    drawScreen (g, b, 3.0f);

    const auto plot = b.reduced (4.0f, 3.0f);
    const float logLo = std::log (20.0f), logHi = std::log (20000.0f);
    const auto xFor = [&] (float hz) { return plot.getX() + plot.getWidth() * (std::log (hz) - logLo) / (logHi - logLo); };
    const auto yFor = [&] (float db) { return juce::jmap (juce::jlimit (-36.0f, 18.0f, db), 18.0f, -36.0f, plot.getY(), plot.getBottom()); };

    g.setColour (col::creamFaint.withAlpha (0.22f));
    for (const float hz : { 100.0f, 1000.0f, 10000.0f })
        g.drawVerticalLine (juce::roundToInt (xFor (hz)), plot.getY(), plot.getBottom());
    g.drawHorizontalLine (juce::roundToInt (yFor (0.0f)), plot.getX(), plot.getRight());

    const float q = 0.707f * std::pow (8.5f, res);
    const bool lpOn = lp < 19500.0f, hpOn = hp > 21.0f;

    juce::Path curve;
    constexpr int steps = 72;

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
    fill.lineTo (plot.getRight(), plot.getBottom());
    fill.lineTo (plot.getX(), plot.getBottom());
    fill.closeSubPath();

    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (plot.toNearestInt());
        g.setColour (col::amber.withAlpha (0.13f));
        g.fillPath (fill);
        g.setColour (col::amber.withAlpha (0.25f));
        g.strokePath (curve, juce::PathStrokeType (3.2f));
        g.setColour (col::amber);
        g.strokePath (curve, juce::PathStrokeType (1.2f));
    }
}

} // namespace dlay::ui
