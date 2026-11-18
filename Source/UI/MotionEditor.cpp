#include "MotionEditor.h"

namespace dlay::ui
{

namespace
{
    struct AxisWords { const char* up; const char* down; };

    constexpr AxisWords kAxisWords[kNumLanes] =
    {
        { "FASTER", "SLOWER" }, { "RIGHT", "LEFT" }, { "MORE", "LESS" },
        { "HIGH-PASS OPENS", "LOW-PASS CLOSES" }, { "UP", "DOWN" }, { "LOUDER ECHOES", "QUIETER ECHOES" }
    };

    juce::String describeValue (int lane, float v)
    {
        switch ((Lane) lane)
        {
            case Lane::speed:     return "x" + juce::String (std::exp2 (v), 2);
            case Lane::pan:       return std::abs (v) < 0.01f ? juce::String ("C") : (v < 0.0f ? "L" : "R") + juce::String (juce::roundToInt (std::abs (v) * 100.0f));
            case Lane::feedback:  return (v >= 0.0f ? "+" : "") + juce::String (juce::roundToInt (v * 50.0f)) + " %";
            case Lane::filter:    return (v >= 0.0f ? "HP +" : "LP ") + juce::String (v * 6.0f, 1) + " oct";
            case Lane::pitch:     return (v >= 0.0f ? "+" : "") + juce::String (v * 12.0f, 1) + " st";
            case Lane::mix:       return (v >= 0.0f ? "+" : "") + juce::String (juce::roundToInt (v * 100.0f)) + " %";
            case Lane::numLanes:
            default:              return {};
        }
    }

