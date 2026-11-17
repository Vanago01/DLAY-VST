#include "PluginEditor.h"
#include "UI/Widgets.h"
#include "UI/EchoVisualizer.h"
#include "UI/MotionEditor.h"
#include "UI/PresetBrowser.h"

using namespace dlay;
using namespace dlay::ui;

namespace
{
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
            { id::limiter,    "Output safety limiter. Leave it on unless you have a reason not to." },
            { id::bypass,     "Bypass. The echoes fade out smoothly instead of clicking off." },
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

    // ------------------------------------------------------------------------
    //  glyphs for the top-bar keys
    // ------------------------------------------------------------------------
    void iconArrow (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, bool pointsRight)
    {
        const auto ctr = r.getCentre();
        const float s = 5.0f, d = pointsRight ? 1.0f : -1.0f;
        juce::Path p;
        p.addTriangle (ctr.x - d * s * 0.7f, ctr.y - s, ctr.x - d * s * 0.7f, ctr.y + s, ctr.x + d * s * 0.9f, ctr.y);
        g.setColour (c);
        g.fillPath (p);
    }

    void iconStar (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, bool filled)
    {
        juce::Path star;
        star.addStar (r.getCentre(), 5, 3.4f, 7.5f, 0.0f);
        g.setColour (c);
        if (filled) g.fillPath (star); else g.strokePath (star, juce::PathStrokeType (1.2f));
    }

