#include "PluginEditor.h"
#include "UI/Widgets.h"
#include "UI/EchoVisualizer.h"
#include "UI/MotionEditor.h"
#include "UI/PresetBrowser.h"

using namespace dlay;
using namespace dlay::ui;

namespace
{
    constexpr float kPiF = juce::MathConstants<float>::pi;

    // ------------------------------------------------------------------------
    //  tooltips
    // ------------------------------------------------------------------------
    juce::String tooltipFor (const juce::String& paramId)
    {
        static const std::map<juce::String, juce::String> tips
        {
            { id::time,       "Delay time. Changing it bends the pitch like a tape machine; INERTIA sets how heavily." },
            { id::division,   "Delay time as a note value at the host tempo." },
            { id::sync,       "Lock the delay time to the host tempo." },
            { id::feedback,   "How much of each echo is fed back. Above 100 % the loop builds on itself - the internal protection catches it, not your speakers." },
            { id::mix,        "Balance between the dry signal and the echoes." },
            { id::stereoMode, "STEREO keeps left and right apart, MONO sums the input, MID/SIDE delays the centre and the sides separately." },
            { id::pingPong,   "Throws the input to one side and crosses the feedback so the repeats bounce left and right." },
            { id::width,      "Stereo width of the echoes: 0 % mono, 100 % as made, 200 % extra wide." },
            { id::offset,     "Makes one side later than the other by up to half the delay time. Left of centre delays the left side." },
            { id::spread,     "Spreads taps, grains or spectral bands across the stereo field. On single-tap algorithms it pulls the left and right times slightly apart." },
            { id::saturation, "Saturation inside the feedback loop: every repeat is pushed a little harder. It never changes how fast the echoes decay." },
            { id::drive,      "Overdrives the signal on its way into the delay." },
            { id::satType,    "Saturation character: TAPE (soft), TUBE (warm, uneven), DIODE (firm clip), FOLD (wave-folder), CRUSH (bit reduction)." },
            { id::lowCut,     "High-pass filter in the feedback loop: every repeat loses more low end." },
            { id::highCut,    "Low-pass filter in the feedback loop: every repeat gets darker." },
            { id::resonance,  "Emphasis at both filter corners. High settings make the echoes ring and whistle." },
            { id::diffusion,  "Smears each repeat through all-pass filters. With feedback, echoes dissolve into a wash." },
            { id::modDepth,   "Chorus-style modulation of the delay time." },
            { id::modRate,    "Speed of the delay-time modulation." },
            { id::pitch,      "Pitch shift inside the feedback loop: every repeat is transposed again." },
            { id::reverse,    "Blends in echoes played backwards. In the REVERSE algorithm it sets how much of the feedback is re-reversed." },
            { id::ducking,    "Turns the echoes down while you play and lets them bloom in the gaps." },
            { id::freeze,     "FREEZE: holds what is on the tape, loss-free, for as long as you like. The input is muted while frozen; every reader keeps working." },
            { id::infinite,   "INFINITE UNIVERSE: the loop sustains for ever at a controlled level while still taking in new sound. Filters, pitch and diffusion keep reshaping it." },
            { id::warp,       "TIME WARP: bends time. Right accelerates the echoes (up to 4x), left slows them down, with tape-style pitch glides." },
            { id::inertia,    "How heavy the tape flywheel is: how slowly the delay follows time changes, and so how long the pitch bends last." },
            { id::orbit,      "ECHO ORBIT: sends the echoes travelling along a path through the stereo field. This sets the size of the path." },
            { id::orbitRate,  "Speed of the orbit when it is not synced." },
            { id::orbitShape, "Orbit path: circle, figure 8, pendulum, spiral, or scatter (a new random spot every cycle)." },
            { id::orbitSync,  "Lock the orbit to the host tempo." },
            { id::orbitDiv,   "Length of one orbit in note values." },
            { id::ghost,      "GHOST MODE: faint, filtered, slowly detuning apparitions of earlier repeats that drift across the stereo field." },
            { id::collapse,   "TIME COLLAPSE: adds echoes that get closer and closer together (right, like a bouncing ball) or further apart (left)." },
            { id::random,     "RANDOM UNIVERSE: on every cycle, nudges time, position, filter and feedback by a controlled random amount." },
            { id::tapeMemory, "TAPE MEMORY: wears out the tape. Wow, flutter, drifting speed, dull highs, dropouts and saturation - on any algorithm." },
            { id::outGain,    "Output level." },
            { id::limiter,    "Output safety limiter. Leave it on unless you have a reason not to. The light shows when it is working." },
            { id::bypass,     "Power. Click to bypass DLAY: the echoes fade out smoothly instead of clicking off." },
            { id::quality,    "Oversampling of the saturation stages: ECO 1x, HIGH 2x, ULTRA 4x." },
            { id::mode,       "Delay algorithm. Use the arrows or the mouse wheel to step, click for the full list." },
        };

        const auto it = tips.find (paramId);
        return it != tips.end() ? it->second : juce::String();
    }

    juce::String formatMs (float ms)
    {
        // (juce::String (value, 0) means "as many decimals as it takes", so whole numbers are rounded explicitly)
        if (ms >= 1000.0f)  return juce::String (ms / 1000.0f, 2) + " s";
        if (ms >= 100.0f)   return juce::String (juce::roundToInt (ms)) + " ms";
        return juce::String (ms, 1) + " ms";
    }

    const char* const kFamilyNames[] = { "CLASSIC", "VINTAGE", "CREATIVE" };

