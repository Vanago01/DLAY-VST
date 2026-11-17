#include "EchoVisualizer.h"

namespace dlay::ui
{

namespace
{
    constexpr float kPiF = juce::MathConstants<float>::pi;

    juce::String formatMs (float ms)
    {
        // (juce::String (value, 0) means "as many decimals as it takes", so whole numbers are rounded explicitly)
        if (ms >= 1000.0f)  return juce::String (ms / 1000.0f, 2) + " s";
        if (ms >= 100.0f)   return juce::String (juce::roundToInt (ms)) + " ms";
        return juce::String (ms, 1) + " ms";
    }
}

EchoVisualizer::EchoVisualizer (DlayAudioProcessor& p)
    : proc (p), vis (p.engine.visual)
{
    setTooltip ("The tape loop: sound enters at REC and travels right to the PLAY heads.\n"
                "Drag left/right to change the time, up/down to change the feedback.\n"
                "Drag inside the ORBIT radar to set its size; double-click it for the next shape.");

    setOpaque (true);           // paints every pixel itself, so the panels behind it are never redrawn

    for (auto& t : trail)
        t = {};

    pOrbit      = proc.apvts.getRawParameterValue (id::orbit);
    pOrbitShape = proc.apvts.getRawParameterValue (id::orbitShape);
    pOrbitSync  = proc.apvts.getRawParameterValue (id::orbitSync);
    pOrbitDiv   = proc.apvts.getRawParameterValue (id::orbitDiv);
    pOrbitRate  = proc.apvts.getRawParameterValue (id::orbitRate);
    pInfinite   = proc.apvts.getRawParameterValue (id::infinite);
    pSync       = proc.apvts.getRawParameterValue (id::sync);
    pTime       = proc.apvts.getRawParameterValue (id::time);
    pDivision   = proc.apvts.getRawParameterValue (id::division);
    pFeedback   = proc.apvts.getRawParameterValue (id::feedback);
}

void EchoVisualizer::resized()
{
    screen = getLocalBounds().toFloat().reduced (8.0f);

    auto inner = screen.reduced (10.0f, 7.0f);
    statusBar = inner.removeFromTop (17.0f);
    inner.removeFromTop (5.0f);

    const float radarW = juce::jmin (inner.getHeight() * 0.98f, inner.getWidth() * 0.35f);
    radarBox = inner.removeFromRight (radarW);
    inner.removeFromRight (14.0f);
    field = inner;

    tapeX0 = field.getX() + 26.0f;
    tapeX1 = field.getRight() - 4.0f;

    backgroundLayer = {};       // rebuilt on the next paint
    overlayLayer = {};
}

float EchoVisualizer::xForDelay (float delayMs) const noexcept
{
    return tapeX0 + (tapeX1 - tapeX0) * delayMs / juce::jmax (1.0f, viewMs);
}

float EchoVisualizer::envelopeAt (const std::array<std::atomic<float>, (size_t) VisualData::kRingSize>& ring,
                                  float delayMs, float spanMs) const
{
    const int start = (int) (delayMs / hopMs);
    const int span  = juce::jlimit (1, 64, (int) std::ceil (spanMs / hopMs));
    float peak = 0.0f;

    for (int k = 0; k < span; ++k)
    {
        const auto age = (uint32_t) (start + k);

        if (age >= ringValid)
            break;                              // recorded before the tape was last erased: gone

        const auto idx = (size_t) ((ringWrite - 1u - age) & (uint32_t) (VisualData::kRingSize - 1));
        peak = juce::jmax (peak, ring[idx].load (std::memory_order_relaxed));
    }

    return peak;
}

juce::Point<float> EchoVisualizer::orbitPoint (int shape, float phase, float amount) const
{
    const float ph = 2.0f * kPiF * phase;
    float x = 0.0f, y = 0.0f;

    switch ((OrbitShape) shape)
    {
        case OrbitShape::circle:    x = std::sin (ph);  y = std::cos (ph);                break;
        case OrbitShape::figure8:   x = std::sin (ph);  y = 0.8f * std::sin (2.0f * ph);  break;
        case OrbitShape::pendulum:  x = std::sin (ph);  y = 0.25f * std::cos (2.0f * ph); break;

        case OrbitShape::spiral:
        {
            const float turns = phase * 8.0f;
            const float r = 0.2f + 0.8f * std::abs (std::sin (kPiF * turns / 4.0f));
            x = r * std::sin (2.0f * kPiF * turns);
            y = r * std::cos (2.0f * kPiF * turns);
            break;
        }

        case OrbitShape::scatter:
        {
            const auto i = (uint32_t) (phase * 16.0f);
            x = (float) hash01 (i, 0x0B17u) * 2.0f - 1.0f;
            y = (float) hash01 (i, 0x0B18u) * 2.0f - 1.0f;
            break;
        }

        case OrbitShape::numShapes:
        default: break;
    }

    return { x * amount, y * amount };
}

// ============================================================================
void EchoVisualizer::tick (float dt)
{
    time += dt;

    numHeads = juce::jlimit (0, (int) VisualData::kMaxHeads, vis.numHeads.load (std::memory_order_relaxed));

    for (int i = 0; i < numHeads; ++i)
    {
        const auto& h = vis.heads[(size_t) i];
        heads[(size_t) i] = { h.delayMs.load (std::memory_order_relaxed), h.pan.load (std::memory_order_relaxed),
                              h.gain.load (std::memory_order_relaxed), h.kind.load (std::memory_order_relaxed) };
    }

    ringWrite = vis.ringWrite.load (std::memory_order_acquire);
    ringValid = ringWrite - vis.ringErasedAt.load (std::memory_order_relaxed);      // slots since the last erase
    hopMs     = juce::jmax (0.05f, vis.ringHopMs.load (std::memory_order_relaxed));

    const float timeL = vis.timeMsL.load (std::memory_order_relaxed);
    const float timeR = vis.timeMsR.load (std::memory_order_relaxed);
    const float fb    = vis.feedback.load (std::memory_order_relaxed);

    chainL = juce::jmax (0.0f, vis.chainMsL.load (std::memory_order_relaxed));
    chainR = juce::jmax (0.0f, vis.chainMsR.load (std::memory_order_relaxed));
    const float chain = 0.5f * (chainL + chainR);

    // Zoom so that the furthest playback head sits comfortably inside the view. GHOST voices
    // and TIME COLLAPSE bounces read older tape further right: make room for them too, but
    // never let them squeeze the main loop below a quarter of the width.
    float longest = juce::jmax (timeL, timeR), furthest = 0.0f;

    for (int i = 0; i < numHeads; ++i)
    {
        const auto& h = heads[(size_t) i];

        if (h.kind == VisualData::headMain || h.kind == VisualData::headTap)
            longest = juce::jmax (longest, h.delayMs + chain);
        else if ((h.kind == VisualData::headGhost || h.kind == VisualData::headCollapse) && h.gain > 0.03f)
            furthest = juce::jmax (furthest, h.delayMs + chain);
    }

    longest = juce::jmax (longest, juce::jmin (furthest, longest * 4.0f));
    const float target = juce::jlimit (30.0f, 9800.0f, longest * 1.24f);
    viewMs += (target - viewMs) * (1.0f - std::exp (-dt / 0.16f));

    flow = std::fmod (flow + dt * (0.25f + 0.9f * juce::jmin (1.2f, fb)), 1.0f);

    const float span = juce::jmax (hopMs, viewMs * 5.0f / juce::jmax (1.0f, tapeX1 - tapeX0));
    const float gl = std::sqrt (juce::jmin (1.0f, envelopeAt (vis.ringL, juce::jmax (0.0f, timeL - chainL), span)));
    const float gr = std::sqrt (juce::jmin (1.0f, envelopeAt (vis.ringR, juce::jmax (0.0f, timeR - chainR), span)));
    glowL += (gl - glowL) * (gl > glowL ? 0.7f : 0.25f);
    glowR += (gr - glowR) * (gr > glowR ? 0.7f : 0.25f);

    const float rec = std::sqrt (juce::jmin (1.0f, envelopeAt (vis.ringIn, 0.0f, 30.0f)));
    recGlow += (rec - recGlow) * (rec > recGlow ? 0.8f : 0.2f);

    // radar
    const float px = 0.5f * (vis.panL.load (std::memory_order_relaxed) + vis.panR.load (std::memory_order_relaxed));
    const float py = vis.orbitY.load (std::memory_order_relaxed);
    trail[(size_t) trailPos] = { px, py };
    trailPos = (trailPos + 1) % (int) trail.size();

    const float wet = juce::jmax (vis.wetPeakL.exchange (0.0f, std::memory_order_relaxed),
                                  vis.wetPeakR.exchange (0.0f, std::memory_order_relaxed));
    blipCooldown -= dt;

    if (wet > 0.015f && wet > wetSmooth * 1.5f && blipCooldown <= 0.0f)
    {
        blips[(size_t) blipPos] = { px, py, 1.0f, juce::jlimit (0.35f, 1.0f, std::sqrt (wet) * 1.4f) };
        blipPos = (blipPos + 1) % (int) blips.size();
        blipCooldown = 0.06f;
    }

    wetSmooth += (wet - wetSmooth) * (wet > wetSmooth ? 0.5f : 0.12f);

    for (auto& b : blips)
        b.life = juce::jmax (0.0f, b.life - dt / 1.6f);

    repaint();
}

// ============================================================================
void EchoVisualizer::paintBackground (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();

    g.fillAll (col::bg);

    // bezel
    g.setColour (juce::Colours::black);
    g.fillRoundedRectangle (b, 9.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3e44), 0.0f, b.getY(), juce::Colour (0xff141618), 0.0f, b.getBottom(), false));
    g.fillRoundedRectangle (b.reduced (1.0f), 8.0f);
    g.setColour (juce::Colours::black.withAlpha (0.9f));
    g.fillRoundedRectangle (screen.expanded (2.5f), 7.5f);

    drawScreen (g, screen, 6.0f);

    juce::Graphics::ScopedSaveState state (g);
    juce::Path clip;
    clip.addRoundedRectangle (screen, 6.0f);
    g.reduceClipRegion (clip);

    // faint phosphor wash and graticule
    g.setGradientFill (juce::ColourGradient (juce::Colour (0x18ffb03a), screen.getCentreX(), screen.getCentreY(),
                                             juce::Colours::transparentBlack, screen.getX(), screen.getY(), true));
    g.fillRect (screen);

    g.setColour (col::cyan.withAlpha (0.035f));
    for (float x = screen.getX() + 8.0f; x < screen.getRight(); x += 16.0f)
        g.drawVerticalLine (juce::roundToInt (x), screen.getY(), screen.getBottom());
    for (float y = screen.getY() + 8.0f; y < screen.getBottom(); y += 16.0f)
        g.drawHorizontalLine (juce::roundToInt (y), screen.getX(), screen.getRight());
}