    void iconCurvedArrow (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, bool redo)
    {
        const auto ctr = r.getCentre();
        const float rad = 5.5f, d = redo ? 1.0f : -1.0f;
        juce::Path arc;
        arc.addCentredArc (ctr.x, ctr.y + 1.0f, rad, rad, 0.0f, d * -2.2f, d * 1.2f, true);
        g.setColour (c);
        g.strokePath (arc, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const juce::Point<float> tip (ctr.x + d * rad * std::sin (1.2f), ctr.y + 1.0f - rad * std::cos (1.2f));
        juce::Path head;
        head.addTriangle (tip.x + d * 1.0f, tip.y + 4.6f, tip.x - d * 4.2f, tip.y + 0.6f, tip.x + d * 3.2f, tip.y - 2.2f);
        g.fillPath (head);
    }

    // ------------------------------------------------------------------------
    /** Preset name on a read-out; click to open the library. */
    class PresetDisplay : public juce::Component,
                          public juce::SettableTooltipClient
    {
    public:
        std::function<void()> onClick;
        juce::String name, category;
        bool modified = false, open = false;

        void paint (juce::Graphics& g) override
        {
            const auto b = getLocalBounds().toFloat();
            drawScreen (g, b.reduced (1.0f), 4.0f);

            auto text = b.reduced (12.0f, 2.0f);

            g.setFont (font (FontKind::semiBold, 8.5f, 0.2f));
            g.setColour (col::orange);
            g.drawText (category.isEmpty() ? juce::String ("PRESET") : category.toUpperCase(), text.removeFromTop (12.0f).translated (0.0f, 2.0f),
                        juce::Justification::centredLeft, true);

            drawGlowText (g, name + (modified ? " *" : ""), text.translated (0.0f, -1.0f), juce::Justification::centredLeft,
                          font (FontKind::semiBold, 18.0f, 0.06f), col::cream);

            // chevron: the library opens below
            const auto ctr = juce::Point<float> (b.getRight() - 15.0f, b.getCentreY());
            juce::Path chevron;
            const float flip = open ? -1.0f : 1.0f;
            chevron.startNewSubPath (ctr.x - 4.5f, ctr.y - 2.2f * flip);
            chevron.lineTo (ctr.x, ctr.y + 2.4f * flip);
            chevron.lineTo (ctr.x + 4.5f, ctr.y - 2.2f * flip);
            g.setColour (isMouseOver() ? col::amber : col::creamDim);
            g.strokePath (chevron, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
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
            const auto b = getLocalBounds().toFloat();
            drawScreen (g, b.reduced (1.0f), 3.0f);

            auto text = b.reduced (8.0f, 1.0f);
            const auto caption = text.removeFromBottom (12.0f).translated (0.0f, -2.0f);

            drawGlowText (g, synced ? juce::String (kDivisions[(size_t) juce::jlimit (0, kNumDivisions - 1, divisionIndex)].name) : formatMs (shownMs),
                          text.translated (0.0f, 1.5f), juce::Justification::centred, monoFont (17.0f), col::amber);

            g.setFont (monoFont (8.5f));
            g.setColour (col::creamFaint);
            g.drawText (synced ? "= " + formatMs (shownMs) : juce::String ("FREE RUNNING"), caption, juce::Justification::centred, false);
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

        // ---- top bar ---------------------------------------------------------
        addKey (bypassKey, "BYPASS", col::red);
        bypassKey.attach (apvts, id::bypass);
        bypassKey.setTooltip (tooltipFor (id::bypass));

        addKey (prevKey, "<");
        prevKey.setShowLed (false);
        prevKey.drawIcon = [] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) { iconArrow (g, r, c, false); };
        prevKey.setTooltip ("Previous preset");
        prevKey.onClick = [this] { stepPreset (-1); };

        addKey (nextKey, ">");
        nextKey.setShowLed (false);
        nextKey.drawIcon = [] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) { iconArrow (g, r, c, true); };
        nextKey.setTooltip ("Next preset");
        nextKey.onClick = [this] { stepPreset (1); };

        addAndMakeVisible (presetDisplay);
        presetDisplay.setTooltip ("Open the preset library: search, categories, favourites, save.");
        presetDisplay.onClick = [this] { showBrowser (! browser.isVisible(), false); };

        addKey (favKey, "FAV");
        favKey.setShowLed (false);
        favKey.drawIcon = [this] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
        {
            const bool fav = proc.presets->isFavourite (proc.presets->getCurrentIndex());
            iconStar (g, r, fav ? col::amber : c, fav);
        };
        favKey.setTooltip ("Mark this preset as a favourite");
        favKey.onClick = [this]
        {
            const int index = proc.presets->getCurrentIndex();
            if (index >= 0)
                proc.presets->setFavourite (index, ! proc.presets->isFavourite (index));
            favKey.repaint();
        };

        addKey (saveKey, "SAVE");
        saveKey.setShowLed (false);
        saveKey.setTooltip ("Save the current sound as a preset");
        saveKey.onClick = [this] { showBrowser (true, true); };

        addKey (undoKey, "UNDO");
        undoKey.setShowLed (false);
        undoKey.drawIcon = [] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) { iconCurvedArrow (g, r, c, false); };
        undoKey.setTooltip ("Undo");
        undoKey.onClick = [this] { proc.undoManager.undo(); };

        addKey (redoKey, "REDO");
        redoKey.setShowLed (false);
        redoKey.drawIcon = [] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) { iconCurvedArrow (g, r, c, true); };
        redoKey.setTooltip ("Redo");
        redoKey.onClick = [this] { proc.undoManager.redo(); };

        addKey (viewKey, "ADVANCED", col::cyan);
        viewKey.setTooltip ("Switch between the compact view and the full view with DETAIL and MOTION.");
        viewKey.onClick = [this] { editor.setAdvancedView (! editor.isAdvancedView()); };

        addKey (menuKey, "MENU");
        menuKey.setShowLed (false);
        menuKey.setTooltip ("Settings: quality, mix lock, window size, preset folder.");
        menuKey.onClick = [this] { showSettingsMenu(); };

        // ---- delay engine ----------------------------------------------------
        static const char* familyNames[] = { "CLASSIC", "VINTAGE", "CREATIVE" };
        static const char* familyTips[]  = { "Classic delays: digital, stereo, ping-pong, dual, multi-tap, slapback.",
                                             "Vintage delays: tape, bucket-brigade, tape echo, warm analog, degraded, lo-fi.",
                                             "Creative delays: reverse, granular, pitch, spectral, diffused, glitch, fractal, random, shimmer." };