    void drawShapeIcon (juce::Graphics& g, juce::Rectangle<float> r, int index, juce::Colour colour)
    {
        const auto box = r.withSizeKeepingCentre (juce::jmin (r.getWidth(), 24.0f), juce::jmin (r.getHeight(), 12.0f));
        const float x0 = box.getX(), w = box.getWidth(), cy = box.getCentreY(), a = box.getHeight() * 0.5f;
        juce::Path p;

        switch ((LfoShape) index)
        {
            case LfoShape::sine:
                for (int i = 0; i <= 20; ++i)
                {
                    const float t = (float) i / 20.0f;
                    const juce::Point<float> pt (x0 + t * w, cy - a * std::sin (t * juce::MathConstants<float>::twoPi));
                    if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
                }
                break;

            case LfoShape::triangle:
                p.startNewSubPath (x0, cy);           p.lineTo (x0 + w * 0.25f, cy - a);
                p.lineTo (x0 + w * 0.75f, cy + a);    p.lineTo (x0 + w, cy);
                break;

            case LfoShape::square:
                p.startNewSubPath (x0, cy - a);       p.lineTo (x0 + w * 0.5f, cy - a);
                p.lineTo (x0 + w * 0.5f, cy + a);     p.lineTo (x0 + w, cy + a);
                break;

            case LfoShape::saw:
                p.startNewSubPath (x0, cy + a);       p.lineTo (x0 + w, cy - a);
                break;

            case LfoShape::random:
            {
                static constexpr float v[] = { 0.2f, -0.7f, 0.9f, -0.2f, 0.6f, -0.9f, 0.1f };
                for (int i = 0; i < 7; ++i)
                {
                    const juce::Point<float> pt (x0 + w * (float) i / 6.0f, cy - a * v[i]);
                    if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
                }
                p = p.createPathWithRoundedCorners (3.0f);
                break;
            }

            case LfoShape::sampleHold:
            {
                static constexpr float v[] = { 0.5f, -0.8f, 0.9f, -0.3f };
                for (int i = 0; i < 4; ++i)
                {
                    const float xa = x0 + w * (float) i / 4.0f, xb = x0 + w * (float) (i + 1) / 4.0f;
                    if (i == 0) p.startNewSubPath (xa, cy - a * v[i]); else p.lineTo (xa, cy - a * v[i]);
                    p.lineTo (xb, cy - a * v[i]);
                }
                break;
            }

            case LfoShape::custom:
                p.startNewSubPath (x0, cy + a * 0.6f);
                p.cubicTo (x0 + w * 0.3f, cy - a * 1.6f, x0 + w * 0.5f, cy + a * 1.4f, x0 + w, cy - a * 0.8f);
                g.setColour (colour);
                g.fillEllipse (x0 - 1.6f, cy + a * 0.6f - 1.6f, 3.2f, 3.2f);
                g.fillEllipse (x0 + w - 1.6f, cy - a * 0.8f - 1.6f, 3.2f, 3.2f);
                break;

            case LfoShape::numShapes:
            default: break;
        }

        g.setColour (colour);
        g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

// ============================================================================
//  per-lane controls (rebuilt when the lane tab changes)
// ============================================================================
struct MotionEditor::LaneControls
{
    LaneControls (APVTS& s, int laneIndex)
        : shape (s, id::lane (laneIndex, id::laneShape), lfoShapeNames()),
          depth (s, id::lane (laneIndex, id::laneDepth), "Depth", Knob::Size::medium),
          rate (s, id::lane (laneIndex, id::laneRate), "Rate", Knob::Size::medium),
          phase (s, id::lane (laneIndex, id::lanePhase), "Phase", Knob::Size::compact),
          stereo (s, id::lane (laneIndex, id::laneStereo), "L / R", Knob::Size::compact),
          sync (s, id::lane (laneIndex, id::laneSync), "Sync"),
          length (s, id::lane (laneIndex, id::laneDiv))
    {
        shape.drawIcon = drawShapeIcon;
        shape.setTooltip ("Lane shape: sine, triangle, square, saw, smooth random, sample & hold, or your own curve.");
        depth.setTooltip (juce::String ("How far this lane moves its target. At 100 %: ") + kLanes[(size_t) laneIndex].unitHint + ". Negative values flip the curve.");
        rate.setTooltip ("Speed of the lane when it is not synced to the host tempo.");
        phase.setTooltip ("Where in the cycle the lane starts.");
        stereo.setTooltip ("Phase offset of the right channel. Above zero the left and right sides move independently.");
        sync.setTooltip ("Lock the lane length to the host tempo. While the transport runs, the lane follows the song position.");
        length.setTooltip ("Length of one pass of the lane, in note values.");
        length.setCaption ("Length", Knob::Size::medium);
    }

    SegmentSwitch shape;
    Knob depth, rate, phase, stereo;
    ToggleSwitch sync;
    ChoiceDisplay length;
};

// ============================================================================
//  curve view
// ============================================================================
class MotionEditor::CurveView : public juce::Component,
                                public juce::SettableTooltipClient
{
public:
    explicit CurveView (DlayAudioProcessor& p) : proc (p)
    {
        setOpaque (true);       // redrawn 30 times a second: do not drag the panel behind it along

        setTooltip ("Click to add a point, drag to move it, double-click to remove it.\n"
                    "Alt-drag (or right-drag) a segment to bend it. Hold Shift to snap.\n"
                    "With DRAW on, paint the curve freehand. Editing switches the lane to CUSTOM.");
        setLane (0);
    }

    bool drawMode = false;

    void setLane (int l)
    {
        lane = l;
        shapeRaw  = proc.apvts.getRawParameterValue (id::lane (lane, id::laneShape));
        depthRaw  = proc.apvts.getRawParameterValue (id::lane (lane, id::laneDepth));
        stereoRaw = proc.apvts.getRawParameterValue (id::lane (lane, id::laneStereo));
        points = proc.curves->getPoints (lane);
        traceL.fill (kEmpty);
        traceR.fill (kEmpty);
        lastIndex = -1;
        repaint();
    }

    void tick()
    {
        auto& motion = proc.engine.motion;
        phase  = motion.uiPhase[(size_t) lane].load (std::memory_order_relaxed);
        valueL = motion.uiValue[(size_t) lane][0].load (std::memory_order_relaxed);
        valueR = motion.uiValue[(size_t) lane][1].load (std::memory_order_relaxed);

        // sweep-scope trace of what the lane really did on this pass
        const int n = (int) traceL.size();
        const int index = juce::jlimit (0, n - 1, (int) (phase * (float) n));

        if (lastIndex < 0 || index == lastIndex)
        {
            traceL[(size_t) index] = valueL;
            traceR[(size_t) index] = valueR;
        }
        else
        {
            int steps = index - lastIndex;
            if (steps < 0) steps += n;
            if (steps > n / 3) steps = 1;                   // lane much faster than the frame rate

            for (int k = 1; k <= steps; ++k)
            {
                const int i = (index - steps + k + n) % n;
                traceL[(size_t) i] = valueL;
                traceR[(size_t) i] = valueR;
            }
        }

        for (int k = 1; k <= 8; ++k)                        // moving gap ahead of the beam
        {
            traceL[(size_t) ((index + k) % n)] = kEmpty;
            traceR[(size_t) ((index + k) % n)] = kEmpty;
        }

        lastIndex = index;

        if (dragging < 0 && ! bending && ! stroking)
            points = proc.curves->getPoints (lane);         // follow presets, undo and redo

        repaint();
    }

    void resetCurve()
    {
        ensureCustom();
        commit (MotionCurves::defaultCurve (lane));
    }

    void invertCurve()
    {
        ensureCustom();
        auto flipped = proc.curves->getPoints (lane);
        for (auto& p : flipped)
            p.y = 1.0f - p.y;
        commit (flipped);
    }

    // ------------------------------------------------------------------------
    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat().reduced (1.5f);
        constexpr float corner = 14.0f;

        g.fillAll (col::card);
        drawScreen (g, b, corner);

        const auto p = plot();
        const int shape = currentShape();
        const float depth = depthRaw->load() * 0.01f;
        const float stereo = stereoRaw->load() / 360.0f;
        const bool custom = shape == (int) LfoShape::custom;
        const bool randomShape = shape == (int) LfoShape::random || shape == (int) LfoShape::sampleHold;
        const float cy = p.getCentreY(), amp = p.getHeight() * 0.5f;
        auto& motion = proc.engine.motion;

        {
            juce::Graphics::ScopedSaveState screenClip (g);
            juce::Path clip;
            clip.addRoundedRectangle (b, corner);
            g.reduceClipRegion (clip);
            drawStars (g, b, 0x3077u + (juce::uint32) lane * 131u, 70, 0.8f);
        }

        // ---- grid
        for (int i = 0; i <= 16; ++i)
        {
            const float x = p.getX() + p.getWidth() * (float) i / 16.0f;
            g.setColour (juce::Colours::white.withAlpha ((i % 4) == 0 ? 0.13f : 0.045f));
            g.fillRect (x - 0.5f, p.getY(), 1.0f, p.getHeight());
        }

        for (int i = 0; i <= 4; ++i)
        {
            const float y = p.getY() + p.getHeight() * (float) i / 4.0f;
            g.setColour (juce::Colours::white.withAlpha (i == 2 ? 0.26f : 0.05f));
            g.fillRect (p.getX(), y - 0.5f, p.getWidth(), 1.0f);
        }

        g.setFont (capsFont (9.5f));
        g.setColour (col::starFaint);
        g.drawText (kAxisWords[lane].up, juce::Rectangle<float> (p.getX() + 6.0f, p.getY() + 3.0f, 200.0f, 12.0f), juce::Justification::centredLeft, false);
        g.drawText (kAxisWords[lane].down, juce::Rectangle<float> (p.getX() + 6.0f, p.getBottom() - 15.0f, 200.0f, 12.0f), juce::Justification::centredLeft, false);

        g.setFont (monoFont (10.0f));
        static const char* quarters[] = { "0", "1/4", "1/2", "3/4", "1" };
        for (int i = 0; i <= 4; ++i)
        {
            const float x = p.getX() + p.getWidth() * (float) i / 4.0f;
            const auto just = i == 0 ? juce::Justification::centredLeft : (i == 4 ? juce::Justification::centredRight : juce::Justification::centred);
            g.drawText (quarters[i], juce::Rectangle<float> (x - 20.0f, p.getBottom() + 2.0f, 40.0f, 12.0f)
                                         .translated (i == 0 ? 20.0f : (i == 4 ? -20.0f : 0.0f), 0.0f), just, false);
        }

        // everything that follows is the plot itself and stays inside it
        {
            juce::Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (p.expanded (1.0f, 6.0f).toNearestInt());

            const int n = juce::jmax (32, (int) p.getWidth() / 2);

            // ---- what reaches the engine: shape x depth (filled), right channel in purple
            if (! randomShape && std::abs (depth) > 0.001f)
            {
                juce::Path area, line, lineR;
                area.startNewSubPath (p.getX(), cy);

                for (int i = 0; i <= n; ++i)
                {
                    const float t = (float) i / (float) n;
                    const float x = p.getX() + p.getWidth() * t;
                    const float v = (float) motion.evaluate (lane, shape, (double) juce::jmin (t, 0.9999f)) * depth;
                    area.lineTo (x, cy - v * amp);
                    if (i == 0) line.startNewSubPath (x, cy - v * amp); else line.lineTo (x, cy - v * amp);

                    if (stereo > 0.001f)
                    {
                        const float vr = (float) motion.evaluate (lane, shape, (double) t + (double) stereo) * depth;
                        if (i == 0) lineR.startNewSubPath (x, cy - vr * amp); else lineR.lineTo (x, cy - vr * amp);
                    }
                }

                area.lineTo (p.getRight(), cy);
                area.closeSubPath();

                g.setGradientFill (accentGradient (p, 0.20f));
                g.fillPath (area);
                g.setColour (col::pink.withAlpha (0.9f));
                g.strokePath (line, juce::PathStrokeType (1.5f));

                if (stereo > 0.001f)
                {
                    g.setColour (col::purple.withAlpha (0.85f));
                    g.strokePath (lineR, juce::PathStrokeType (1.2f));
                }
            }

            // ---- the shape at full scale
            if (! randomShape)
            {
                juce::Path full;
                for (int i = 0; i <= n; ++i)
                {
                    const float t = (float) i / (float) n;
                    const float v = custom ? MotionCurves::evaluate (points, t) * 2.0f - 1.0f
                                           : (float) motion.evaluate (lane, shape, (double) juce::jmin (t, 0.9999f));
                    const juce::Point<float> pt (p.getX() + p.getWidth() * t, cy - v * amp);
                    if (i == 0) full.startNewSubPath (pt); else full.lineTo (pt);
                }

                g.setColour (custom ? col::star.withAlpha (0.9f) : col::star.withAlpha (0.24f));
                g.strokePath (full, juce::PathStrokeType (custom ? 1.6f : 1.0f));
            }
            else
            {
                g.setFont (capsFont (9.5f));
                g.setColour (col::starFaint);
                g.drawText (shape == (int) LfoShape::random ? "SMOOTH RANDOM  -  LIVE TRACE" : "SAMPLE & HOLD  -  LIVE TRACE",
                            p.withTrimmedTop (p.getHeight() - 15.0f).translated (-6.0f, -3.0f), juce::Justification::centredRight, false);
            }

            // ---- live trace
            const auto drawTrace = [&] (const std::array<float, 192>& trace, juce::Colour c)
            {
                juce::Path path;
                bool open = false;
                const int count = (int) trace.size();

                for (int i = 0; i < count; ++i)
                {
                    const float v = trace[(size_t) i];
                    if (v <= kEmpty + 1.0f) { open = false; continue; }

                    const juce::Point<float> pt (p.getX() + p.getWidth() * ((float) i + 0.5f) / (float) count, cy - juce::jlimit (-1.0f, 1.0f, v) * amp);
                    if (! open) { path.startNewSubPath (pt); open = true; } else path.lineTo (pt);
                }

                g.setColour (c.withAlpha (0.24f));
                g.strokePath (path, juce::PathStrokeType (4.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.setColour (c.interpolatedWith (juce::Colours::white, 0.45f));
                g.strokePath (path, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            };

            if (std::abs (depth) > 0.001f)
            {
                if (stereo > 0.001f)
                    drawTrace (traceR, col::purple);
                drawTrace (traceL, col::pink);
            }

            // ---- beam
            {
                const float x = p.getX() + p.getWidth() * phase;
                g.setColour (col::star.withAlpha (std::abs (depth) > 0.001f ? 0.75f : 0.25f));
                g.fillRect (x - 0.5f, p.getY(), 1.0f, p.getHeight());

                if (std::abs (depth) > 0.001f)
                {
                    const float y = cy - juce::jlimit (-1.0f, 1.0f, valueL) * amp;
                    g.setGradientFill (juce::ColourGradient (col::pink.withAlpha (0.75f), x, y, col::pink.withAlpha (0.0f), x + 14.0f, y, true));
                    g.fillEllipse (x - 14.0f, y - 14.0f, 28.0f, 28.0f);
                    g.setColour (col::star);
                    g.fillEllipse (x - 3.2f, y - 3.2f, 6.4f, 6.4f);

                    if (stereo > 0.001f)
                    {
                        const float yr = cy - juce::jlimit (-1.0f, 1.0f, valueR) * amp;
                        g.setColour (col::purple);
                        g.fillEllipse (x - 2.8f, yr - 2.8f, 5.6f, 5.6f);
                    }
                }
            }

            // ---- handles
            if (custom)
            {
                for (int i = 0; i < (int) points.size(); ++i)
                {
                    const auto pt = toScreen (points[(size_t) i]);
                    const bool hot = i == dragging || i == hover;
                    const float r = hot ? 5.6f : 4.0f;
                    g.setColour (col::space);
                    g.fillEllipse (pt.x - r - 1.0f, pt.y - r - 1.0f, 2.0f * r + 2.0f, 2.0f * r + 2.0f);
                    g.setColour (hot ? col::pink : col::star);
                    g.drawEllipse (pt.x - r, pt.y - r, 2.0f * r, 2.0f * r, 1.5f);
                    if (hot) g.fillEllipse (pt.x - 2.2f, pt.y - 2.2f, 4.4f, 4.4f);
                }
            }
        }

        // live read-out of the lane, on top of the plot: its name and what it is doing right now
        {
            const juce::String name (kLanes[(size_t) lane].name);
            const auto value = describeValue (lane, valueL);
            const auto nameFont = capsFont (10.5f);
            const auto valueFont = monoFont (12.0f);
            const float vw = textWidth (valueFont, value) + 4.0f, nw = textWidth (nameFont, name) + 10.0f;
            const auto pill = juce::Rectangle<float> (p.getRight() - vw - nw - 24.0f, p.getY() + 2.0f, vw + nw + 22.0f, 20.0f);

            g.setColour (juce::Colour (0xff120e1b).withAlpha (0.82f));
            g.fillRoundedRectangle (pill, 10.0f);

            auto r = pill.reduced (11.0f, 0.0f);
            drawAccentText (g, name, r.removeFromLeft (nw), juce::Justification::centredLeft, nameFont);
            g.setFont (valueFont);
            g.setColour (col::star);
            g.drawText (value, r, juce::Justification::centredRight, false);
        }
    }

    // ------------------------------------------------------------------------
    void mouseMove (const juce::MouseEvent& e) override
    {
        const int h = currentShape() == (int) LfoShape::custom ? hitPoint (e.position) : -1;
        if (h != hover) { hover = h; repaint(); }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        if (hover != -1) { hover = -1; repaint(); }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! plot().expanded (8.0f).contains (e.position))
            return;

        ensureCustom();
        points = proc.curves->getPoints (lane);
        before = points;
        changed = false;

        if (drawMode)
        {
            // freehand: work on a fixed grid so a stroke simply overwrites what it passes
            std::vector<CurvePoint> grid;
            for (int i = 0; i <= kGridSteps; ++i)
            {
                const float x = (float) i / (float) kGridSteps;
                grid.push_back ({ x, MotionCurves::evaluate (points, x), 0.0f });
            }
            points = grid;
            stroking = true;
            lastStrokeStep = -1;
            paintStroke (e);
            return;
        }

        if (e.mods.isAltDown() || e.mods.isPopupMenu())
        {
            const float x = toNorm (e.position).x;
            for (int i = 0; i + 1 < (int) points.size(); ++i)
            {
                if (x >= points[(size_t) i].x && x <= points[(size_t) i + 1].x)
                {
                    bending = true;
                    bendIndex = i;
                    bendStart = points[(size_t) i].curve;
                    bendRising = points[(size_t) i + 1].y >= points[(size_t) i].y;
                    break;
                }
            }
            return;
        }

        int index = hitPoint (e.position);

        if (index < 0)
        {
            auto np = toNorm (e.position);
            CurvePoint cp { np.x, np.y, 0.0f };
            const auto it = std::lower_bound (points.begin(), points.end(), cp, [] (const CurvePoint& a, const CurvePoint& b) { return a.x < b.x; });
            index = (int) (it - points.begin());
            points.insert (it, cp);
            changed = true;
            push();
        }

        dragging = index;
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (stroking)
        {
            paintStroke (e);
        }
        else if (bending)
        {
            const float delta = (float) e.getDistanceFromDragStartY() / 70.0f;
            points[(size_t) bendIndex].curve = juce::jlimit (-1.0f, 1.0f, bendStart + (bendRising ? delta : -delta));
            changed = true;
            push();
        }
        else if (dragging >= 0 && dragging < (int) points.size())
        {
            auto np = toNorm (e.position);

            if (e.mods.isShiftDown())
            {
                np.x = std::round (np.x * 16.0f) / 16.0f;
                np.y = std::round (np.y * 8.0f) / 8.0f;
            }

            auto& pt = points[(size_t) dragging];
            const bool first = dragging == 0, last = dragging == (int) points.size() - 1;

            if (first)      pt.x = 0.0f;
            else if (last)  pt.x = 1.0f;
            else            pt.x = juce::jlimit (points[(size_t) dragging - 1].x + 0.002f, points[(size_t) dragging + 1].x - 0.002f, np.x);

            pt.y = juce::jlimit (0.0f, 1.0f, np.y);
            changed = true;
            push();
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (changed)
        {
            // one undo step for the whole gesture
            proc.curves->setPoints (lane, before, false);
            commit (points);
        }

        dragging = -1;
        bending = false;
        stroking = false;
        changed = false;
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (currentShape() != (int) LfoShape::custom || drawMode)
            return;

        const int index = hitPoint (e.position);
        auto pts = proc.curves->getPoints (lane);

        if (index > 0 && index < (int) pts.size() - 1)
        {
            pts.erase (pts.begin() + index);
            commit (pts);
        }
    }

private:
    static constexpr float kEmpty = -100.0f;
    static constexpr int kGridSteps = 32;

    int currentShape() const                    { return juce::roundToInt (shapeRaw->load()); }

    juce::Rectangle<float> plot() const
    {
        return getLocalBounds().toFloat().reduced (14.0f, 11.0f).withTrimmedBottom (11.0f);
    }

    juce::Point<float> toScreen (const CurvePoint& cp) const
    {
        const auto p = plot();
        return { p.getX() + cp.x * p.getWidth(), p.getBottom() - cp.y * p.getHeight() };
    }

    juce::Point<float> toNorm (juce::Point<float> pos) const
    {
        const auto p = plot();
        return { juce::jlimit (0.0f, 1.0f, (pos.x - p.getX()) / p.getWidth()),
                 juce::jlimit (0.0f, 1.0f, (p.getBottom() - pos.y) / p.getHeight()) };
    }

    int hitPoint (juce::Point<float> pos) const
    {
        int best = -1;
        float bestDist = 9.0f;

        for (int i = 0; i < (int) points.size(); ++i)
        {
            const float d = toScreen (points[(size_t) i]).getDistanceFrom (pos);
            if (d < bestDist) { bestDist = d; best = i; }
        }

        return best;
    }

    void push()
    {
        proc.curves->setPoints (lane, points, false);
        repaint();
    }

    void commit (const std::vector<CurvePoint>& newPoints)
    {
        proc.undoManager.beginNewTransaction ("Edit MOTION curve");
        proc.curves->setPoints (lane, newPoints, true);
        proc.undoManager.beginNewTransaction();
        points = newPoints;
        repaint();
    }

    /** Editing an LFO lane turns it into a custom curve that starts out as that LFO. */
    void ensureCustom()
    {
        const int shape = currentShape();
        if (shape == (int) LfoShape::custom)
            return;

        std::vector<CurvePoint> seed;

        switch ((LfoShape) shape)
        {
            case LfoShape::sine:
                for (int i = 0; i <= 16; ++i)
                    seed.push_back ({ (float) i / 16.0f, 0.5f + 0.5f * std::sin ((float) i / 16.0f * juce::MathConstants<float>::twoPi), 0.0f });
                break;
            case LfoShape::triangle:
                seed = { { 0.0f, 0.5f, 0.0f }, { 0.25f, 1.0f, 0.0f }, { 0.75f, 0.0f, 0.0f }, { 1.0f, 0.5f, 0.0f } };
                break;
            case LfoShape::square:
                seed = { { 0.0f, 1.0f, 0.0f }, { 0.499f, 1.0f, 0.0f }, { 0.5f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
                break;
            case LfoShape::saw:
                seed = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 0.0f } };
                break;
            default: break;
        }

        proc.undoManager.beginNewTransaction ("MOTION lane to custom curve");

        if (! seed.empty())
            proc.curves->setPoints (lane, seed, true);

        if (auto* param = proc.apvts.getParameter (id::lane (lane, id::laneShape)))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 ((float) LfoShape::custom));
            param->endChangeGesture();
        }
    }

    void paintStroke (const juce::MouseEvent& e)
    {
        const auto np = toNorm (e.position);
        const int step = juce::jlimit (0, kGridSteps, juce::roundToInt (np.x * (float) kGridSteps));

        if (lastStrokeStep < 0)
        {
            points[(size_t) step].y = np.y;
        }
        else
        {
            // fill every step the pointer skipped over
            const int from = juce::jmin (lastStrokeStep, step), to = juce::jmax (lastStrokeStep, step);
            for (int s = from; s <= to; ++s)
            {
                const float t = to == from ? 1.0f : (float) (s - lastStrokeStep) / (float) (step - lastStrokeStep);
                points[(size_t) s].y = lastStrokeY + (np.y - lastStrokeY) * t;
            }
        }

        lastStrokeStep = step;
        lastStrokeY = np.y;
        changed = true;
        push();
    }

    DlayAudioProcessor& proc;
    int lane = 0;
    std::atomic<float>* shapeRaw = nullptr, *depthRaw = nullptr, *stereoRaw = nullptr;

    std::vector<CurvePoint> points, before;
    int dragging = -1, hover = -1, bendIndex = 0, lastStrokeStep = -1;
    bool bending = false, bendRising = true, stroking = false, changed = false;
    float bendStart = 0.0f, lastStrokeY = 0.5f;

    std::array<float, 192> traceL {}, traceR {};
    int lastIndex = -1;
    float phase = 0.0f, valueL = 0.0f, valueR = 0.0f;
};

// ============================================================================
//  MotionEditor
// ============================================================================
MotionEditor::MotionEditor (DlayAudioProcessor& p)
    : proc (p)
{
    for (int i = 0; i < kNumLanes; ++i)
    {
        laneDepth[(size_t) i] = proc.apvts.getRawParameterValue (id::lane (i, id::laneDepth));

        auto& tab = tabs[(size_t) i];
        tab = std::make_unique<KeyButton> (kLanes[(size_t) i].name);
        tab->setFontSize (11.0f);
        tab->setDot (true);
        tab->setTooltip (juce::String ("MOTION lane: ") + kLanes[(size_t) i].name + ". The light shows how hard the lane is working right now.");
        tab->onClick = [this, i] { setLane (i); };
        addAndMakeVisible (*tab);
    }

    curve = std::make_unique<CurveView> (proc);
    curve->setComponentID ("motionCurve");
    addAndMakeVisible (*curve);

    for (auto* b : { &drawButton, &resetButton, &invertButton })
    {
        b->setFontSize (11.5f);
        addAndMakeVisible (*b);
    }

    drawButton.setClickingTogglesState (true);
    drawButton.setOnFill (KeyButton::OnFill::accent);
    drawButton.setTooltip ("Freehand: paint the curve directly with the mouse.");
    drawButton.onClick = [this] { curve->drawMode = drawButton.getToggleState(); };

    resetButton.setTooltip ("Put this lane's custom curve back to its starting shape.");
    resetButton.onClick = [this] { curve->resetCurve(); };

    invertButton.setTooltip ("Flip this lane's custom curve upside down.");
    invertButton.onClick = [this] { curve->invertCurve(); };

    setLane (juce::jlimit (0, kNumLanes - 1, proc.getUiLane()));
}

MotionEditor::~MotionEditor() = default;

void MotionEditor::setLane (int laneIndex)
{
    lane = juce::jlimit (0, kNumLanes - 1, laneIndex);
    proc.setUiLane (lane);

    for (int i = 0; i < kNumLanes; ++i)
        tabs[(size_t) i]->setToggleState (i == lane, juce::dontSendNotification);

    controls = std::make_unique<LaneControls> (proc.apvts, lane);

    for (auto* c : std::initializer_list<juce::Component*> { &controls->shape, &controls->depth, &controls->rate, &controls->phase,
                                                             &controls->stereo, &controls->sync, &controls->length })
        addAndMakeVisible (*c);

    const bool synced = controls->sync.getToggleState();
    controls->length.setVisible (synced);
    controls->rate.setVisible (! synced);

    curve->setLane (lane);
    resized();
    repaint();
}

void MotionEditor::tick (float)
{
    auto& motion = proc.engine.motion;

    for (int i = 0; i < kNumLanes; ++i)
    {
        const float depth = laneDepth[(size_t) i]->load() * 0.01f;
        const float value = std::abs (motion.uiValue[(size_t) i][0].load (std::memory_order_relaxed));
        tabs[(size_t) i]->setLit (std::abs (depth) < 0.001f ? 0.0f : 0.3f + 0.7f * juce::jmin (1.0f, value / juce::jmax (0.05f, std::abs (depth))));
    }

    if (controls != nullptr)
    {
        const bool synced = controls->sync.getToggleState();

        if (controls->length.isVisible() != synced || controls->rate.isVisible() == synced)
        {
            controls->length.setVisible (synced);
            controls->rate.setVisible (! synced);
        }
    }

    curve->tick();
}

void MotionEditor::resized()
{
    constexpr int pad = 16, headerY = 12, headerH = 32, contentY = 54, controlW = 380;

    const int right = getWidth() - pad;
    const int contentH = getHeight() - contentY - pad;

    // ---- top row: lane keys after the card's tabs, curve tools at the far right
    {
        constexpr int toolW = 76, toolGap = 6;
        int x = right - (3 * toolW + 2 * toolGap);

        for (auto* b : { &drawButton, &resetButton, &invertButton })
        {
            b->setBounds (x, headerY, toolW, headerH);
            x += toolW + toolGap;
        }

        const int first = headerInset;
        const int room = right - (3 * toolW + 2 * toolGap) - 24 - first;
        const int gap = 6;
        const int tabW = juce::jlimit (60, 100, (room - gap * (kNumLanes - 1)) / kNumLanes);

        for (int i = 0; i < kNumLanes; ++i)
            tabs[(size_t) i]->setBounds (first + i * (tabW + gap), headerY, tabW, headerH);
    }

    controlArea = { right - controlW, contentY, controlW, contentH };
    curveArea   = { pad, contentY, controlArea.getX() - 16 - pad, contentH };

    curve->setBounds (curveArea);

    if (controls != nullptr)
    {
        auto r = controlArea;
        controls->shape.setBounds (r.removeFromTop (32));

        // a row of dials, their names on one line at the bottom
        const int bottom = controlArea.getBottom() - 4;
        const int x = controlArea.getX();

        controls->depth.setBounds (x, bottom - 94, 84, 94);
        controls->rate.setBounds (x + 88, bottom - 94, 84, 94);
        controls->length.setBounds (x + 82, bottom - 94, 96, 94);
        // (one unit higher: the names of small dials are set in a slightly lower line)
        controls->sync.setBounds (x + 182, bottom - 69, 56, 68);
        controls->phase.setBounds (x + 244, bottom - 69, 68, 68);
        controls->stereo.setBounds (x + 312, bottom - 69, 68, 68);
    }
}

} // namespace dlay::ui