void EchoVisualizer::paintOverlay (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();

    {
        juce::Graphics::ScopedSaveState state (g);
        juce::Path clip;
        clip.addRoundedRectangle (screen, 6.0f);
        g.reduceClipRegion (clip);

        // CRT: scan lines, vignette, glass
        g.setColour (juce::Colours::black.withAlpha (0.13f));
        for (float y = screen.getY(); y < screen.getBottom(); y += 3.0f)
            g.fillRect (screen.getX(), y, screen.getWidth(), 1.0f);

        const float v = 46.0f;
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.5f), screen.getX(), 0.0f, juce::Colours::transparentBlack, screen.getX() + v, 0.0f, false));
        g.fillRect (screen.withWidth (v));
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.5f), screen.getRight(), 0.0f, juce::Colours::transparentBlack, screen.getRight() - v, 0.0f, false));
        g.fillRect (screen.withLeft (screen.getRight() - v));
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.45f), 0.0f, screen.getBottom(), juce::Colours::transparentBlack, 0.0f, screen.getBottom() - v, false));
        g.fillRect (screen.withTop (screen.getBottom() - v));

        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.045f), screen.getX(), screen.getY(),
                                                 juce::Colours::transparentWhite, screen.getX() + screen.getWidth() * 0.4f, screen.getY() + screen.getHeight() * 0.7f, false));
        g.fillRect (screen);
    }

    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawRoundedRectangle (b.reduced (1.0f), 8.0f, 0.8f);
}