        for (int f = 0; f < 3; ++f)
        {
            auto& key = familyKeys[(size_t) f];
            key = std::make_unique<KeyButton> (familyNames[f]);
            key->setFontHeight (10.5f);
            key->setTooltip (familyTips[f]);
            key->onClick = [this, f]
            {
                const int mode = juce::roundToInt (apvts.getRawParameterValue (id::mode)->load());
                if ((int) modeInfo (mode).family != f)
                {
                    for (int m = 0; m < kNumModes; ++m)
                        if ((int) modeInfo (m).family == f) { modeDisplay.setIndex (m); break; }
                }
            };
            addAndMakeVisible (*key);
        }

        addAndMakeVisible (modeDisplay);
        modeDisplay.setFontHeight (18.0f);
        modeDisplay.buildMenu = [] (juce::PopupMenu& menu, int current)
        {
            int family = -1;
            for (int m = 0; m < kNumModes; ++m)
            {
                if ((int) modeInfo (m).family != family)
                {
                    family = (int) modeInfo (m).family;
                    menu.addSectionHeader (familyNames[family]);
                }
                menu.addItem (m + 1, modeInfo (m).name, true, m == current);
            }
        };
        modeDisplay.onUserChange = [this] (int newMode) { applyModeConveniences (newMode); };

        makeKnob (timeKnob, id::time, "TIME", Size::large);
        makeKnob (divisionKnob, id::division, "TIME", Size::large);
        addAndMakeVisible (timeReadout);
        addAndMakeVisible (syncSwitch);
        syncSwitch.setTooltip (tooltipFor (id::sync));
        makeKnob (inertiaKnob, id::inertia, "INERTIA", Size::compact, col::cream);

        makeKnob (feedbackKnob, id::feedback, "FEEDBACK", Size::medium);
        makeKnob (mixKnob, id::mix, "MIX", Size::medium);
        makeKnob (macroA, id::algoA, "A", Size::compact, col::orange);
        makeKnob (macroB, id::algoB, "B", Size::compact, col::orange);
        makeKnob (pingPongKnob, id::pingPong, "PING-PONG", Size::compact, col::cyan);
        makeKnob (widthKnob, id::width, "WIDTH", Size::compact, col::cyan);

        addAndMakeVisible (stereoSwitch);
        stereoSwitch.setTooltip (tooltipFor (id::stereoMode));

        // ---- character ---------------------------------------------------------
        makeKnob (saturationKnob, id::saturation, "SATURATION", Size::medium, col::orange);
        makeKnob (driveKnob, id::drive, "DRIVE", Size::medium, col::orange);
        addAndMakeVisible (satTypeDisplay);
        satTypeDisplay.setCaption ("TYPE");
        satTypeDisplay.setFontHeight (13.0f);
        satTypeDisplay.setTooltip (tooltipFor (id::satType));

        addAndMakeVisible (filterScope);
        makeKnob (lowCutKnob, id::lowCut, "LOW CUT", Size::medium);
        makeKnob (highCutKnob, id::highCut, "HIGH CUT", Size::medium);
        makeKnob (resonanceKnob, id::resonance, "RESONANCE", Size::compact);

        makeKnob (diffusionKnob, id::diffusion, "DIFFUSION", Size::medium);
        makeKnob (modDepthKnob, id::modDepth, "MODULATION", Size::medium);
        makeKnob (pitchKnob, id::pitch, "PITCH", Size::medium, col::cyan);
        pitchKnob->slider().setMouseDragSensitivity (260);

        makeKnob (modRateKnob, id::modRate, "MOD RATE", Size::compact);
        makeKnob (reverseKnob, id::reverse, "REVERSE", Size::compact, col::cyan);
        makeKnob (duckingKnob, id::ducking, "DUCKING", Size::compact);

        // ---- signature -----------------------------------------------------------
        makeKnob (warpKnob, id::warp, "TIME WARP", Size::medium);
        makeKnob (orbitKnob, id::orbit, "ECHO ORBIT", Size::medium, col::cyan);
        makeKnob (ghostKnob, id::ghost, "GHOST", Size::medium, col::cyan);
        makeKnob (collapseKnob, id::collapse, "COLLAPSE", Size::medium);
        makeKnob (randomKnob, id::random, "RANDOM", Size::medium);
        makeKnob (tapeMemoryKnob, id::tapeMemory, "TAPE MEMORY", Size::medium, col::orange);