    // ------------------------------------------------------------------------
    //  glyphs
    // ------------------------------------------------------------------------
    const juce::PathStrokeType kIconStroke (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    void iconChevron (juce::Graphics& g, juce::Point<float> c, float size, juce::Colour colour, bool pointsRight)
    {
        const float d = pointsRight ? 1.0f : -1.0f;
        juce::Path p;
        p.startNewSubPath (c.x - d * size * 0.45f, c.y - size);
        p.lineTo (c.x + d * size * 0.55f, c.y);
        p.lineTo (c.x - d * size * 0.45f, c.y + size);
        g.setColour (colour);
        g.strokePath (p, kIconStroke);
    }

    juce::Path heartPath (juce::Point<float> c, float s)
    {
        juce::Path heart;
        heart.startNewSubPath (c.x, c.y + s * 0.92f);
        heart.cubicTo (c.x - s * 1.45f, c.y - s * 0.05f, c.x - s * 0.80f, c.y - s * 1.25f, c.x, c.y - s * 0.42f);
        heart.cubicTo (c.x + s * 0.80f, c.y - s * 1.25f, c.x + s * 1.45f, c.y - s * 0.05f, c.x, c.y + s * 0.92f);
        heart.closeSubPath();
        return heart;
    }

    void iconHeart (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour colour, bool filled)
    {
        const auto c = r.getCentre().translated (0.0f, 0.6f);
        const auto heart = heartPath (c, 7.4f);

        if (filled)
        {
            g.setGradientFill (accentGradient ({ c.x - 8.0f, c.y }, { c.x + 8.0f, c.y }));
            g.fillPath (heart);
        }
        else
        {
            g.setColour (colour);
            g.strokePath (heart, kIconStroke);
        }
    }

    void iconCurvedArrow (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, bool redo)
    {
        const auto ctr = r.getCentre();
        const float rad = 6.6f, d = redo ? 1.0f : -1.0f;
        juce::Path arc;
        arc.addCentredArc (ctr.x, ctr.y + 1.2f, rad, rad, 0.0f, d * -2.2f, d * 1.2f, true);
        g.setColour (c);
        g.strokePath (arc, kIconStroke);

        const juce::Point<float> tip (ctr.x + d * rad * std::sin (1.2f), ctr.y + 1.2f - rad * std::cos (1.2f));
        juce::Path head;
        head.addTriangle (tip.x + d * 1.2f, tip.y + 5.4f, tip.x - d * 5.0f, tip.y + 0.7f, tip.x + d * 3.8f, tip.y - 2.6f);
        g.fillPath (head);
    }

    void iconPower (juce::Graphics& g, juce::Rectangle<float> r, bool lit, float alpha)
    {
        const auto c = r.getCentre();
        const float rad = 7.2f;
        juce::Path p;
        p.addCentredArc (c.x, c.y + 0.8f, rad, rad, 0.0f, 0.62f, 2.0f * kPiF - 0.62f, true);
        p.startNewSubPath (c.x, c.y - rad - 1.4f);
        p.lineTo (c.x, c.y - 0.6f);

        if (lit)    g.setGradientFill (accentGradient ({ c.x - rad, c.y }, { c.x + rad, c.y }, alpha));
        else        g.setColour (col::inkFaint.withMultipliedAlpha (alpha));

        g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    /** Every algorithm has its own little constellation, made from its number. */
    void drawConstellation (juce::Graphics& g, juce::Rectangle<float> area, int mode)
    {
        juce::Random rng ((juce::int64) (0x51DE5EED + mode * 7919));
        const auto c = area.getCentre();
        const float R = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f;

        const int n = 5 + rng.nextInt (3);
        std::vector<juce::Point<float>> stars;
        float angle = rng.nextFloat() * 2.0f * kPiF;

        for (int i = 0; i < n; ++i)
        {
            const float r = R * (0.42f + 0.52f * rng.nextFloat());
            stars.push_back ({ c.x + std::sin (angle) * r, c.y - std::cos (angle) * r });
            angle += (2.0f * kPiF / (float) n) * (0.62f + 0.5f * rng.nextFloat());
        }

        // one more star off the figure, joined to its middle
        const auto spur = c + juce::Point<float> ((rng.nextFloat() - 0.5f) * R * 0.5f, (rng.nextFloat() - 0.5f) * R * 0.5f);

        juce::Path lines;
        for (int i = 0; i < n; ++i)
        {
            if (i == 0) lines.startNewSubPath (stars[(size_t) i]);
            else        lines.lineTo (stars[(size_t) i]);
        }
        lines.startNewSubPath (stars[(size_t) (n / 2)]);
        lines.lineTo (spur);

        g.setGradientFill (accentGradient ({ area.getX(), c.y }, { area.getRight(), c.y }, 0.8f));
        g.strokePath (lines, juce::PathStrokeType (1.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        stars.push_back (spur);
        const int brightest = rng.nextInt (n);

        for (int i = 0; i < (int) stars.size(); ++i)
        {
            const auto p = stars[(size_t) i];
            const bool big = i == brightest;
            const float r = big ? 2.6f : 1.3f + 0.9f * rng.nextFloat();
            const auto tint = (i % 2) == 0 ? col::pink : col::purple;
            const float halo = r * 3.2f;

            g.setGradientFill (juce::ColourGradient (tint.withAlpha (0.55f), p.x, p.y, tint.withAlpha (0.0f), p.x + halo, p.y, true));
            g.fillEllipse (p.x - halo, p.y - halo, 2.0f * halo, 2.0f * halo);

            if (big)
            {
                g.setColour (col::star.withAlpha (0.8f));
                g.fillRect (p.x - 6.5f, p.y - 0.45f, 13.0f, 0.9f);
                g.fillRect (p.x - 0.45f, p.y - 6.5f, 0.9f, 13.0f);
            }

            g.setColour (col::star);
            g.fillEllipse (p.x - r, p.y - r, 2.0f * r, 2.0f * r);
        }
    }

    // ------------------------------------------------------------------------
    /** Preset name on a dark pill; click to open the library. */
    class PresetDisplay : public juce::Component,
                          public juce::SettableTooltipClient
    {
    public:
        std::function<void()> onClick;
        juce::String name, category;
        bool modified = false, open = false;

        void paint (juce::Graphics& g) override
        {
            const auto b = getLocalBounds().toFloat().reduced (1.5f);
            const float corner = b.getHeight() * 0.5f;
            drawScreen (g, b, corner, 0.85f);

            {
                juce::Graphics::ScopedSaveState state (g);
                juce::Path clip;
                clip.addRoundedRectangle (b, corner);
                g.reduceClipRegion (clip);
                drawStars (g, b, 0x9E5E7u, 26, 0.7f);
            }

            auto text = b.reduced (24.0f, 0.0f).withTrimmedRight (20.0f);

            drawAccentText (g, category.isEmpty() ? juce::String ("PRESET") : category.toUpperCase(),
                            text.withY (b.getY() + 6.0f).withHeight (13.0f), juce::Justification::centredLeft, capsFont (10.0f));

            g.setFont (font (FontKind::bold, 16.0f, 0.0f));
            g.setColour (col::star);
            g.drawText (name + (modified ? " *" : ""), text.withY (b.getY() + 17.5f).withHeight (22.0f), juce::Justification::centredLeft, true);

            // chevron: the library opens below
            const auto ctr = juce::Point<float> (b.getRight() - 24.0f, b.getCentreY());
            juce::Path chevron;
            const float flip = open ? -1.0f : 1.0f;
            chevron.startNewSubPath (ctr.x - 5.0f, ctr.y - 2.4f * flip);
            chevron.lineTo (ctr.x, ctr.y + 2.8f * flip);
            chevron.lineTo (ctr.x + 5.0f, ctr.y - 2.4f * flip);
            g.setColour (isMouseOver() ? col::star : col::starDim);
            g.strokePath (chevron, kIconStroke);
        }

        void mouseDown (const juce::MouseEvent&) override   { if (onClick != nullptr) onClick(); }
        void mouseEnter (const juce::MouseEvent&) override  { repaint(); }
        void mouseExit (const juce::MouseEvent&) override   { repaint(); }
    };

    // ------------------------------------------------------------------------
    /** Shows the delay time the way it is set: a note value when synced, milliseconds when free. */
    class TimeReadout : public juce::Component,
                        public juce::SettableTooltipClient
    {
    public:
        TimeReadout (DlayAudioProcessor& p)
            : proc (p),
              division (*p.apvts.getParameter (id::division), [this] (float v) { divisionIndex = juce::roundToInt (v); repaint(); }, &p.undoManager)
        {
            pSync = p.apvts.getRawParameterValue (id::sync);
            pTime = p.apvts.getRawParameterValue (id::time);
            division.sendInitialUpdate();
            setTooltip ("The delay time. When synced: click for the note values, or use the mouse wheel.");
        }

        void refresh()
        {
            const bool s = pSync->load() > 0.5f;
            const float ms = s ? proc.engine.visual.baseTimeMs.load() : pTime->load();

            if (s != synced || std::abs (ms - shownMs) > 0.05f)
            {
                synced = s;
                shownMs = ms;
                repaint();
            }
        }

        void paint (juce::Graphics& g) override
        {
            const auto b = getLocalBounds().toFloat().reduced (1.5f);
            drawScreen (g, b, 12.0f, 0.8f);

            const juce::String value = synced ? juce::String (kDivisions[(size_t) juce::jlimit (0, kNumDivisions - 1, divisionIndex)].name) : formatMs (shownMs);

            g.setFont (monoFont (19.0f));
            g.setColour (col::star);
            g.drawText (value, b.withTrimmedBottom (b.getHeight() * 0.36f).translated (0.0f, 3.0f), juce::Justification::centred, false);

            g.setFont (capsFont (10.0f));
            g.setColour (col::starDim);
            g.drawText (synced ? "= " + formatMs (shownMs).toUpperCase() : juce::String ("FREE RUNNING"),
                        b.withTop (b.getBottom() - 19.0f).translated (0.0f, -4.0f), juce::Justification::centred, false);
        }

        void mouseDown (const juce::MouseEvent&) override
        {
            if (! synced)
                return;

            juce::PopupMenu menu;
            for (int i = 0; i <= (int) Div::bar2; ++i)
                menu.addItem (i + 1, kDivisions[(size_t) i].name, true, i == divisionIndex);

            juce::Component::SafePointer<TimeReadout> safe (this);
            showPopupMenu (menu, *this, [safe] (int result)
            {
                if (safe != nullptr)
                    safe->division.setValueAsCompleteGesture ((float) (result - 1));
            });
        }

        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
        {
            if (! synced)
                return;

            wheelAccumulator += (double) wheel.deltaY * (wheel.isReversed ? -1.0 : 1.0);

            if (std::abs (wheelAccumulator) >= 0.08)
            {
                const int next = juce::jlimit (0, (int) Div::bar2, divisionIndex + (wheelAccumulator > 0.0 ? 1 : -1));
                wheelAccumulator = 0.0;
                if (next != divisionIndex)
                    division.setValueAsCompleteGesture ((float) next);
            }
        }

    private:
        DlayAudioProcessor& proc;
        std::atomic<float>* pSync = nullptr, *pTime = nullptr;
        int divisionIndex = 0;
        bool synced = true;
        float shownMs = -1.0f;
        double wheelAccumulator = 0.0;
        juce::ParameterAttachment division;
    };

    // ------------------------------------------------------------------------
    /** The algorithm, on a screen of its own: the three families along the top, the
        algorithm's constellation and name, and one dot for each algorithm of its family.
        Click a family, an arrow or a dot to go there; anywhere else opens the full list. */
    class ModeScreen : public ChoiceDisplay
    {
    public:
        explicit ModeScreen (APVTS& state) : ChoiceDisplay (state, id::mode) {}

        void paint (juce::Graphics& g) override
        {
            const auto b = getLocalBounds().toFloat().reduced (1.5f);
            constexpr float corner = 14.0f;
            drawScreen (g, b, corner);

            {
                juce::Graphics::ScopedSaveState state (g);
                juce::Path clip;
                clip.addRoundedRectangle (b, corner);
                g.reduceClipRegion (clip);
                drawStars (g, b, 0xA11C0u, 40, 0.8f);
            }

            const auto& info = modeInfo (current);
            const auto zones = layout();
            const bool hover = isMouseOver (true);

            for (int f = 0; f < 3; ++f)
            {
                if (f == (int) info.family)
                {
                    drawAccentText (g, kFamilyNames[f], zones.family[f], juce::Justification::centredLeft, capsFont (10.0f));
                }
                else
                {
                    g.setFont (capsFont (10.0f));
                    g.setColour (col::starFaint);
                    g.drawText (kFamilyNames[f], zones.family[f], juce::Justification::centredLeft, false);
                }
            }

            const auto arrow = hover ? col::star : col::starDim;
            iconChevron (g, zones.previous.getCentre(), 4.6f, arrow, false);
            iconChevron (g, zones.next.getCentre(), 4.6f, arrow, true);

            drawConstellation (g, zones.glyph, current);

            g.setFont (font (FontKind::heavy, 21.0f, -0.005f));
            g.setColour (col::star);
            g.drawText (info.name, zones.name, juce::Justification::centredLeft, true);

            for (int k = 0; k < (int) zones.dots.size(); ++k)
            {
                const auto d = zones.dots[(size_t) k];

                if (zones.firstOfFamily + k == current)
                {
                    g.setGradientFill (accentGradient (d));
                    g.fillRoundedRectangle (d, d.getHeight() * 0.5f);
                }
                else
                {
                    g.setColour (juce::Colours::white.withAlpha (0.26f));
                    g.fillEllipse (d);
                }
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const auto zones = layout();
            const auto pos = e.position;

            for (int f = 0; f < 3; ++f)
            {
                if (zones.family[f].expanded (5.0f, 7.0f).contains (pos))
                {
                    if ((int) modeInfo (current).family != f)
                        for (int m = 0; m < kNumModes; ++m)
                            if ((int) modeInfo (m).family == f) { setIndex (m); break; }
                    return;
                }
            }

            if (zones.previous.contains (pos))      { setIndex (current - 1); return; }
            if (zones.next.contains (pos))          { setIndex (current + 1); return; }

            for (int k = 0; k < (int) zones.dots.size(); ++k)
            {
                if (zones.dots[(size_t) k].expanded (3.5f, 8.0f).contains (pos))
                {
                    setIndex (zones.firstOfFamily + k);
                    return;
                }
            }

            showMenu();
        }

    private:
        struct Zones
        {
            juce::Rectangle<float> family[3], previous, next, glyph, name;
            std::vector<juce::Rectangle<float>> dots;
            int firstOfFamily = 0;
        };

        Zones layout() const
        {
            Zones z;
            const auto b = getLocalBounds().toFloat().reduced (1.5f);
            const auto f = capsFont (10.0f);

            float x = b.getX() + 15.0f;
            for (int i = 0; i < 3; ++i)
            {
                const float w = textWidth (f, kFamilyNames[i]) + 2.0f;
                z.family[i] = { x, b.getY() + 9.0f, w, 14.0f };
                x += w + 11.0f;
            }

            z.next     = { b.getRight() - 34.0f, b.getY() + 3.0f, 30.0f, 26.0f };
            z.previous = z.next.translated (-30.0f, 0.0f);

            z.glyph = { b.getX() + 13.0f, b.getY() + 30.0f, 46.0f, 46.0f };
            z.name  = { b.getX() + 70.0f, b.getY() + 30.0f, b.getWidth() - 82.0f, 26.0f };

            const int family = (int) modeInfo (current).family;
            int count = 0;
            z.firstOfFamily = -1;

            for (int m = 0; m < kNumModes; ++m)
            {
                if ((int) modeInfo (m).family == family)
                {
                    if (z.firstOfFamily < 0) z.firstOfFamily = m;
                    ++count;
                }
            }

            float dx = b.getX() + 71.0f;
            const float dy = b.getY() + 63.0f;

            for (int k = 0; k < count; ++k)
            {
                const bool selected = z.firstOfFamily + k == current;
                const float w = selected ? 17.0f : 5.0f;
                z.dots.push_back ({ dx, dy, w, 5.0f });
                dx += w + 6.0f;
            }

            return z;
        }
    };

    // ------------------------------------------------------------------------
    //  the lower card's pages: which dial sits where, and what it says underneath
    // ------------------------------------------------------------------------
    struct DockCell { const char* paramId; const char* legend; const char* line; };

    constexpr DockCell kTimeCells[] =
    {
        { id::warp,       "Time Warp",       "Speeds the echoes up or slows them down" },
        { id::collapse,   "Time Collapse",   "Echoes bunch up like a bouncing ball" },
        { id::random,     "Random Universe", "Rolls new times and places every cycle" },
        { id::tapeMemory, "Tape Memory",     "Wow, flutter, dropouts and worn highs" },
        { id::pitch,      "Pitch",           "Transposes every repeat again" },
        { id::reverse,    "Reverse",         "Blends in echoes played backwards" },
    };

    constexpr const char* kTimeHint  = "Bend time.  Shift-drag any dial for fine control, double-click it to reset, double-click its name to type a value.";
    constexpr const char* kSpaceHint = "Shape space.  Drag inside the orbit radar on the display to set the path, double-click it for the next shape.";
}

// ============================================================================
//  Content: everything, in design units
// ============================================================================
class DlayAudioProcessorEditor::Content : public juce::Component
{
public:
    Content (DlayAudioProcessorEditor& e, DlayAudioProcessor& p)
        : editor (e), proc (p), apvts (p.apvts), visualizer (p), motion (p), browser (p), timeReadout (p)
    {
        using Size = Knob::Size;
        using Look = KeyButton::Look;
        using OnFill = KeyButton::OnFill;

        // ---- top bar ---------------------------------------------------------
        addAndMakeVisible (bypassKey);
        bypassKey.attach (apvts, id::bypass);
        bypassKey.setLook (Look::round);
        bypassKey.setOnFill (OnFill::none);
        bypassKey.drawIcon = [this] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
        {
            iconPower (g, r, ! bypassKey.getToggleState(), c.getFloatAlpha());
        };
        bypassKey.setTooltip (tooltipFor (id::bypass));

        addAndMakeVisible (prevKey);
        prevKey.setLook (Look::round);
        prevKey.drawIcon = [] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) { iconChevron (g, r.getCentre().translated (-0.6f, 0.0f), 5.4f, c, false); };
        prevKey.setTooltip ("Previous preset");
        prevKey.onClick = [this] { stepPreset (-1); };

        addAndMakeVisible (nextKey);
        nextKey.setLook (Look::round);
        nextKey.drawIcon = [] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) { iconChevron (g, r.getCentre().translated (0.6f, 0.0f), 5.4f, c, true); };
        nextKey.setTooltip ("Next preset");
        nextKey.onClick = [this] { stepPreset (1); };

        addAndMakeVisible (presetDisplay);
        presetDisplay.setTooltip ("Open the preset library: search, categories, favourites, save.");
        presetDisplay.onClick = [this] { showBrowser (! browser.isVisible(), false); };

        addAndMakeVisible (favKey);
        favKey.setLook (Look::round);
        favKey.drawIcon = [this] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
        {
            iconHeart (g, r, c, proc.presets->isFavourite (proc.presets->getCurrentIndex()));
        };
        favKey.setTooltip ("Mark this preset as a favourite");
        favKey.onClick = [this]
        {
            const int index = proc.presets->getCurrentIndex();
            if (index >= 0)
                proc.presets->setFavourite (index, ! proc.presets->isFavourite (index));
            favKey.repaint();
        };

        addAndMakeVisible (saveKey);
        saveKey.setTooltip ("Save the current sound as a preset");
        saveKey.onClick = [this] { showBrowser (true, true); };

        addAndMakeVisible (undoKey);
        undoKey.setLook (Look::round);
        undoKey.drawIcon = [] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) { iconCurvedArrow (g, r, c, false); };
        undoKey.setTooltip ("Undo");
        undoKey.onClick = [this] { proc.undo(); };

        addAndMakeVisible (redoKey);
        redoKey.setLook (Look::round);
        redoKey.drawIcon = [] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) { iconCurvedArrow (g, r, c, true); };
        redoKey.setTooltip ("Redo");
        redoKey.onClick = [this] { proc.redo(); };

        addAndMakeVisible (viewKey);
        viewKey.setTooltip ("Show or hide the lower panel with the TIME, SPACE and MOTION pages.");
        viewKey.onClick = [this] { editor.setAdvancedView (! editor.isAdvancedView()); };

        addAndMakeVisible (menuKey);
        menuKey.setTooltip ("Settings: quality, mix lock, window size, preset folder.");
        menuKey.onClick = [this] { showSettingsMenu(); };

        // ---- engine ------------------------------------------------------------
        addAndMakeVisible (modeScreen);
        modeScreen.buildMenu = [] (juce::PopupMenu& menu, int current)
        {
            int family = -1;
            for (int m = 0; m < kNumModes; ++m)
            {
                if ((int) modeInfo (m).family != family)
                {
                    family = (int) modeInfo (m).family;
                    menu.addSectionHeader (kFamilyNames[family]);
                }
                menu.addItem (m + 1, modeInfo (m).name, true, m == current);
            }
        };
        modeScreen.onUserChange = [this] (int newMode) { applyModeConveniences (newMode); };

        makeKnob (timeKnob, id::time, "Time", Size::large);
        makeKnob (divisionKnob, id::division, "Time", Size::large);
        makeKnob (feedbackKnob, id::feedback, "Feedback", Size::medium);
        makeKnob (mixKnob, id::mix, "Mix", Size::medium, Knob::Body::dark);

        addAndMakeVisible (timeReadout);
        addAndMakeVisible (syncSwitch);
        syncSwitch.setTooltip (tooltipFor (id::sync));
        makeKnob (inertiaKnob, id::inertia, "Inertia", Size::compact);

        makeKnob (macroA, id::algoA, "A", Size::compact);
        makeKnob (macroB, id::algoB, "B", Size::compact);
        makeKnob (pingPongKnob, id::pingPong, "Ping-Pong", Size::compact);
        makeKnob (widthKnob, id::width, "Width", Size::compact);

        // ---- character ---------------------------------------------------------
        makeKnob (saturationKnob, id::saturation, "Saturation", Size::medium);
        makeKnob (driveKnob, id::drive, "Drive", Size::medium);
        addAndMakeVisible (satTypeDisplay);
        satTypeDisplay.setCaption ("Type", Size::medium);
        satTypeDisplay.setTooltip (tooltipFor (id::satType));

        addAndMakeVisible (filterScope);
        makeKnob (lowCutKnob, id::lowCut, "Low Cut", Size::medium);
        makeKnob (highCutKnob, id::highCut, "High Cut", Size::medium);
        makeKnob (resonanceKnob, id::resonance, "Resonance", Size::medium);

        makeKnob (diffusionKnob, id::diffusion, "Diffusion", Size::compact);
        makeKnob (modDepthKnob, id::modDepth, "Mod Depth", Size::compact);
        makeKnob (modRateKnob, id::modRate, "Mod Rate", Size::compact);
        makeKnob (duckingKnob, id::ducking, "Ducking", Size::compact);

        // ---- under the display: hold keys and the output -------------------------
        addAndMakeVisible (freezeKey);
        freezeKey.attach (apvts, id::freeze);
        freezeKey.setOnFill (OnFill::ice);
        freezeKey.setFontSize (13.0f);
        freezeKey.setTooltip (tooltipFor (id::freeze));

        addAndMakeVisible (infiniteKey);
        infiniteKey.attach (apvts, id::infinite);
        infiniteKey.setOnFill (OnFill::accent);
        infiniteKey.setFontSize (13.0f);
        infiniteKey.setTooltip (tooltipFor (id::infinite));

        addAndMakeVisible (meterL);
        addAndMakeVisible (meterR);
        makeKnob (outGainKnob, id::outGain, "Output", Size::compact);

        addAndMakeVisible (limiterKey);
        limiterKey.attach (apvts, id::limiter);
        limiterKey.setDot (true);
        limiterKey.setTooltip (tooltipFor (id::limiter));

        // ---- lower card: tabs and the dials of the TIME and SPACE pages ------------
        addChildComponent (dockTabs);
        dockTabs.setTooltip ("TIME: warp, collapse, pitch and reverse.  SPACE: orbit, ghosts and the stereo field.  MOTION: six lanes of modulation.");
        dockTabs.onChange = [this] (int index) { setPage (index); };

        for (size_t i = 0; i < timeKnobs.size(); ++i)
        {
            makeKnob (timeKnobs[i], kTimeCells[i].paramId, kTimeCells[i].legend, Size::large);
            timeKnobs[i]->setVisible (false);
        }

        if (auto* pitch = timeKnobs[4].get())
            pitch->slider().setMouseDragSensitivity (260);

        makeKnob (orbitKnob, id::orbit, "Echo Orbit", Size::large);
        makeKnob (ghostKnob, id::ghost, "Ghost Mode", Size::large);
        makeKnob (offsetKnob, id::offset, "Offset", Size::large);
        makeKnob (spreadKnob, id::spread, "Spread", Size::large);
        makeKnob (orbitRateKnob, id::orbitRate, "Rate", Size::compact);

        addChildComponent (orbitShapeDisplay);
        orbitShapeDisplay.setCaption ("Orbit Shape", Size::compact);
        orbitShapeDisplay.setTooltip (tooltipFor (id::orbitShape));

        addChildComponent (orbitDivDisplay);
        orbitDivDisplay.setCaption ("Length", Size::compact);
        orbitDivDisplay.setFontSize (12.5f);
        orbitDivDisplay.setTooltip (tooltipFor (id::orbitDiv));

        addChildComponent (orbitSyncSwitch);
        orbitSyncSwitch.setTooltip (tooltipFor (id::orbitSync));

        addChildComponent (stereoSwitch);
        stereoSwitch.setTooltip (tooltipFor (id::stereoMode));

        addChildComponent (qualityDisplay);
        qualityDisplay.setCaption ("Quality", Size::compact);
        qualityDisplay.setTooltip (tooltipFor (id::quality));

        for (auto* k : { orbitKnob.get(), ghostKnob.get(), offsetKnob.get(), spreadKnob.get(), orbitRateKnob.get() })
            k->setVisible (false);

        // ---- centre + overlays -----------------------------------------------------------
        addAndMakeVisible (visualizer);
        addChildComponent (motion);
        dockTabs.toFront (false);                   // the tabs sit in the MOTION page's top row

        addChildComponent (browser);
        browser.onClose = [this] { showBrowser (false, false); };

        setOpaque (true);

        pMode      = apvts.getRawParameterValue (id::mode);
        pSync      = apvts.getRawParameterValue (id::sync);
        pOrbitSync = apvts.getRawParameterValue (id::orbitSync);
        pResonance = apvts.getRawParameterValue (id::resonance);

        visualizer.setComponentID ("visualizer");
        motion.setComponentID ("motion");
        browser.setComponentID ("browser");
        modeScreen.setComponentID ("modeScreen");
        dockTabs.setComponentID ("pages");

        page = juce::jlimit (0, (int) numPages - 1, proc.getUiPage());
        dockTabs.setIndex (page, false);

        refreshDynamicState (true);
    }

    // ------------------------------------------------------------------------
    void setAdvanced (bool advanced)
    {
        isAdvanced = advanced;
        viewKey.setToggleState (advanced, juce::dontSendNotification);
        updatePageVisibility();
        refreshDynamicState (true);
        repaint();
    }

    void setPage (int newPage)
    {
        page = juce::jlimit (0, (int) numPages - 1, newPage);
        proc.setUiPage (page);
        dockTabs.setIndex (page, false);
        updatePageVisibility();
        refreshDynamicState (true);
        repaint();
    }

    void showLane (int laneIndex)
    {
        setPage (pageMotion);
        motion.setLane (laneIndex);
    }

    void showBrowser (bool show, bool focusSave)
    {
        browser.setVisible (show);
        presetDisplay.open = show;
        presetDisplay.repaint();

        if (show)
        {
            browser.toFront (false);
            browser.prepareToShow (focusSave);
        }
    }

    // ------------------------------------------------------------------------
    void tick (float dt)
    {
        auto& vis = proc.engine.visual;

        visualizer.tick (dt);

        if (isAdvanced && page == pageMotion)
            motion.tick (dt);

        meterL.push (vis.outPeakL.exchange (0.0f, std::memory_order_relaxed), dt);
        meterR.push (vis.outPeakR.exchange (0.0f, std::memory_order_relaxed), dt);

        filterScope.set (vis.hpHz.load (std::memory_order_relaxed), vis.lpHz.load (std::memory_order_relaxed),
                         pResonance->load() * 0.01f);

        // the limiter's light glows while it is pulling the level down
        const float gr = vis.limiterGr.exchange (1.0f, std::memory_order_relaxed);
        limiterKey.setLit (limiterKey.getToggleState() ? juce::jlimit (0.0f, 1.0f, (1.0f - gr) * 5.0f) : 0.0f);

        timeReadout.refresh();
        refreshDynamicState (false);
    }

    // ------------------------------------------------------------------------
    void paint (juce::Graphics& g) override
    {
        // The page, the cards and every fixed legend never change, so they are rendered once
        // per size at the display's real pixel density. Touching a dial then costs one blit
        // instead of redrawing the faceplate (the cards' soft shadows are the expensive part).
        const float scale = juce::jlimit (0.25f, 8.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
        const int w = juce::jmax (1, (int) std::ceil ((float) getWidth() * scale));
        const int h = juce::jmax (1, (int) std::ceil ((float) getHeight() * scale));

        if (! faceplate.isValid() || faceplate.getWidth() != w || faceplate.getHeight() != h
             || std::abs (scale - faceplateScale) > 1.0e-4f || faceplateAdvanced != isAdvanced || faceplatePage != page)
        {
            faceplateScale = scale;
            faceplateAdvanced = isAdvanced;
            faceplatePage = page;
            faceplate = juce::Image (juce::Image::RGB, w, h, true);

            juce::Graphics layer (faceplate);
            layer.addTransform (juce::AffineTransform::scale (scale));
            paintFaceplate (layer);
        }

        juce::Graphics::ScopedSaveState state (g);
        g.addTransform (juce::AffineTransform::scale (1.0f / faceplateScale));
        g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
        g.drawImageAt (faceplate, 0, 0);
    }

    void paintFaceplate (juce::Graphics& g)
    {
        const auto full = getLocalBounds().toFloat();

        drawPage (g, full);

        // ---- logo: the name, and its full stop echoing away
        {
            const auto logoFont = font (FontKind::heavy, 31.0f, -0.015f);
            const float x0 = 24.0f, wordW = textWidth (logoFont, "DLAY");

            g.setFont (logoFont);
            g.setColour (col::ink);
            g.drawText ("DLAY", juce::Rectangle<float> (x0, 12.0f, wordW + 6.0f, 44.0f), juce::Justification::centredLeft, false);

            static constexpr float radii[]  = { 4.4f, 3.3f, 2.3f };
            static constexpr float alphas[] = { 1.0f, 0.62f, 0.30f };
            const juce::Colour tints[] = { col::pink, col::magenta, col::purple };
            const float baseline = 34.0f + 0.355f * 31.0f;
            float x = x0 + wordW + 8.0f;

            for (int i = 0; i < 3; ++i)
            {
                const float r = radii[i];
                g.setColour (tints[i].withAlpha (alphas[i]));
                g.fillEllipse (x, baseline - 2.0f * r, 2.0f * r, 2.0f * r);
                x += 2.0f * r + 4.5f;
            }
        }

        g.setFont (capsFont (10.0f));
        g.setColour (col::inkFaint);
        g.drawText (juce::String ("V") + DLAY_VERSION_STRING, juce::Rectangle<float> (940.0f, 26.0f, 106.0f, 16.0f), juce::Justification::centredRight, false);

        // ---- cards
        drawCard (g, leftCard.toFloat());
        drawCard (g, rightCard.toFloat());
        drawCard (g, screenCard.toFloat());
        drawCard (g, stripCard.toFloat(), 16.0f);

        drawSectionHeader (g, headerIn (leftCard), "01", "ENGINE");
        drawSectionHeader (g, headerIn (rightCard), "02", "CHARACTER");

        // the hold keys on the left of the strip, the output on the right
        g.setColour (col::line);
        g.fillRect ((float) meterL.getX() - 14.0f, (float) stripCard.getY() + 14.0f, 1.0f, (float) stripCard.getHeight() - 28.0f);

        if (! isAdvanced)
            return;

        // ---- lower card
        drawCard (g, dockCard.toFloat());

        if (page == pageMotion)
            return;

        g.setFont (font (FontKind::medium, 12.5f, 0.0f));
        g.setColour (col::inkFaint);
        g.drawText (page == pageTime ? kTimeHint : kSpaceHint,
                    juce::Rectangle<float> ((float) dockTabs.getRight() + 24.0f, (float) dockTabs.getY(),
                                            (float) dockCard.getRight() - 18.0f - (float) dockTabs.getRight() - 24.0f, (float) dockTabs.getHeight()),
                    juce::Justification::centredRight, true);

        const auto describe = [&] (int cell, const char* line)
        {
            const auto r = cellBounds (cell);
            g.setFont (font (FontKind::medium, 12.5f, 0.0f));
            g.setColour (col::inkDim);
            g.drawFittedText (line, juce::Rectangle<int> (r.getX() + 10, r.getY() + 116, r.getWidth() - 20, 34),
                              juce::Justification::centredTop, 2, 1.0f);
        };

        if (page == pageTime)
        {
            for (int i = 0; i < (int) std::size (kTimeCells); ++i)
                describe (i, kTimeCells[i].line);
        }
        else
        {
            describe (0, "Sends the echoes travelling around you");
            describe (2, "Faint apparitions of earlier repeats");
            describe (3, "Makes one side later than the other");
            describe (4, "Fans taps, grains and bands out wide");

            // hairlines set the orbit's own settings and the stereo options apart
            g.setColour (col::line);
            const auto orbit = cellBounds (1), stereo = cellBounds (5);
            g.fillRect ((float) orbit.getRight() + 2.0f, (float) orbit.getY() + 8.0f, 1.0f, (float) orbit.getHeight() - 20.0f);
            g.fillRect ((float) stereo.getX() - 3.0f, (float) stereo.getY() + 8.0f, 1.0f, (float) stereo.getHeight() - 20.0f);

            g.setFont (Knob::legendFontFor (Knob::Size::compact));
            g.setColour (col::ink);
            g.drawText ("Stereo Mode", juce::Rectangle<int> (stereoSwitch.getX(), stereoSwitch.getBottom() + 2, stereoSwitch.getWidth(), 20),
                        juce::Justification::centred, false);
        }
    }

    void resized() override
    {
        constexpr int W = kBaseWidth;
        faceplate = {};

        // ---- top bar
        bypassKey.setBounds (176, 13, 42, 42);
        prevKey.setBounds (232, 13, 42, 42);
        presetDisplay.setBounds (278, 11, 372, 46);
        nextKey.setBounds (654, 13, 42, 42);
        favKey.setBounds (702, 13, 42, 42);
        saveKey.setBounds (752, 15, 76, 38);
        undoKey.setBounds (846, 13, 42, 42);
        redoKey.setBounds (890, 13, 42, 42);
        viewKey.setBounds (1058, 15, 116, 38);
        menuKey.setBounds (1182, 15, 76, 38);

        // ---- cards
        leftCard   = { 12, 68, 320, 400 };
        rightCard  = { W - 12 - 320, 68, 320, 400 };
        screenCard = { leftCard.getRight() + 10, 68, rightCard.getX() - leftCard.getRight() - 20, 324 };
        stripCard  = { screenCard.getX(), screenCard.getBottom() + 10, screenCard.getWidth(), 66 };
        dockCard   = { 12, 478, W - 24, 230 };

        visualizer.setBounds (screenCard.reduced (5));
        motion.setBounds (dockCard);
        browser.setBounds (0, 64, W, getHeight() - 64);

        // ---- engine
        {
            const int x = leftCard.getX() + 16, w = leftCard.getWidth() - 32, y = leftCard.getY();

            modeScreen.setBounds (x, y + 34, w, 86);

            timeKnob->setBounds (x, y + 128, 104, 112);
            divisionKnob->setBounds (timeKnob->getBounds());
            feedbackKnob->setBounds (x + 110, y + 146, 84, 94);
            mixKnob->setBounds (x + 204, y + 146, 84, 94);

            timeReadout.setBounds (x, y + 252, 150, 54);
            syncSwitch.setBounds (x + 158, y + 246, 56, 68);
            inertiaKnob->setBounds (x + 218, y + 246, 70, 68);

            const int quarter = w / 4;
            int i = 0;
            for (auto* k : { macroA.get(), macroB.get(), pingPongKnob.get(), widthKnob.get() })
                k->setBounds (x + quarter * i++, y + 320, quarter, 66);
        }

        // ---- character
        {
            const int x = rightCard.getX() + 16, w = rightCard.getWidth() - 32, y = rightCard.getY();
            const int third = w / 3;

            saturationKnob->setBounds (x + (third - 84) / 2, y + 36, 84, 94);
            driveKnob->setBounds (x + third + (third - 84) / 2, y + 36, 84, 94);
            satTypeDisplay.setBounds (x + 2 * third + 2, y + 36, third - 2, 94);

            filterScope.setBounds (x, y + 138, w, 58);

            int i = 0;
            for (auto* k : { lowCutKnob.get(), highCutKnob.get(), resonanceKnob.get() })
                k->setBounds (x + third * i++ + (third - 84) / 2, y + 204, 84, 94);

            const int quarter = w / 4;
            i = 0;
            for (auto* k : { diffusionKnob.get(), modDepthKnob.get(), modRateKnob.get(), duckingKnob.get() })
                k->setBounds (x + quarter * i++, y + 308, quarter, 66);
        }

        // ---- strip under the display
        {
            const int y = stripCard.getY(), right = stripCard.getRight() - 14;

            freezeKey.setBounds (stripCard.getX() + 14, y + 13, 104, 40);
            infiniteKey.setBounds (stripCard.getX() + 124, y + 13, 112, 40);

            limiterKey.setBounds (right - 84, y + 15, 84, 36);
            outGainKnob->setBounds (right - 84 - 8 - 68, y + 2, 68, 62);

            const int meterRight = outGainKnob->getX() - 10;
            const int meterX = infiniteKey.getRight() + 34;
            meterL.setBounds (meterX, y + 20, meterRight - meterX, 12);
            meterR.setBounds (meterX, y + 35, meterRight - meterX, 12);
        }

        // ---- lower card
        {
            dockTabs.setBounds (dockCard.getX() + 16, dockCard.getY() + 12, 300, 32);
            motion.setHeaderInset (16 + 300 + 26);

            // TIME: six dials
            for (int i = 0; i < (int) timeKnobs.size(); ++i)
                timeKnobs[(size_t) i]->setBounds (dialIn (cellBounds (i)));

            // SPACE: orbit and its settings, ghosts, the stereo field
            orbitKnob->setBounds (dialIn (cellBounds (0)));
            ghostKnob->setBounds (dialIn (cellBounds (2)));
            offsetKnob->setBounds (dialIn (cellBounds (3)));
            spreadKnob->setBounds (dialIn (cellBounds (4)));

            {
                const auto c = cellBounds (1);
                orbitShapeDisplay.setBounds (c.getX() + 14, c.getY() + 4, c.getWidth() - 28, 60);
                orbitRateKnob->setBounds (c.getX() + 26, c.getY() + 76, 68, 68);
                orbitDivDisplay.setBounds (c.getX() + 10, c.getY() + 76, 104, 68);
                orbitSyncSwitch.setBounds (c.getX() + 122, c.getY() + 76, 56, 68);
            }
            {
                const auto c = cellBounds (5);
                stereoSwitch.setBounds (c.getX() + 4, c.getY() + 10, c.getWidth() - 8, 32);
                qualityDisplay.setBounds (c.getX() + 24, c.getY() + 82, c.getWidth() - 48, 60);
            }
        }
    }

private:
    // ------------------------------------------------------------------------
    static juce::Rectangle<float> headerIn (juce::Rectangle<int> card)
    {
        return juce::Rectangle<float> ((float) card.getX() + 18.0f, (float) card.getY() + 12.0f, (float) card.getWidth() - 36.0f, 16.0f);
    }

    /** One of the six columns of the lower card's TIME and SPACE pages. */
    juce::Rectangle<int> cellBounds (int index) const
    {
        const int x0 = dockCard.getX() + 16, w = (dockCard.getWidth() - 32) / 6;
        return { x0 + w * index, dockCard.getY() + 52, w, dockCard.getHeight() - 52 - 12 };
    }

    static juce::Rectangle<int> dialIn (juce::Rectangle<int> cell)
    {
        return { cell.getCentreX() - 52, cell.getY() + 2, 104, 112 };
    }

    void makeKnob (std::unique_ptr<Knob>& knob, const char* paramId, const char* legend, Knob::Size size, Knob::Body body = Knob::Body::light)
    {
        knob = std::make_unique<Knob> (apvts, paramId, legend, size, body);
        knob->setComponentID (paramId);
        knob->setTooltip (tooltipFor (paramId));
        addAndMakeVisible (*knob);
    }

    void updatePageVisibility()
    {
        const bool onTime = isAdvanced && page == pageTime, onSpace = isAdvanced && page == pageSpace;
        const bool orbitSynced = pOrbitSync != nullptr && pOrbitSync->load() > 0.5f;

        dockTabs.setVisible (isAdvanced);

        for (auto& k : timeKnobs)
            k->setVisible (onTime);

        for (auto* c : std::initializer_list<juce::Component*> { orbitKnob.get(), ghostKnob.get(), offsetKnob.get(), spreadKnob.get(),
                                                                 &orbitShapeDisplay, &orbitSyncSwitch, &stereoSwitch, &qualityDisplay })
            c->setVisible (onSpace);

        orbitRateKnob->setVisible (onSpace && ! orbitSynced);
        orbitDivDisplay.setVisible (onSpace && orbitSynced);

        motion.setVisible (isAdvanced && page == pageMotion);
    }

    /** Previous / next: within the library's current search and category while it is open,
        through the whole library otherwise (a filter you can not see should not apply). */
    void stepPreset (int delta)
    {
        if (browser.isVisible())
        {
            browser.refresh();
            browser.step (delta);
        }
        else
        {
            proc.presets->loadRelative (delta);
        }
    }

    /** Small courtesies when the user (not automation) picks an algorithm. */
    void applyModeConveniences (int newMode)
    {
        const auto setIfDefault = [this] (const char* paramId, float value)
        {
            if (auto* p = apvts.getParameter (paramId))
            {
                if (std::abs (p->getValue() - p->getDefaultValue()) < 1.0e-6f)
                {
                    p->beginChangeGesture();
                    p->setValueNotifyingHost (p->convertTo0to1 (value));
                    p->endChangeGesture();
                }
            }
        };

        if (newMode == (int) Mode::shimmer || newMode == (int) Mode::pitch)
            setIfDefault (id::pitch, 12.0f);            // these two are about pitch: start an octave up
    }

    void showSettingsMenu()
    {
        juce::PopupMenu menu, quality, size;

        const int q = juce::roundToInt (apvts.getRawParameterValue (id::quality)->load());
        quality.addItem (101, "ECO  -  no oversampling", true, q == 0);
        quality.addItem (102, "HIGH  -  2x oversampling", true, q == 1);
        quality.addItem (103, "ULTRA  -  4x oversampling", true, q == 2);

        const double scale = proc.getUiScale();
        size.addItem (201, "80 %",  true, std::abs (scale - 0.8) < 0.01);
        size.addItem (202, "100 %", true, std::abs (scale - 1.0) < 0.01);
        size.addItem (203, "125 %", true, std::abs (scale - 1.25) < 0.01);
        size.addItem (204, "150 %", true, std::abs (scale - 1.5) < 0.01);

        menu.addSectionHeader (juce::String ("DLAY ") + DLAY_VERSION_STRING);
        menu.addSubMenu ("Saturation quality", quality);
        menu.addSubMenu ("Window size", size);
        menu.addItem (1, "Keep MIX when changing presets", true, proc.presets->isMixLocked());
        menu.addSeparator();
        menu.addItem (2, "Initialise all controls");
        menu.addItem (3, "Show preset folder");

        juce::Component::SafePointer<Content> safe (this);
        showPopupMenu (menu, menuKey, [safe] (int result)
        {
            if (safe == nullptr) return;
            auto& self = *safe;

            if (result >= 101 && result <= 103)
            {
                if (auto* p = self.apvts.getParameter (id::quality))
                {
                    p->beginChangeGesture();
                    p->setValueNotifyingHost (p->convertTo0to1 ((float) (result - 101)));
                    p->endChangeGesture();
                }
            }
            else if (result >= 201 && result <= 204)
            {
                static constexpr double scales[] = { 0.8, 1.0, 1.25, 1.5 };
                self.editor.setWindowScale (scales[result - 201]);
            }
            else if (result == 1)   self.proc.presets->setMixLocked (! self.proc.presets->isMixLocked());
            else if (result == 2)   self.proc.presets->loadInit();
            else if (result == 3)
            {
                const auto dir = self.proc.presets->getUserPresetDirectory();
                dir.createDirectory();
                dir.revealToUser();
            }
        });
    }

    /** Things that follow parameter values rather than user clicks (so automation, undo and
        preset changes are reflected too). Polled at the frame rate; only repaints on change. */
    void refreshDynamicState (bool force)
    {
        const int mode = juce::roundToInt (pMode->load());

        if (force || mode != shownMode)
        {
            shownMode = mode;
            const auto& info = modeInfo (mode);

            macroA->setLegend (titleCase (info.macroA));
            macroA->setTooltip (juce::String (info.name) + " - " + info.macroA + ": " + info.tipA);
            macroB->setLegend (titleCase (info.macroB));
            macroB->setTooltip (juce::String (info.name) + " - " + info.macroB + ": " + info.tipB);
            modeScreen.setTooltip (juce::String (info.name) + ": " + info.description + "\n" + tooltipFor (id::mode));
        }

        const bool synced = pSync->load() > 0.5f;

        if (force || synced != shownSync)
        {
            shownSync = synced;
            timeKnob->setVisible (! synced);
            divisionKnob->setVisible (synced);
        }

        const bool orbitSynced = pOrbitSync->load() > 0.5f;

        if (force || orbitSynced != shownOrbitSync)
        {
            shownOrbitSync = orbitSynced;
            const bool onSpace = isAdvanced && page == pageSpace;
            orbitRateKnob->setVisible (onSpace && ! orbitSynced);
            orbitDivDisplay.setVisible (onSpace && orbitSynced);
        }

        const auto name = proc.presets->getCurrentName();
        const auto category = proc.presets->getCurrentCategory();
        const bool modified = proc.presets->isModified();

        if (force || name != presetDisplay.name || category != presetDisplay.category || modified != presetDisplay.modified)
        {
            presetDisplay.name = name;
            presetDisplay.category = category;
            presetDisplay.modified = modified;
            presetDisplay.repaint();
            favKey.repaint();
        }

        const bool canUndo = proc.undoManager.canUndo(), canRedo = proc.undoManager.canRedo();
        if (undoKey.isEnabled() != canUndo) undoKey.setEnabled (canUndo);
        if (redoKey.isEnabled() != canRedo) redoKey.setEnabled (canRedo);
    }

    // ------------------------------------------------------------------------
    DlayAudioProcessorEditor& editor;
    DlayAudioProcessor& proc;
    APVTS& apvts;

    // top bar
    KeyButton bypassKey { "BYPASS" }, prevKey { "<" }, nextKey { ">" }, favKey { "FAV" }, saveKey { "SAVE" },
              undoKey { "UNDO" }, redoKey { "REDO" }, viewKey { "ADVANCED" }, menuKey { "MENU" };
    PresetDisplay presetDisplay;

    // engine
    ModeScreen modeScreen { apvts };
    std::unique_ptr<Knob> timeKnob, divisionKnob, inertiaKnob, feedbackKnob, mixKnob, macroA, macroB, pingPongKnob, widthKnob;
    ToggleSwitch syncSwitch { apvts, id::sync, "Sync" };

    // character
    std::unique_ptr<Knob> saturationKnob, driveKnob, lowCutKnob, highCutKnob, resonanceKnob,
                          diffusionKnob, modDepthKnob, modRateKnob, duckingKnob;
    ChoiceDisplay satTypeDisplay { apvts, id::satType };
    FilterScope filterScope;

    // under the display
    std::unique_ptr<Knob> outGainKnob;
    KeyButton freezeKey { "FREEZE" }, infiniteKey { "INFINITE" }, limiterKey { "LIMIT" };
    LevelMeter meterL { "L" }, meterR { "R" };

    // lower card
    SegmentBar dockTabs { { "TIME", "SPACE", "MOTION" } };
    std::array<std::unique_ptr<Knob>, std::size (kTimeCells)> timeKnobs;
    std::unique_ptr<Knob> orbitKnob, ghostKnob, offsetKnob, spreadKnob, orbitRateKnob;
    ChoiceDisplay orbitShapeDisplay { apvts, id::orbitShape }, orbitDivDisplay { apvts, id::orbitDiv }, qualityDisplay { apvts, id::quality };
    ToggleSwitch orbitSyncSwitch { apvts, id::orbitSync, "Sync" };
    SegmentSwitch stereoSwitch { apvts, id::stereoMode, { "Stereo", "Mono", "Mid/Side" } };

    // centre and overlays
    EchoVisualizer visualizer;
    MotionEditor motion;
    PresetBrowser browser;
    TimeReadout timeReadout;

    std::atomic<float>* pMode = nullptr, *pSync = nullptr, *pOrbitSync = nullptr, *pResonance = nullptr;

    juce::Image faceplate;
    float faceplateScale = 0.0f;
    bool faceplateAdvanced = true;
    int faceplatePage = -1;

    juce::Rectangle<int> leftCard, rightCard, screenCard, stripCard, dockCard;
    int  shownMode = -1, page = 0;
    bool shownSync = false, shownOrbitSync = false, isAdvanced = true;
};

// ============================================================================
//  Editor
// ============================================================================
DlayAudioProcessorEditor::DlayAudioProcessorEditor (DlayAudioProcessor& p)
    : juce::AudioProcessorEditor (p), dlayProcessor (p)
{
    setLookAndFeel (&lookAndFeel);
    setOpaque (true);

    advancedView = dlayProcessor.getUiAdvanced();

    content = std::make_unique<Content> (*this, dlayProcessor);
    addAndMakeVisible (*content);
    content->setAdvanced (advancedView);

    tooltips = std::make_unique<juce::TooltipWindow> (this, 650);
    tooltips->setLookAndFeel (&lookAndFeel);

    setResizable (true, true);
    applyConstraints();

    const double scale = openingScale();
    setSize (juce::roundToInt (kBaseWidth * scale), juce::roundToInt (baseHeight() * scale));

    lastTick = juce::Time::getMillisecondCounter();
    startTimerHz (30);
}

DlayAudioProcessorEditor::~DlayAudioProcessorEditor()
{
    stopTimer();
    tooltips.reset();
    content.reset();
    setLookAndFeel (nullptr);
}

/** The size to open at: the one stored with the session, or - the first time - one chosen
    for the screen; in both cases never larger than the screen allows. */
double DlayAudioProcessorEditor::openingScale() const
{
    double scale = dlayProcessor.getUiScale();
    double fit = kMaxScale;

    if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
    {
        const auto area = display->userBounds;

        if (area.getWidth() > 0.0f && area.getHeight() > 0.0f)
        {
            // a host adds a title bar and often a toolbar of its own around the plug-in
            fit = juce::jmin ((double) area.getWidth() * 0.96 / (double) kBaseWidth,
                              (double) area.getHeight() * 0.93 / (double) baseHeight());

            if (scale <= 0.0)       // never chosen: full size, and larger on a large screen
                scale = juce::jlimit (1.0, 1.5, (double) area.getHeight() * 0.62 / (double) kAdvancedHeight);
        }
    }

    if (scale <= 0.0)
        scale = 1.0;

    return juce::jlimit (kMinScale, kMaxScale, juce::jmin (scale, fit));
}

void DlayAudioProcessorEditor::applyConstraints()
{
    const double ratio = (double) kBaseWidth / (double) baseHeight();

    if (auto* limits = getConstrainer())
    {
        limits->setFixedAspectRatio (ratio);
        limits->setSizeLimits (juce::roundToInt (kBaseWidth * kMinScale), juce::roundToInt (baseHeight() * kMinScale),
                               juce::roundToInt (kBaseWidth * kMaxScale), juce::roundToInt (baseHeight() * kMaxScale));
    }
}

void DlayAudioProcessorEditor::setAdvancedView (bool advanced)
{
    if (advanced == advancedView)
        return;

    const double scale = (double) getWidth() / (double) kBaseWidth;

    advancedView = advanced;
    dlayProcessor.setUiAdvanced (advanced);
    content->setAdvanced (advanced);

    applyConstraints();
    setSize (juce::roundToInt (kBaseWidth * scale), juce::roundToInt (baseHeight() * scale));
    resized();
}

void DlayAudioProcessorEditor::setWindowScale (double scale)
{
    scale = juce::jlimit (kMinScale, kMaxScale, scale);
    setSize (juce::roundToInt (kBaseWidth * scale), juce::roundToInt (baseHeight() * scale));
}

void DlayAudioProcessorEditor::showPresetBrowser (bool shouldShow)
{
    content->showBrowser (shouldShow, false);
}

void DlayAudioProcessorEditor::showPage (int pageIndex)
{
    content->setPage (pageIndex);
}

void DlayAudioProcessorEditor::showMotionLane (int laneIndex)
{
    content->showLane (laneIndex);
}

void DlayAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (dlay::ui::col::page);
}

void DlayAudioProcessorEditor::resized()
{
    if (content == nullptr)
        return;

    const float scale = (float) getWidth() / (float) kBaseWidth;

    lookAndFeel.uiScale = scale;
    content->setTransform (juce::AffineTransform::scale (scale));
    content->setBounds (0, 0, kBaseWidth, baseHeight());

    dlayProcessor.setUiScale ((double) scale);
}

void DlayAudioProcessorEditor::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounter();
    const float dt = juce::jlimit (0.005f, 0.2f, (float) (now - lastTick) * 0.001f);
    lastTick = now;

    if (content != nullptr)
        content->tick (dt);
}