/** The bezel, glass and CRT texture never change, so they are rendered once per size at
    the display's real pixel density and blitted; only the tape and the radar are redrawn. */
void EchoVisualizer::ensureLayers (float scale)
{
    const int w = juce::jmax (1, (int) std::ceil ((float) getWidth() * scale));
    const int h = juce::jmax (1, (int) std::ceil ((float) getHeight() * scale));

    if (backgroundLayer.isValid() && backgroundLayer.getWidth() == w && backgroundLayer.getHeight() == h
         && std::abs (scale - layerScale) < 1.0e-4f)
        return;

    layerScale = scale;
    backgroundLayer = juce::Image (juce::Image::RGB, w, h, true);
    overlayLayer    = juce::Image (juce::Image::ARGB, w, h, true);

    {
        juce::Graphics g (backgroundLayer);
        g.addTransform (juce::AffineTransform::scale (scale));
        paintBackground (g);
    }
    {
        juce::Graphics g (overlayLayer);
        g.addTransform (juce::AffineTransform::scale (scale));
        paintOverlay (g);
    }
}

void EchoVisualizer::paint (juce::Graphics& g)
{
    const float scale = juce::jlimit (0.25f, 8.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    ensureLayers (scale);

    const auto blit = [&] (const juce::Image& layer)
    {
        juce::Graphics::ScopedSaveState state (g);
        g.addTransform (juce::AffineTransform::scale (1.0f / layerScale));
        g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
        g.drawImageAt (layer, 0, 0);
    };

    blit (backgroundLayer);

    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (screen.toNearestInt());
        paintStatus (g);
        paintTape (g);
        paintRadar (g);
    }

    blit (overlayLayer);
}

// ============================================================================
void EchoVisualizer::paintStatus (juce::Graphics& g)
{
    const float timeL = vis.timeMsL.load (std::memory_order_relaxed);
    const float timeR = vis.timeMsR.load (std::memory_order_relaxed);
    const float fb    = vis.feedback.load (std::memory_order_relaxed);
    const float bpm   = vis.bpm.load (std::memory_order_relaxed);
    const float pitch = vis.pitchSt.load (std::memory_order_relaxed);
    const bool frozen = vis.freeze.load (std::memory_order_relaxed) > 0.5f;
    const int mode    = vis.mode.load (std::memory_order_relaxed);

    auto r = statusBar;
    const auto titleFont = font (FontKind::bold, 13.0f, 0.2f);
    const juce::String title = modeInfo (mode).name;

    drawGlowText (g, title, r.removeFromLeft (textWidth (titleFont, title) + 10.0f), juce::Justification::centredLeft, titleFont, col::cream);

    const auto f = monoFont (10.5f);
    const auto item = [&] (const juce::String& key, const juce::String& value, juce::Colour c)
    {
        g.setFont (f);
        g.setColour (col::creamFaint);
        const float kw = textWidth (f, key) + 5.0f;
        g.drawText (key, r.removeFromLeft (kw), juce::Justification::centredLeft, false);
        const float vw = textWidth (f, value) + 14.0f;
        drawGlowText (g, value, r.removeFromLeft (vw), juce::Justification::centredLeft, f, c);
    };

    r.removeFromLeft (6.0f);
    item ("TIME", std::abs (timeL - timeR) > 0.6f ? formatMs (timeL) + " / " + formatMs (timeR) : formatMs (timeL), col::amber);
    item ("FB", juce::String (juce::roundToInt (fb * 100.0f)) + "%", fb > 1.0f ? col::red : col::amber);
    item ("BPM", juce::String (bpm, 1), col::amber);

    if (std::abs (pitch) > 0.01f)
        item ("PITCH", (pitch > 0.0f ? "+" : "") + juce::String (pitch, 1), col::cyan);

    if (frozen)
        drawGlowText (g, "FROZEN", statusBar, juce::Justification::centredRight, font (FontKind::bold, 12.0f, 0.2f), col::cyan);
    else if (pInfinite->load() > 0.5f)
        drawGlowText (g, "INFINITE", statusBar, juce::Justification::centredRight, font (FontKind::bold, 12.0f, 0.2f), col::orange);
}