        addKey (freezeKey, "FREEZE", col::cyan);
        freezeKey.attach (apvts, id::freeze);
        freezeKey.setFontHeight (12.5f);
        freezeKey.setTooltip (tooltipFor (id::freeze));

        addKey (infiniteKey, "INFINITE", col::orange);
        infiniteKey.attach (apvts, id::infinite);
        infiniteKey.setFontHeight (12.5f);
        infiniteKey.setTooltip (tooltipFor (id::infinite));

        addAndMakeVisible (vuL);
        addAndMakeVisible (vuR);
        makeKnob (outGainKnob, id::outGain, "OUTPUT", Size::medium, col::cream);

        addKey (limiterKey, "LIMIT", col::amber);
        limiterKey.attach (apvts, id::limiter);
        limiterKey.setTooltip (tooltipFor (id::limiter));

        // ---- detail ------------------------------------------------------------------
        makeKnob (offsetKnob, id::offset, "OFFSET", Size::compact, col::cyan);
        makeKnob (spreadKnob, id::spread, "SPREAD", Size::compact, col::cyan);

        addChildComponent (orbitShapeDisplay);
        orbitShapeDisplay.setCaption ("ORBIT SHAPE");
        orbitShapeDisplay.setFontHeight (13.0f);
        orbitShapeDisplay.setColour (col::cyan);
        orbitShapeDisplay.setTooltip (tooltipFor (id::orbitShape));

        makeKnob (orbitRateKnob, id::orbitRate, "ORBIT RATE", Size::compact, col::cyan);
        addChildComponent (orbitDivDisplay);
        orbitDivDisplay.setCaption ("ORBIT LENGTH");
        orbitDivDisplay.setFontHeight (13.0f);
        orbitDivDisplay.setColour (col::cyan);
        orbitDivDisplay.setTooltip (tooltipFor (id::orbitDiv));

        addChildComponent (orbitSyncSwitch);
        orbitSyncSwitch.setTooltip (tooltipFor (id::orbitSync));

        addChildComponent (qualityDisplay);
        qualityDisplay.setCaption ("QUALITY");
        qualityDisplay.setFontHeight (13.0f);
        qualityDisplay.setTooltip (tooltipFor (id::quality));

        // ---- centre + overlays -----------------------------------------------------------
        addAndMakeVisible (visualizer);
        addChildComponent (motion);

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