// ============================================================================
void EchoVisualizer::paintTape (juce::Graphics& g)
{
    const float w = tapeX1 - tapeX0;
    if (w < 20.0f)
        return;

    const float timeL = vis.timeMsL.load (std::memory_order_relaxed);
    const float timeR = vis.timeMsR.load (std::memory_order_relaxed);
    const float fb    = vis.feedback.load (std::memory_order_relaxed);
    const float pitch = vis.pitchSt.load (std::memory_order_relaxed);
    const bool frozen = vis.freeze.load (std::memory_order_relaxed) > 0.5f;

    // The horizontal axis is echo time. Sound first spends a moment inside the processing
    // chain (diffusion, pitch shifter, spectral frames...) before it lands on the tape, so
    // the tape starts that much to the right of the REC head and a head's position reads
    // as the time at which you hear it.
    const float chain = 0.5f * (chainL + chainR);

    // find the main heads (the engine reports their modulated positions on the tape)
    float headL = juce::jmax (0.0f, timeL - chainL), headR = juce::jmax (0.0f, timeR - chainR);
    bool hasMain = false;
    for (int i = 0; i < numHeads; ++i)
    {
        if (heads[(size_t) i].kind == VisualData::headMain)
        {
            (heads[(size_t) i].pan < 0.0f ? headL : headR) = heads[(size_t) i].delayMs;
            hasMain = true;
        }
    }

    const float railH = 22.0f, rulerH = 15.0f, gap = 7.0f;
    const float laneH = (field.getHeight() - railH - rulerH - gap) * 0.5f;
    const auto laneL = juce::Rectangle<float> (tapeX0, field.getY() + railH, w, laneH);
    const auto laneR = laneL.translated (0.0f, laneH + gap);
    const auto tapeColour = frozen ? col::cyan : col::amber;

    // ---- lanes
    const auto drawLane = [&] (juce::Rectangle<float> lane, const std::array<std::atomic<float>, (size_t) VisualData::kRingSize>& ring,
                               float headMs, float chainMs, const char* name)
    {
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRect (lane);
        g.setColour (col::creamFaint.withAlpha (0.28f));
        g.drawHorizontalLine (juce::roundToInt (lane.getCentreY()), lane.getX(), lane.getRight());
        g.setColour (col::creamFaint.withAlpha (0.18f));
        g.drawHorizontalLine (juce::roundToInt (lane.getY()), lane.getX(), lane.getRight());
        g.drawHorizontalLine (juce::roundToInt (lane.getBottom()), lane.getX(), lane.getRight());

        g.setFont (font (FontKind::bold, 11.0f, 0.1f));
        g.setColour (col::creamDim);
        g.drawText (name, juce::Rectangle<float> (field.getX(), lane.getCentreY() - 7.0f, 18.0f, 14.0f), juce::Justification::centred, false);

        constexpr float step = 2.0f;
        const int n = (int) (w / step);
        const float spanMs = viewMs * step / w;
        const float cy = lane.getCentreY(), maxHalf = lane.getHeight() * 0.5f - 2.0f;

        juce::Path top, body;
        std::vector<float> halves ((size_t) n + 1);

        for (int i = 0; i <= n; ++i)
        {
            const float onTape = viewMs * (float) i * step / w - chainMs;       // < 0: still inside the chain
            const float env = onTape >= 0.0f ? envelopeAt (ring, onTape, spanMs) : 0.0f;
            halves[(size_t) i] = juce::jmax (0.5f, std::pow (juce::jmin (1.0f, env), 0.55f) * maxHalf);
        }

        // the stretch where the sound is still inside the processing chain
        const float chainW = w * chainMs / juce::jmax (1.0f, viewMs);

        if (chainW > 5.0f)
        {
            const auto zone = lane.withWidth (juce::jmin (chainW, w));
            g.setColour (col::cyan.withAlpha (0.05f));
            g.fillRect (zone);
            g.setColour (col::cyan.withAlpha (0.16f));
            for (float x = zone.getX() + 3.0f; x < zone.getRight(); x += 5.0f)
                g.drawLine (x, zone.getBottom() - 2.0f, juce::jmin (zone.getRight(), x + 6.0f), zone.getBottom() - 2.0f - juce::jmin (6.0f, zone.getRight() - x), 0.8f);
            g.drawVerticalLine (juce::roundToInt (zone.getRight()), zone.getY(), zone.getBottom());

            if (chainW > 44.0f)
            {
                g.setFont (font (FontKind::semiBold, 7.5f, 0.18f));
                g.setColour (col::cyan.withAlpha (0.55f));
                g.drawText ("IN THE CHAIN", zone.reduced (4.0f, 2.0f).withHeight (9.0f), juce::Justification::centredLeft, false);
            }
        }

        // the tape itself starts where the chain ends
        const int first = juce::jlimit (0, n, (int) std::ceil (w * chainMs / juce::jmax (1.0f, viewMs) / step));

        body.startNewSubPath (lane.getX() + (float) first * step, cy - halves[(size_t) first]);
        top.startNewSubPath (lane.getX() + (float) first * step, cy - halves[(size_t) first]);

        for (int i = first + 1; i <= n; ++i)
        {
            body.lineTo (lane.getX() + (float) i * step, cy - halves[(size_t) i]);
            top.lineTo (lane.getX() + (float) i * step, cy - halves[(size_t) i]);
        }

        for (int i = n; i >= first; --i)
            body.lineTo (lane.getX() + (float) i * step, cy + halves[(size_t) i]);

        body.closeSubPath();

        // bright up to the play head, dim once it has been heard
        const float headProp = juce::jlimit (0.002f, 0.996f, (headMs + chainMs) / juce::jmax (1.0f, viewMs));
        juce::ColourGradient grad (tapeColour.withAlpha (0.78f), lane.getX(), 0.0f, tapeColour.withAlpha (0.10f), lane.getRight(), 0.0f, false);
        grad.addColour (headProp, tapeColour.withAlpha (0.55f));
        grad.addColour (juce::jmin (0.999f, headProp + 0.003f), tapeColour.withAlpha (0.16f));
        g.setGradientFill (grad);
        g.fillPath (body);

        g.setColour (tapeColour.withAlpha (0.55f));
        g.strokePath (top, juce::PathStrokeType (1.0f));
    };

    drawLane (laneL, vis.ringL, headL, chainL, "L");
    drawLane (laneR, vis.ringR, headR, chainR, "R");

    // ---- ruler
    {
        static constexpr float steps[] = { 5.0f, 10.0f, 25.0f, 50.0f, 100.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f };
        float stepMs = steps[9];
        for (const float s : steps)
            if (viewMs / s <= 8.0f) { stepMs = s; break; }

        const float y = laneR.getBottom() + 3.0f;
        g.setFont (monoFont (8.5f));

        for (float ms = 0.0f; ms <= viewMs; ms += stepMs)
        {
            const float x = xForDelay (ms);
            g.setColour (col::creamFaint.withAlpha (0.8f));
            g.drawLine (x, y, x, y + 4.0f, 1.0f);

            if (ms > 0.0f && x < tapeX1 - 12.0f)
            {
                g.setColour (col::creamFaint);
                g.drawText (ms >= 1000.0f ? juce::String (ms / 1000.0f, 1) + "s" : juce::String (juce::roundToInt (ms)),
                            juce::Rectangle<float> (x - 20.0f, y + 4.0f, 40.0f, 9.0f), juce::Justification::centred, false);
            }
        }

        // tempo grid: a diamond on every quarter note
        const float beatMs = 60000.0f / juce::jmax (20.0f, vis.bpm.load (std::memory_order_relaxed));
        if (viewMs / beatMs <= 24.0f)
        {
            g.setColour (col::cyan.withAlpha (0.5f));
            for (float ms = beatMs; ms <= viewMs; ms += beatMs)
            {
                const float x = xForDelay (ms);
                juce::Path d;
                d.addQuadrilateral (x, y - 2.6f, x + 2.2f, y, x, y + 2.6f, x - 2.2f, y);
                g.fillPath (d);
            }
        }
    }

    // ---- secondary heads
    for (int i = 0; i < numHeads; ++i)
    {
        const auto& h = heads[(size_t) i];
        if (h.kind == VisualData::headMain)
            continue;

        const float x = xForDelay (h.delayMs + chain);
        if (x < tapeX0 || x > tapeX1)
            continue;

        const float yPan = juce::jmap (juce::jlimit (-1.0f, 1.0f, h.pan), -1.0f, 1.0f, laneL.getCentreY(), laneR.getCentreY());
        const float a = juce::jlimit (0.0f, 1.0f, h.gain);

        switch (h.kind)
        {
            case VisualData::headTap:
            {
                g.setColour (col::cream.withAlpha (0.25f + 0.45f * a));
                g.drawLine (x, laneL.getY(), x, laneR.getBottom(), 1.0f);
                juce::Path d;
                d.addQuadrilateral (x, yPan - 4.5f, x + 4.0f, yPan, x, yPan + 4.5f, x - 4.0f, yPan);
                g.setColour (col::cream.withAlpha (0.5f + 0.5f * a));
                g.fillPath (d);
                break;
            }

            case VisualData::headCollapse:
            {
                g.setColour (col::orange.withAlpha (0.2f + 0.6f * a));
                for (float y = laneL.getY(); y < laneR.getBottom(); y += 5.0f)
                    g.drawLine (x, y, x, juce::jmin (laneR.getBottom(), y + 2.5f), 1.0f);
                g.fillEllipse (x - 2.6f, yPan - 2.6f, 5.2f, 5.2f);
                break;
            }

            case VisualData::headGhost:
            {
                g.setColour (col::cyan.withAlpha (0.25f + 0.3f * a));
                for (float y = laneL.getY(); y < laneR.getBottom(); y += 4.0f)
                    g.fillRect (x - 0.5f, y, 1.0f, 1.4f);
                const float wob = 2.5f * std::sin (time * 2.3f + (float) i);
                g.setColour (col::cyan.withAlpha (0.18f));
                g.fillEllipse (x - 8.0f, yPan + wob - 8.0f, 16.0f, 16.0f);
                g.setColour (col::cyan.withAlpha (0.75f));
                g.drawEllipse (x - 3.5f, yPan + wob - 3.5f, 7.0f, 7.0f, 1.0f);
                break;
            }

            case VisualData::headReverse:
            {
                g.setColour (col::cyan.withAlpha (0.4f + 0.5f * a));
                g.drawLine (x, laneL.getY() - 2.0f, x, laneR.getBottom() + 2.0f, 1.2f);
                juce::Path arrow;
                arrow.addTriangle (x - 6.0f, laneL.getY() - 6.0f, x, laneL.getY() - 10.0f, x, laneL.getY() - 2.0f);
                g.fillPath (arrow);
                break;
            }

            case VisualData::headGrain:
            {
                const float len = 3.0f + 12.0f * a;
                g.setColour (col::cyan.withAlpha (0.35f + 0.6f * a));
                g.drawLine (x, yPan - len, x, yPan + len, 1.6f);
                break;
            }

            case VisualData::headSlice:
            {
                g.setColour (col::cyan.withAlpha (0.4f + 0.5f * a));
                g.drawLine (x, laneL.getY() - 2.0f, x, laneR.getBottom() + 2.0f, 1.4f);
                g.drawLine (x - 4.0f, laneL.getY() - 2.0f, x + 4.0f, laneL.getY() - 2.0f, 1.4f);
                g.drawLine (x - 4.0f, laneR.getBottom() + 2.0f, x + 4.0f, laneR.getBottom() + 2.0f, 1.4f);
                break;
            }

            default: break;
        }
    }

    // ---- REC head
    {
        const float x = tapeX0;
        const auto recColour = frozen ? col::cyan : col::red;
        g.setColour (col::cream.withAlpha (0.85f));
        g.drawLine (x, laneL.getY() - 3.0f, x, laneR.getBottom() + 3.0f, 1.6f);
        drawLed (g, { field.getX() + 6.0f, field.getY() + 6.0f }, 2.6f, recColour, frozen ? 1.0f : 0.25f + 0.75f * recGlow);
        g.setFont (font (FontKind::semiBold, 8.5f, 0.14f));
        g.setColour (col::creamDim);
        g.drawText (frozen ? "HOLD" : "REC", juce::Rectangle<float> (field.getX() + 11.0f, field.getY() + 1.0f, 26.0f, 10.0f), juce::Justification::centredLeft, false);
    }

    // ---- PLAY heads
    const auto drawPlayHead = [&] (float x, juce::Rectangle<float> lane, float glow, bool pointerOnTop)
    {
        if (x < tapeX0 || x > tapeX1 + 1.0f)
            return;

        if (glow > 0.02f)
        {
            const float r = 10.0f + 26.0f * glow;
            g.setGradientFill (juce::ColourGradient (tapeColour.withAlpha (0.55f * glow), x, lane.getCentreY(),
                                                     tapeColour.withAlpha (0.0f), x + r, lane.getCentreY(), true));
            g.fillEllipse (x - r, lane.getCentreY() - r, 2.0f * r, 2.0f * r);
        }

        g.setColour (col::cream.withAlpha (0.9f));
        g.drawLine (x, lane.getY() - 2.0f, x, lane.getBottom() + 2.0f, 1.6f);

        juce::Path p;
        if (pointerOnTop)   p.addTriangle (x - 4.5f, lane.getY() - 8.0f, x + 4.5f, lane.getY() - 8.0f, x, lane.getY() - 1.5f);
        else                p.addTriangle (x - 4.5f, lane.getBottom() + 8.0f, x + 4.5f, lane.getBottom() + 8.0f, x, lane.getBottom() + 1.5f);
        g.fillPath (p);
    };

    const float xL = xForDelay (headL + chainL), xR = xForDelay (headR + chainR);

    if (hasMain)
    {
        drawPlayHead (xL, laneL, glowL, true);
        drawPlayHead (xR, laneR, glowR, false);
    }

    // ---- feedback rail: carries the sound from the play head back to the record head
    {
        const float xh = juce::jlimit (tapeX0 + 12.0f, tapeX1, hasMain ? xL : xForDelay (juce::jmax (timeL, timeR)));
        const float railY = field.getY() + 4.0f;
        const float yTop = laneL.getY() - 9.0f;
        const float amount = juce::jlimit (0.0f, 1.25f, fb);
        const auto railColour = col::amber.interpolatedWith (col::cyan, juce::jlimit (0.0f, 1.0f, std::abs (pitch) / 12.0f));

        juce::Path rail;
        rail.startNewSubPath (xh, yTop);
        rail.lineTo (xh, railY);
        rail.lineTo (tapeX0 + 34.0f, railY);
        g.setColour (railColour.withAlpha (0.12f + 0.3f * juce::jmin (1.0f, amount)));
        g.strokePath (rail, juce::PathStrokeType (1.0f + 1.6f * juce::jmin (1.0f, amount), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // travelling pulses, brighter and closer together with more feedback
        if (amount > 0.01f)
        {
            const float length = (xh - tapeX0 - 34.0f) + (yTop - railY);
            const float spacing = 26.0f;
            const int count = (int) (length / spacing) + 1;

            for (int k = 0; k < count; ++k)
            {
                const float dist = ((float) k + flow) * spacing;
                if (dist > length) continue;

                juce::Point<float> pt;
                if (dist < yTop - railY)    pt = { xh, yTop - dist };
                else                        pt = { xh - (dist - (yTop - railY)), railY };

                const float fade = juce::jlimit (0.0f, 1.0f, std::pow (juce::jmin (1.0f, amount), dist / juce::jmax (1.0f, length) * 2.0f + 0.2f));
                g.setColour (railColour.withAlpha (0.25f + 0.7f * fade));
                g.fillEllipse (pt.x - 1.8f, pt.y - 1.8f, 3.6f, 3.6f);
            }
        }

        g.setFont (monoFont (8.5f));
        g.setColour (railColour.withAlpha (0.8f));
        juce::String text = "FEEDBACK " + juce::String (juce::roundToInt (fb * 100.0f)) + "%";
        if (std::abs (pitch) > 0.01f)
            text << "  " << (pitch > 0.0f ? "+" : "") << juce::String (pitch, 1) << " st / pass";
        g.drawText (text, juce::Rectangle<float> (tapeX0 + 36.0f, railY + 4.5f, 220.0f, 9.0f), juce::Justification::centredLeft, false);
    }

    if (hoverTape || drag == Drag::tape)
    {
        g.setFont (font (FontKind::semiBold, 9.0f, 0.16f));
        g.setColour (col::cream.withAlpha (0.6f));
        g.drawText ("DRAG SIDEWAYS: TIME    UP / DOWN: FEEDBACK",
                    juce::Rectangle<float> (tapeX1 - 240.0f, field.getBottom() - 30.0f, 236.0f, 10.0f), juce::Justification::centredRight, false);
    }
}

// ============================================================================
void EchoVisualizer::paintRadar (juce::Graphics& g)
{
    const float captionH = 15.0f;
    const auto box = radarBox;
    const float R = juce::jmin (box.getWidth(), box.getHeight() - captionH - 12.0f) * 0.5f - 8.0f;
    const juce::Point<float> c (box.getCentreX(), box.getY() + 12.0f + R + 4.0f);

    if (R < 10.0f)
        return;

    const float amount = pOrbit->load() * 0.01f;
    const int shape = juce::roundToInt (pOrbitShape->load());
    const bool active = amount > 0.005f;

    // ---- graticule
    g.setColour (juce::Colours::black.withAlpha (0.3f));
    g.fillEllipse (c.x - R, c.y - R, 2.0f * R, 2.0f * R);

    for (int i = 1; i <= 3; ++i)
    {
        const float r = R * (float) i / 3.0f;
        g.setColour (col::cyan.withAlpha (i == 3 ? 0.42f : 0.14f));
        g.drawEllipse (c.x - r, c.y - r, 2.0f * r, 2.0f * r, i == 3 ? 1.2f : 0.8f);
    }

    g.setColour (col::cyan.withAlpha (0.14f));
    g.drawLine (c.x - R, c.y, c.x + R, c.y, 0.8f);
    g.drawLine (c.x, c.y - R, c.x, c.y + R, 0.8f);

    for (int i = 0; i < 24; ++i)
    {
        const float a = (float) i / 24.0f * 2.0f * kPiF;
        const float r0 = R * ((i % 6) == 0 ? 0.93f : 0.965f);
        g.setColour (col::cyan.withAlpha (0.4f));
        g.drawLine (c.x + std::sin (a) * r0, c.y - std::cos (a) * r0, c.x + std::sin (a) * R, c.y - std::cos (a) * R, 0.8f);
    }

    g.setFont (font (FontKind::bold, 10.0f, 0.1f));
    g.setColour (col::creamDim);
    g.drawText ("L", juce::Rectangle<float> (c.x - R - 13.0f, c.y - 6.0f, 10.0f, 12.0f), juce::Justification::centred, false);
    g.drawText ("R", juce::Rectangle<float> (c.x + R + 3.0f, c.y - 6.0f, 10.0f, 12.0f), juce::Justification::centred, false);

    g.setFont (font (FontKind::semiBold, 7.5f, 0.16f));
    g.setColour (col::creamFaint);
    g.drawText ("NEAR", juce::Rectangle<float> (c.x - 20.0f, c.y - R + 3.0f, 40.0f, 8.0f), juce::Justification::centred, false);
    g.drawText ("FAR", juce::Rectangle<float> (c.x - 20.0f, c.y + R - 11.0f, 40.0f, 8.0f), juce::Justification::centred, false);

    const auto toScreen = [&] (juce::Point<float> p) { return juce::Point<float> (c.x + juce::jlimit (-1.0f, 1.0f, p.x) * R * 0.94f,
                                                                                    c.y - juce::jlimit (-1.0f, 1.0f, p.y) * R * 0.94f); };

    // ---- the path the echoes travel
    if (active)
    {
        if (shape == (int) OrbitShape::scatter)
        {
            for (int i = 0; i < 16; ++i)
            {
                const auto p = toScreen (orbitPoint (shape, ((float) i + 0.5f) / 16.0f, amount));
                g.setColour (col::cyan.withAlpha (0.35f));
                g.drawEllipse (p.x - 2.0f, p.y - 2.0f, 4.0f, 4.0f, 0.9f);
            }
        }
        else
        {
            const int points = shape == (int) OrbitShape::spiral ? 420 : 96;
            juce::Path path;

            for (int i = 0; i <= points; ++i)
            {
                const auto p = toScreen (orbitPoint (shape, (float) i / (float) points, amount));
                if (i == 0) path.startNewSubPath (p); else path.lineTo (p);
            }

            g.setColour (col::cyan.withAlpha (0.16f));
            g.strokePath (path, juce::PathStrokeType (3.5f));
            g.setColour (col::cyan.withAlpha (0.55f));
            g.strokePath (path, juce::PathStrokeType (1.0f));
        }
    }

    // ---- echo blips: every repeat leaves a ring where it sounded
    for (const auto& b : blips)
    {
        if (b.life <= 0.0f) continue;

        const auto p = toScreen ({ b.x, b.y });
        const float r = 2.5f + (1.0f - b.life) * 15.0f * b.size;
        g.setColour (col::amber.withAlpha (0.75f * b.life * b.life));
        g.drawEllipse (p.x - r, p.y - r, 2.0f * r, 2.0f * r, 1.2f);
        g.setColour (col::amber.withAlpha (0.5f * b.life));
        g.fillEllipse (p.x - 1.6f, p.y - 1.6f, 3.2f, 3.2f);
    }

    // ---- trail and current position
    for (int i = 0; i < (int) trail.size(); ++i)
    {
        const int idx = (trailPos + i) % (int) trail.size();
        const float age = (float) i / (float) trail.size();           // 0 = oldest
        const auto p = toScreen (trail[(size_t) idx]);
        const float r = 0.6f + 2.2f * age;
        g.setColour (col::cream.withAlpha (0.04f + 0.5f * age * age));
        g.fillEllipse (p.x - r, p.y - r, 2.0f * r, 2.0f * r);
    }

    {
        const auto p = toScreen (trail[(size_t) ((trailPos + (int) trail.size() - 1) % (int) trail.size())]);
        const float pulse = 0.5f + 0.5f * juce::jmin (1.0f, wetSmooth * 3.0f);
        g.setGradientFill (juce::ColourGradient (col::amber.withAlpha (0.6f * pulse), p.x, p.y, col::amber.withAlpha (0.0f), p.x + 16.0f, p.y, true));
        g.fillEllipse (p.x - 16.0f, p.y - 16.0f, 32.0f, 32.0f);
        g.setColour (col::cream);
        g.fillEllipse (p.x - 3.2f, p.y - 3.2f, 6.4f, 6.4f);
    }

    // ---- legend
    g.setFont (font (FontKind::semiBold, 9.5f, 0.18f));
    g.setColour (active ? col::cyan : col::creamFaint);
    g.drawText ("ECHO ORBIT", juce::Rectangle<float> (box.getX(), box.getY(), box.getWidth(), 10.0f), juce::Justification::centredLeft, false);

    juce::String caption = orbitShapeNames()[juce::jlimit (0, 4, shape)];
    if (pOrbitSync->load() > 0.5f)
        caption << "  " << kDivisions[(size_t) juce::jlimit (0, kNumDivisions - 1, juce::roundToInt (pOrbitDiv->load()))].name;
    else
        caption << "  " << juce::String (pOrbitRate->load(), 2) << " Hz";

    g.setFont (monoFont (9.0f));
    g.setColour (active ? col::cyan.withAlpha (0.9f) : col::creamFaint);
    g.drawText (active ? caption : juce::String ("OFF  -  DRAG TO ENGAGE"),
                juce::Rectangle<float> (box.getX(), box.getBottom() - captionH + 2.0f, box.getWidth(), 11.0f), juce::Justification::centred, false);
}

// ============================================================================
//  interaction
// ============================================================================
void EchoVisualizer::setParam (const juce::String& paramId, float realValue)
{
    if (auto* p = proc.apvts.getParameter (paramId))
        p->setValueNotifyingHost (p->convertTo0to1 (realValue));
}

void EchoVisualizer::gesture (const juce::String& paramId, bool begin)
{
    if (auto* p = proc.apvts.getParameter (paramId))
    {
        if (begin)
        {
            proc.undoManager.beginNewTransaction();
            p->beginChangeGesture();
        }
        else
        {
            p->endChangeGesture();
        }
    }
}

void EchoVisualizer::mouseMove (const juce::MouseEvent& e)
{
    const bool overTape  = field.contains (e.position);
    const bool overRadar = radarBox.contains (e.position);

    if (overTape != hoverTape || overRadar != hoverRadar)
    {
        hoverTape = overTape;
        hoverRadar = overRadar;
        setMouseCursor (overTape ? juce::MouseCursor::DraggingHandCursor
                                 : (overRadar ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::NormalCursor));
    }
}

void EchoVisualizer::mouseDown (const juce::MouseEvent& e)
{
    if (radarBox.contains (e.position))
    {
        drag = Drag::orbit;
        gesture (id::orbit, true);
        mouseDrag (e);
    }
    else if (field.contains (e.position))
    {
        drag = Drag::tape;
        dragSynced        = pSync->load() > 0.5f;
        dragStartTime     = pTime->load();
        dragStartDivision = juce::roundToInt (pDivision->load());
        dragStartFeedback = pFeedback->load();

        gesture (dragSynced ? id::division : id::time, true);
        if (auto* p = proc.apvts.getParameter (id::feedback))
            p->beginChangeGesture();
    }
}

void EchoVisualizer::mouseDrag (const juce::MouseEvent& e)
{
    if (drag == Drag::orbit)
    {
        const float captionH = 15.0f;
        const float R = juce::jmin (radarBox.getWidth(), radarBox.getHeight() - captionH - 12.0f) * 0.5f - 8.0f;
        const juce::Point<float> c (radarBox.getCentreX(), radarBox.getY() + 12.0f + R + 4.0f);
        setParam (id::orbit, juce::jlimit (0.0f, 100.0f, 100.0f * e.position.getDistanceFrom (c) / juce::jmax (1.0f, R * 0.94f)));
    }
    else if (drag == Drag::tape)
    {
        const float dx = (float) e.getDistanceFromDragStartX();
        const float dy = (float) e.getDistanceFromDragStartY();

        if (dragSynced)
            setParam (id::division, (float) juce::jlimit (0, (int) Div::bar2, dragStartDivision + juce::roundToInt (dx / 34.0f)));
        else
            setParam (id::time, juce::jlimit (1.0f, 4000.0f, dragStartTime * std::exp2 (dx / 170.0f)));

        setParam (id::feedback, juce::jlimit (0.0f, 125.0f, dragStartFeedback - dy * 0.45f));
    }
}

void EchoVisualizer::mouseUp (const juce::MouseEvent&)
{
    if (drag == Drag::orbit)
    {
        gesture (id::orbit, false);
    }
    else if (drag == Drag::tape)
    {
        gesture (dragSynced ? id::division : id::time, false);
        if (auto* p = proc.apvts.getParameter (id::feedback))
            p->endChangeGesture();
    }

    drag = Drag::none;
}

void EchoVisualizer::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (radarBox.contains (e.position))
    {
        const int shape = juce::roundToInt (pOrbitShape->load());
        gesture (id::orbitShape, true);
        setParam (id::orbitShape, (float) ((shape + 1) % (int) OrbitShape::numShapes));
        gesture (id::orbitShape, false);

        if (pOrbit->load() < 1.0f)
        {
            gesture (id::orbit, true);
            setParam (id::orbit, 60.0f);
            gesture (id::orbit, false);
        }
    }
}

void EchoVisualizer::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const float delta = wheel.deltaY * (wheel.isReversed ? -1.0f : 1.0f);

    if (radarBox.contains (e.position))
    {
        const float rate = pOrbitRate->load();
        gesture (id::orbitRate, true);
        setParam (id::orbitRate, juce::jlimit (0.01f, 10.0f, rate * std::exp2 (delta * 1.5f)));
        gesture (id::orbitRate, false);
    }
    else if (field.contains (e.position) && pSync->load() < 0.5f)
    {
        const float ms = pTime->load();
        gesture (id::time, true);
        setParam (id::time, juce::jlimit (1.0f, 4000.0f, ms * std::exp2 (delta * 0.5f)));
        gesture (id::time, false);
    }
}

} // namespace dlay::ui