        refreshDynamicState (true);
    }

    // ------------------------------------------------------------------------
    void setAdvanced (bool advanced)
    {
        isAdvanced = advanced;
        viewKey.setToggleState (advanced, juce::dontSendNotification);

        for (auto* c : std::initializer_list<juce::Component*> { offsetKnob.get(), spreadKnob.get(), &orbitShapeDisplay,
                                                                 &orbitSyncSwitch, &qualityDisplay, &motion })
            c->setVisible (advanced);

        refreshDynamicState (true);
        repaint();
    }

    void showLane (int laneIndex)     { motion.setLane (laneIndex); }

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

        if (isAdvanced)
            motion.tick (dt);

        vuL.push (vis.outPeakL.exchange (0.0f, std::memory_order_relaxed), dt);
        vuR.push (vis.outPeakR.exchange (0.0f, std::memory_order_relaxed), dt);

        filterScope.set (vis.hpHz.load (std::memory_order_relaxed), vis.lpHz.load (std::memory_order_relaxed),
                         pResonance->load() * 0.01f);

        // the limiter lamp flickers brighter while it is pulling the level down
        const float gr = vis.limiterGr.exchange (1.0f, std::memory_order_relaxed);
        limiterKey.setLit (limiterKey.getToggleState() ? juce::jlimit (0.45f, 1.0f, 0.45f + (1.0f - gr) * 4.0f) : 0.0f);

        timeReadout.refresh();
        refreshDynamicState (false);
    }

    // ------------------------------------------------------------------------
    void paint (juce::Graphics& g) override
    {
        // Panels, textures, screws and legends never change, so they are rendered once per
        // size at the display's real pixel density. Touching a knob then costs one blit
        // instead of redrawing the faceplate.
        const float scale = juce::jlimit (0.25f, 8.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
        const int w = juce::jmax (1, (int) std::ceil ((float) getWidth() * scale));
        const int h = juce::jmax (1, (int) std::ceil ((float) getHeight() * scale));

        if (! faceplate.isValid() || faceplate.getWidth() != w || faceplate.getHeight() != h
             || std::abs (scale - faceplateScale) > 1.0e-4f || faceplateAdvanced != isAdvanced)
        {
            faceplateScale = scale;
            faceplateAdvanced = isAdvanced;
            faceplate = juce::Image (juce::Image::RGB, w, h, true);

            juce::Graphics layer (faceplate);
            layer.addTransform (juce::AffineTransform::scale (scale));
            paintFaceplate (layer);
        }

        {
            juce::Graphics::ScopedSaveState state (g);
            g.addTransform (juce::AffineTransform::scale (1.0f / faceplateScale));
            g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
            g.drawImageAt (faceplate, 0, 0);
        }

        // algorithm description under the mode read-out
        g.setFont (font (FontKind::medium, 10.5f, 0.03f));
        g.setColour (col::creamDim);
        g.drawFittedText (modeInfo (juce::roundToInt (pMode->load())).description, descriptionArea, juce::Justification::centredLeft, 2, 0.9f);
    }

    void paintFaceplate (juce::Graphics& g)
    {
        const auto full = getLocalBounds().toFloat();

        g.fillAll (col::bg);
        g.setTiledImageFill (brushedTexture(), 0, 0, 0.35f);
        g.fillRect (full);

        // ---- top bar
        const auto top = full.withHeight (56.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1d2024), 0.0f, 0.0f, juce::Colour (0xff111315), 0.0f, top.getBottom(), false));
        g.fillRect (top);
        g.setTiledImageFill (brushedTexture(), 0, 0, 0.6f);
        g.fillRect (top);
        g.setColour (col::orange);
        g.fillRect (top.withHeight (2.0f));
        g.setColour (juce::Colours::black);
        g.fillRect (top.withTop (top.getBottom() - 1.0f));
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.fillRect (top.withTop (top.getBottom() - 2.0f).withHeight (1.0f));

        // logo: the name, and its last letter echoing away
        {
            const auto logoFont = font (FontKind::bold, 40.0f, 0.13f);
            const float x0 = 22.0f, y0 = 6.0f, h = 46.0f;
            const float wordW = textWidth (logoFont, "DLAY");
            const float advance = wordW - textWidth (logoFont, "DLA");

            g.setFont (logoFont);
            g.setColour (col::cream);
            g.drawText ("DLAY", juce::Rectangle<float> (x0, y0, wordW + 8.0f, h), juce::Justification::centredLeft, false);

            static constexpr float fades[] = { 0.62f, 0.30f, 0.13f };
            for (int i = 0; i < 3; ++i)
            {
                g.setColour (col::orange.withAlpha (fades[i]));
                g.drawText ("Y", juce::Rectangle<float> (x0 + wordW + advance * 0.86f * (float) i, y0, advance + 8.0f, h),
                            juce::Justification::centredLeft, false);
            }

            const float tx = x0 + wordW + advance * 0.86f * 3.0f + 12.0f;
            g.setFont (font (FontKind::semiBold, 9.0f, 0.24f));
            g.setColour (col::creamFaint);
            g.drawText ("BEND TIME", juce::Rectangle<float> (tx, 14.0f, 110.0f, 9.0f), juce::Justification::centredLeft, false);
            g.drawText ("SHAPE SPACE", juce::Rectangle<float> (tx, 24.5f, 110.0f, 9.0f), juce::Justification::centredLeft, false);
            g.drawText ("CREATE ECHOES", juce::Rectangle<float> (tx, 35.0f, 110.0f, 9.0f), juce::Justification::centredLeft, false);
        }

        g.setFont (monoFont (9.5f));
        g.setColour (col::creamFaint);
        g.drawText (juce::String ("v") + DLAY_VERSION_STRING, juce::Rectangle<float> (full.getRight() - 62.0f, 22.0f, 50.0f, 12.0f), juce::Justification::centredRight, false);

        // ---- panels
        drawPanel (g, leftPanel.toFloat());
        drawPanel (g, rightPanel.toFloat());
        drawPanel (g, signaturePanel.toFloat());

        drawSectionHeader (g, headerIn (leftPanel), "01", "DELAY ENGINE");
        drawSectionHeader (g, headerIn (rightPanel), "03", "CHARACTER");
        drawSectionHeader (g, headerIn (signaturePanel).withRight (832.0f), "04", "SIGNATURE");
        drawSectionHeader (g, headerIn (signaturePanel).withLeft (852.0f), "05", "OUTPUT");

        if (isAdvanced)
        {
            drawPanel (g, detailPanel.toFloat());
            drawSectionHeader (g, headerIn (detailPanel).withRight (190.0f), "06", "DETAIL");

            g.setFont (font (FontKind::medium, 10.5f, 0.03f));
            g.setColour (col::creamFaint);
            g.drawFittedText ("Shift-drag any knob for fine control. Double-click a knob to reset it, double-click its name to type a value. "
                              "Drag the tape in the display to change time and feedback.",
                              juce::Rectangle<int> (610, detailPanel.getY() + 10, 440, detailPanel.getHeight() - 20), juce::Justification::centredLeft, 3, 0.9f);
        }
    }

    void resized() override
    {
        constexpr int W = kBaseWidth;
        faceplate = {};

        // ---- top bar
        bypassKey.setBounds (296, 14, 66, 30);
        prevKey.setBounds (378, 14, 30, 30);
        presetDisplay.setBounds (412, 10, 348, 38);
        nextKey.setBounds (764, 14, 30, 30);
        favKey.setBounds (800, 14, 32, 30);
        saveKey.setBounds (838, 14, 56, 30);
        undoKey.setBounds (912, 14, 36, 30);
        redoKey.setBounds (952, 14, 36, 30);
        viewKey.setBounds (1004, 14, 92, 30);
        menuKey.setBounds (1102, 14, 58, 30);

        // ---- panels
        leftPanel      = { 8, 62, 262, 400 };
        rightPanel     = { W - 8 - 262, 62, 262, 400 };
        signaturePanel = { 8, 468, W - 16, 104 };
        detailPanel    = { 8, 578, W - 16, 72 };

        visualizer.setBounds (leftPanel.getRight() + 6, 62, rightPanel.getX() - leftPanel.getRight() - 12, 400);
        motion.setBounds (8, 656, W - 16, 224);
        browser.setBounds (0, 56, W, getHeight() - 56);

        // ---- delay engine
        {
            const int x = leftPanel.getX() + 16, w = leftPanel.getWidth() - 32, y = leftPanel.getY();

            const int tabW = (w - 8) / 3;
            for (int f = 0; f < 3; ++f)
                familyKeys[(size_t) f]->setBounds (x + f * (tabW + 4), y + 30, tabW, 24);

            modeDisplay.setBounds (x, y + 60, w, 34);
            descriptionArea = { x + 2, y + 96, w - 4, 26 };

            timeKnob->setBounds (x - 2, y + 126, 104, 104);
            divisionKnob->setBounds (timeKnob->getBounds());
            timeReadout.setBounds (x + 108, y + 130, w - 108, 38);
            syncSwitch.setBounds (x + 114, y + 172, 44, 62);
            inertiaKnob->setBounds (x + 166, y + 174, 58, 60);

            const int half = w / 2;
            feedbackKnob->setBounds (x + (half - 76) / 2, y + 236, 76, 80);
            mixKnob->setBounds (x + half + (half - 76) / 2, y + 236, 76, 80);

            const int quarter = w / 4;
            macroA->setBounds (x + (quarter - 54) / 2, y + 318, 54, 58);
            macroB->setBounds (x + quarter + (quarter - 54) / 2, y + 318, 54, 58);
            pingPongKnob->setBounds (x + 2 * quarter + (quarter - 54) / 2, y + 318, 54, 58);
            widthKnob->setBounds (x + 3 * quarter + (quarter - 54) / 2, y + 318, 54, 58);

            stereoSwitch.setBounds (x, y + 378, w, 16);
        }

        // ---- character
        {
            const int x = rightPanel.getX() + 16, w = rightPanel.getWidth() - 32, y = rightPanel.getY();
            const int third = w / 3;

            saturationKnob->setBounds (x + (third - 74) / 2, y + 28, 74, 78);
            driveKnob->setBounds (x + third + (third - 74) / 2, y + 28, 74, 78);
            satTypeDisplay.setBounds (x + 2 * third + 2, y + 44, third - 2, 36);

            filterScope.setBounds (x, y + 110, w, 44);

            lowCutKnob->setBounds (x + (third - 74) / 2, y + 158, 74, 78);
            highCutKnob->setBounds (x + third + (third - 74) / 2, y + 158, 74, 78);
            resonanceKnob->setBounds (x + 2 * third + (third - 56) / 2, y + 170, 56, 60);

            diffusionKnob->setBounds (x + (third - 74) / 2, y + 240, 74, 78);
            modDepthKnob->setBounds (x + third + (third - 74) / 2, y + 240, 74, 78);
            pitchKnob->setBounds (x + 2 * third + (third - 74) / 2, y + 240, 74, 78);

            modRateKnob->setBounds (x + (third - 56) / 2, y + 326, 56, 60);
            reverseKnob->setBounds (x + third + (third - 56) / 2, y + 326, 56, 60);
            duckingKnob->setBounds (x + 2 * third + (third - 56) / 2, y + 326, 56, 60);
        }

        // ---- signature + output
        {
            const int y = signaturePanel.getY() + 24;
            int x = 26;

            for (auto* k : { warpKnob.get(), orbitKnob.get(), ghostKnob.get(), collapseKnob.get(), randomKnob.get(), tapeMemoryKnob.get() })
            {
                k->setBounds (x + 14, y, 74, 76);
                x += 102;
            }

            freezeKey.setBounds (650, y + 12, 86, 46);
            infiniteKey.setBounds (742, y + 12, 86, 46);

            vuL.setBounds (852, y + 3, 112, 68);
            vuR.setBounds (970, y + 3, 112, 68);
            outGainKnob->setBounds (1090, y, 74, 76);
            limiterKey.setBounds (1170, y + 14, 54, 42);
        }

        // ---- detail
        {
            const int y = detailPanel.getY();
            offsetKnob->setBounds (196, y + 8, 58, 60);
            spreadKnob->setBounds (262, y + 8, 58, 60);
            orbitShapeDisplay.setBounds (344, y + 18, 124, 38);
            orbitRateKnob->setBounds (480, y + 8, 62, 60);
            orbitDivDisplay.setBounds (476, y + 18, 78, 38);
            orbitSyncSwitch.setBounds (560, y + 5, 44, 62);
            qualityDisplay.setBounds (W - 8 - 16 - 150, y + 18, 150, 38);
        }
    }

private:
    // ------------------------------------------------------------------------
    static juce::Rectangle<float> headerIn (juce::Rectangle<int> panel)
    {
        return juce::Rectangle<float> ((float) panel.getX() + 20.0f, (float) panel.getY() + 8.0f, (float) panel.getWidth() - 40.0f, 14.0f);
    }

    void addKey (KeyButton& key, const juce::String& text, juce::Colour led = col::amber)
    {
        key.setButtonText (text);
        key.setLedColour (led);
        addAndMakeVisible (key);
    }

    void makeKnob (std::unique_ptr<Knob>& knob, const char* paramId, const char* legend, Knob::Size size, juce::Colour accent = col::amber)
    {
        knob = std::make_unique<Knob> (apvts, paramId, legend, size, accent);
        knob->setComponentID (paramId);
        knob->setTooltip (tooltipFor (paramId));
        addAndMakeVisible (*knob);
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
        size.addItem (201, "75 %",  true, std::abs (scale - 0.75) < 0.01);
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
                static constexpr double scales[] = { 0.75, 1.0, 1.25, 1.5 };
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

            macroA->setLegend (info.macroA);
            macroA->setTooltip (juce::String (info.name) + " - " + info.macroA + ": " + info.tipA);
            macroB->setLegend (info.macroB);
            macroB->setTooltip (juce::String (info.name) + " - " + info.macroB + ": " + info.tipB);
            modeDisplay.setTooltip (juce::String (info.name) + ": " + info.description + "\n" + tooltipFor (id::mode));

            for (int f = 0; f < 3; ++f)
                familyKeys[(size_t) f]->setToggleState ((int) info.family == f, juce::dontSendNotification);

            repaint (descriptionArea);
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
            orbitRateKnob->setVisible (isAdvanced && ! orbitSynced);
            orbitDivDisplay.setVisible (isAdvanced && orbitSynced);
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

    // delay engine
    std::array<std::unique_ptr<KeyButton>, 3> familyKeys;
    ChoiceDisplay modeDisplay { apvts, id::mode };
    std::unique_ptr<Knob> timeKnob, divisionKnob, inertiaKnob, feedbackKnob, mixKnob, macroA, macroB, pingPongKnob, widthKnob;
    LeverSwitch syncSwitch { apvts, id::sync, "SYNC" };
    SegmentSwitch stereoSwitch { apvts, id::stereoMode, stereoModeNames() };

    // character
    std::unique_ptr<Knob> saturationKnob, driveKnob, lowCutKnob, highCutKnob, resonanceKnob,
                          diffusionKnob, modDepthKnob, pitchKnob, modRateKnob, reverseKnob, duckingKnob;
    ChoiceDisplay satTypeDisplay { apvts, id::satType };
    FilterScope filterScope;

    // signature + output
    std::unique_ptr<Knob> warpKnob, orbitKnob, ghostKnob, collapseKnob, randomKnob, tapeMemoryKnob, outGainKnob;
    KeyButton freezeKey { "FREEZE" }, infiniteKey { "INFINITE" }, limiterKey { "LIMIT" };
    VuMeter vuL { "L" }, vuR { "R" };

    // detail
    std::unique_ptr<Knob> offsetKnob, spreadKnob, orbitRateKnob;
    ChoiceDisplay orbitShapeDisplay { apvts, id::orbitShape }, orbitDivDisplay { apvts, id::orbitDiv }, qualityDisplay { apvts, id::quality };
    LeverSwitch orbitSyncSwitch { apvts, id::orbitSync, "SYNC", col::cyan };

    // centre and overlays
    EchoVisualizer visualizer;
    MotionEditor motion;
    PresetBrowser browser;
    TimeReadout timeReadout;

    std::atomic<float>* pMode = nullptr, *pSync = nullptr, *pOrbitSync = nullptr, *pResonance = nullptr;

    juce::Image faceplate;
    float faceplateScale = 0.0f;
    bool faceplateAdvanced = true;

    juce::Rectangle<int> leftPanel, rightPanel, signaturePanel, detailPanel, descriptionArea;
    int  shownMode = -1;
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

    // open at the stored scale, but never larger than the screen allows
    double scale = juce::jlimit (0.6, 2.0, dlayProcessor.getUiScale());

    if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
    {
        const auto area = display->userBounds;
        if (area.getWidth() > 0.0f && area.getHeight() > 0.0f)
            scale = juce::jmin (scale, juce::jmax (0.6, juce::jmin ((double) area.getWidth() * 0.96 / (double) kBaseWidth,
                                                                     (double) area.getHeight() * 0.9 / (double) baseHeight())));
    }

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

void DlayAudioProcessorEditor::applyConstraints()
{
    const double ratio = (double) kBaseWidth / (double) baseHeight();

    if (auto* limits = getConstrainer())
    {
        limits->setFixedAspectRatio (ratio);
        limits->setSizeLimits (juce::roundToInt (kBaseWidth * 0.6), juce::roundToInt (baseHeight() * 0.6),
                               kBaseWidth * 2, baseHeight() * 2);
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
    scale = juce::jlimit (0.6, 2.0, scale);
    setSize (juce::roundToInt (kBaseWidth * scale), juce::roundToInt (baseHeight() * scale));
}

void DlayAudioProcessorEditor::showPresetBrowser (bool shouldShow)
{
    content->showBrowser (shouldShow, false);
}

void DlayAudioProcessorEditor::showMotionLane (int laneIndex)
{
    content->showLane (laneIndex);
}

void DlayAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (dlay::ui::col::bg);
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
