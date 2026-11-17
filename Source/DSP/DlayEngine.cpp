#include "DlayEngine.h"

namespace dlay
{

namespace
{
    constexpr double kSqrt2 = 1.4142135623730951;

    // all-pass stage lengths in milliseconds (mutually prime-ish, slightly different per channel)
    constexpr double kDiffBaseMs[2][4] = { { 4.31, 7.93, 12.73, 19.31 },
                                           { 4.79, 8.61, 13.37, 20.17 } };

    constexpr double kBigBaseMs[2][6]  = { { 11.3, 17.9, 26.7, 37.1, 53.3, 71.9 },
                                           { 12.1, 19.3, 28.1, 39.7, 56.9, 75.7 } };

    constexpr double kStereoRatios[7]  = { 0.5, 2.0 / 3.0, 0.75, 1.0, 4.0 / 3.0, 1.5, 2.0 };
    constexpr double kDualRatios[9]    = { 0.25, 1.0 / 3.0, 0.5, 2.0 / 3.0, 0.75, 1.0, 1.5, 2.0, 3.0 };

    constexpr int    kPatternLen[6]    = { 2, 4, 3, 4, 4, 4 };
    constexpr double kPatternPan[6][4] = { { -1.0,  1.0,  0.0,  0.0 },      // L R
                                           { -1.0,  0.0,  1.0,  0.0 },      // L C R C
                                           { -1.0,  1.0,  0.0,  0.0 },      // L R C
                                           { -1.0, -1.0,  1.0,  1.0 },      // L L R R
                                           { -1.0,  1.0,  1.0, -1.0 },      // L R R L
                                           {  0.0, -1.0,  0.0,  1.0 } };    // C L C R

    constexpr int    kSliceDivs[8]     = { 1, 2, 3, 4, 6, 8, 12, 16 };

    // stale tape blocks zero-filled per control tick and line (16 kB): a full erase is
    // finished in the background within about 45 ms at any sample rate
    constexpr int    kSweepBlocks      = 4;
    constexpr int    kHeadCombos[7]    = { 0b001, 0b010, 0b100, 0b011, 0b110, 0b101, 0b111 };

    inline int macroIndex (double v, int count) noexcept
    {
        return clampT ((int) (v * (double) count), 0, count - 1);
    }

    /** Unity at centre, +3 dB when hard-panned: keeps the power of a stereo tap constant. */
    inline void balanceGains (double pan, double& gl, double& gr) noexcept
    {
        panGains (pan, gl, gr);
        gl *= kSqrt2;
        gr *= kSqrt2;
    }
}

// ============================================================================
DlayEngine::DlayEngine()
{
    for (size_t i = 0; i < hann.size(); ++i)
    {
        const double s = std::sin (kPi * (double) i / (double) (hann.size() - 1));
        hann[i] = (float) (s * s);
    }
}

void DlayEngine::prepare (double sampleRate)
{
    sr = sampleRate > 1000.0 ? sampleRate : 48000.0;

    for (auto& l : lines)
        l.prepare ((int) (kMaxDelaySeconds * sr) + 256);

    maxDelay = std::min (lines[0].maxDelay(), kMaxDelaySeconds * sr) - 8.0;

    motion.prepare (sr, kTick);

    macroCoef = 1.0 - std::exp (-(double) kTick / (0.025 * sr));
    tapCoef   = 1.0 - std::exp (-1.0 / (0.003 * sr));
    multCoef  = 1.0 - std::exp (-1.0 / (0.03 * sr));

    for (int c = 0; c < 2; ++c)
    {
        diff[c].prepare (sr, 0.026);
        bigDiff[c].prepare (sr, 0.17);
        ghostDiff[c].prepare (sr, 0.03);
        ghostDiff[c].setLength (0, (c == 0 ? 0.0073 : 0.0081) * sr);
        ghostDiff[c].setLength (1, (c == 0 ? 0.0127 : 0.0113) * sr);
        ghostDiff[c].snapLengths();

        ps[c].prepare (sr);
        spec[c].prepare (sr, 0x77u + (uint32_t) c * 0x1234u);
        osLoop[c].prepare();
        osIn[c].prepare();

        dcBlock[c].setCutoff (8.0, sr);
        subsonic[c].setCutoff (10.0, sr);
        tiltLp[c].setCutoff (900.0, sr);
        snapHp[c].setCutoff (1500.0, sr);
    }

    shimmerHpC.set (120.0, 0.707, sr);

    osLatency[0] = 0.0;
    osLatency[1] = Oversampler::measureLatency (2);
    osLatency[2] = Oversampler::measureLatency (4);

    loopEnvCoef = 1.0 - std::exp (-1.0 / (0.25 * sr));
    duckAtk     = 1.0 - std::exp (-1.0 / (0.002 * sr));
    duckRel     = 1.0 - std::exp (-1.0 / (0.18 * sr));
    limRel      = 1.0 - std::exp (-1.0 / (0.1 * sr));
    noiseEnvRel = std::exp (-1.0 / (0.08 * sr));
    orbitSmooth = 1.0 - std::exp (-(double) kTick / (0.006 * sr));

    ghostFastA = 1.0 - std::exp (-1.0 / (0.004 * sr));
    ghostFastR = 1.0 - std::exp (-1.0 / (0.06 * sr));
    ghostSlowA = 1.0 - std::exp (-1.0 / (0.14 * sr));
    ghostSlowR = 1.0 - std::exp (-1.0 / (0.35 * sr));

    ringHop = std::max (16, (int) std::lround (sr / 750.0));
    visual.ringHopMs.store ((float) ((double) ringHop * 1000.0 / sr));

    rng.seed (0xC0FFEE11u);
    rngCtl.seed (0x5EED1234u);

    prepared = true;
    reset();
}

void DlayEngine::reset() noexcept
{
    if (! prepared)
        return;

    clearAudioState();
    motion.reset();

    tickPos      = 0;
    fadeState    = FadeState::steady;
    fadeLevel    = 1.0;
    fadeRamp.snap (1.0);
    snapNext     = true;
    snapOutput   = true;
    pendingReset = false;
    softResetFlag.store (false);
    hardResetFlag.store (false);
}

/*  Erases everything that sounds. Safe on the audio thread at any sample rate: the tape,
    the pitch shifters, the diffusers and the spectral history do not zero-fill their
    memory here - they mark it as empty and it reads as silence from then on (see
    DelayLine). What is left is a few kilobytes of filter state. */
void DlayEngine::clearAudioState() noexcept
{
    for (int c = 0; c < 2; ++c)
    {
        lines[c].clear();
        diff[c].reset();
        bigDiff[c].reset();
        ghostDiff[c].reset();
        ps[c].reset();
        spec[c].reset();
        osLoop[c].reset();
        osIn[c].reset();

        lpS[c].reset();   hpS[c].reset();
        shimmerHp[c].reset();
        dcBlock[c].reset();
        colorLp[c].reset();  colorLp2[c].reset();  colorHp[c].reset();
        tiltLp[c].reset();   memLp[c].reset();     depthLp[c].reset();
        subsonic[c].reset();
        snapHp[c].reset();
        bumpS[c].reset();
        bbdPre[c].reset();   bbdPost[c].reset();
        ghostS[c].reset();

        shPhase[c] = 0.0;  shHold[c] = 0.0;
        noiseEnv[c] = 0.0;
        ghostFast[c] = ghostSlow[c] = 0.0;
    }

    loopEnv  = 0.0;
    agcGain  = 1.0;
    agcInt   = 0.0;
    duckEnv  = 0.0;
    limGain  = 1.0;
    ringPkL = ringPkR = ringPkIn = 0.0;
    freezeLatched = false;
    healFlag = false;

    // tell the display that what it shows of the old tape is gone
    ringErased = visual.ringWrite.load (std::memory_order_relaxed);
    visual.ringErasedAt.store (ringErased, std::memory_order_relaxed);

    resetModeState();
}

void DlayEngine::resetModeState() noexcept
{
    for (auto& t : taps)   t = Tap();
    for (auto& t : ctaps)  t = Tap();
    for (auto& g : grains) g.active = false;

    grainCountdown = 0;

    rev[0].pos = 0.0;
    rev[1].pos = rev[1].len * 0.5;

    slicePos = 0.0;
    sliceAction = SliceAction::normal;
    sliceRepeats = 0;
    sliceIndex = 0;

    randomModeCounter = 1.0e12;        // forces a roll on the first tick
    for (int k = 0; k < kMaxTaps; ++k)
    {
        randomMult[k] = 1.0;
        randomPan[k]  = 0.0;
        randomGain[k] = 1.0;
    }

    for (int c = 0; c < 2; ++c)
    {
        bigDiff[c].reset();
        spec[c].reset();
        colorLp[c].reset();  colorLp2[c].reset();  colorHp[c].reset();
        bumpS[c].reset();
        bbdPre[c].reset();   bbdPost[c].reset();
        shPhase[c] = 0.0;    shHold[c] = 0.0;
    }
}

// ============================================================================
//  taps
// ============================================================================
void DlayEngine::setTap (int index, double mult, double gl, double gr, double fbWeight, double pan) noexcept
{
    auto& tp = taps[index];
    tp.glT = gl;
    tp.grT = gr;
    tp.fbT = fbWeight;
    tp.pan = pan;

    if (! tp.active)
    {
        tp.active = true;
        tp.mult = tp.multTarget = mult;
        tp.g = 0.0;
        tp.gl = gl;  tp.gr = gr;  tp.fb = fbWeight;
        tp.gTarget = 1.0;
        return;
    }

    if (std::abs (mult - tp.mult) <= 0.03 * tp.mult)
    {
        tp.multTarget = mult;           // small change: glide (tape-style bend)
        tp.gTarget = 1.0;
    }
    else if (tp.g < 0.003)
    {
        tp.mult = tp.multTarget = mult; // silent now: jump
        tp.gTarget = 1.0;
    }
    else
    {
        tp.gTarget = 0.0;               // big change: fade out first, jump next tick
    }
}

void DlayEngine::clearTapsFrom (int index) noexcept
{
    for (int k = index; k < kMaxTaps; ++k)
    {
        auto& tp = taps[k];
        tp.gTarget = 0.0;
        tp.fbT = 0.0;

        if (tp.g < 1.0e-3)
            tp.active = false;
    }
}

void DlayEngine::updateTaps (const EngineParams&, double fbBase) noexcept
{
    const Mode   m      = (Mode) mode;
    const double a      = macro[mAlgoA];
    const double b      = macro[mAlgoB];
    const double spread = macro[mSpread];

    switch (m)
    {
        case Mode::multiTap:
        {
            const int n = 2 + clampT ((int) std::lround (a * 6.0), 0, 6);
            const double tiltDb = (b - 0.5) * 24.0;

            double gains[kMaxTaps];
            double maxG = 0.0;

            for (int k = 0; k < n; ++k)
            {
                const double pos = (double) k / (double) (n - 1);
                gains[k] = dbToGain (tiltDb * (pos - 0.5));
                maxG = std::max (maxG, gains[k]);
            }

            // more taps should not simply mean louder: meet a single echo half-way (in dB)
            double sumSq = 0.0;
            for (int k = 0; k < n; ++k)
                sumSq += (gains[k] / maxG) * (gains[k] / maxG);
            const double tapNorm = std::pow (sumSq, -0.25);

            for (int k = 0; k < n; ++k)
            {
                const double pan = spread * ((k & 1) != 0 ? 1.0 : -1.0) * (0.35 + 0.65 * (double) (k + 1) / (double) n);
                double gl, gr;
                balanceGains (pan, gl, gr);
                const double g = tapNorm * gains[k] / maxG;
                setTap (k, (double) (k + 1) / (double) n, g * gl, g * gr, k == n - 1 ? 1.0 : 0.0, pan);
            }

            clearTapsFrom (n);
            break;
        }

        case Mode::tapeEcho:
        {
            const int combo = kHeadCombos[macroIndex (a, 7)];
            const int count = ((combo & 1) != 0 ? 1 : 0) + ((combo & 2) != 0 ? 1 : 0) + ((combo & 4) != 0 ? 1 : 0);
            const double og = 1.0 / std::sqrt ((double) count);

            for (int h = 0; h < 3; ++h)
            {
                if ((combo & (1 << h)) != 0)
                {
                    setTap (h, (double) (h + 1), og, og, 1.0 / (double) count, 0.0);
                }
                else
                {
                    taps[h].gTarget = 0.0;
                    taps[h].fbT = 0.0;
                    if (taps[h].g < 1.0e-3) taps[h].active = false;
                }
            }

            clearTapsFrom (3);
            break;
        }

        case Mode::pingPong:
        {
            const int pat = macroIndex (a, 6);
            const int n   = kPatternLen[pat];
            const double decay = clampT (fbBase, 0.0, 1.0);
            double g = 1.0;

            // ANGLE sets how far the bounces are thrown; the PING-PONG control pushes them
            // further out, all the way to hard left / right.
            const double angle = b + macro[mPingPong] * (1.0 - b);

            for (int k = 0; k < n; ++k)
            {
                const double pan = kPatternPan[pat][k] * angle;
                double gl, gr;
                balanceGains (pan, gl, gr);
                setTap (k, (double) (k + 1), g * gl, g * gr, k == n - 1 ? g : 0.0, pan);
                g *= decay;
            }

            clearTapsFrom (n);
            break;
        }

        case Mode::dual:
        {
            setTap (0, 1.0, kSqrt2 * std::cos (b * kPi * 0.5), kSqrt2 * std::sin (b * kPi * 0.5), 1.0, 0.0);
            clearTapsFrom (1);
            break;
        }

        case Mode::slapback:
        {
            setTap (0, 1.0, 1.0, 1.0, 1.0, 0.0);

            if (b > 0.01)
                setTap (1, 1.5, 0.35 * b, 0.95 * b, 0.0, 0.6);
            else
            {
                taps[1].gTarget = 0.0;
                if (taps[1].g < 1.0e-3) taps[1].active = false;
            }

            clearTapsFrom (2);
            break;
        }

        case Mode::fractal:
        {
            const int n = 2 + clampT ((int) std::lround (a * 4.0), 0, 4);
            const double rho = 0.5 + 0.3 * b + 0.012 * std::sin (kTwoPi * evolvePhase);

            double sumG = 0.0, sumSq = 0.0, g = 1.0;
            for (int k = 0; k < n; ++k) { sumG += g; sumSq += g * g; g *= 0.8; }

            const double norm = 1.0 / std::sqrt (sumSq);
            g = 1.0;

            for (int k = 0; k < n; ++k)
            {
                const double pan = spread * ((k & 1) != 0 ? 1.0 : -1.0) * (double) (k + 1) / (double) n;
                double gl, gr;
                balanceGains (pan, gl, gr);
                setTap (k, std::pow (rho, (double) k), g * norm * gl, g * norm * gr, g / sumG, pan);
                g *= 0.8;
            }

            clearTapsFrom (n);
            break;
        }

        case Mode::random:
        {
            static constexpr double defaults[4] = { 1.0, 0.5, 0.75, 1.25 };

            randomModeCounter += (double) kTick;
            const double cycleLen = 0.5 * (loopSamples[0] + loopSamples[1]);
            const bool first = randomModeCounter > 1.0e11;

            if (randomModeCounter >= cycleLen)
            {
                randomModeCounter = 0.0;

                for (int k = 1; k < 4; ++k)
                {
                    if (first || rngCtl.next01() < 0.2 + 0.8 * a)
                    {
                        randomMult[k] = clampT (defaults[k] * std::exp2 (a * 0.9 * rngCtl.nextBipolar()), 0.125, 1.75);
                        randomPan[k]  = rngCtl.nextBipolar() * (0.35 + 0.65 * a);
                        randomGain[k] = 0.45 + 0.55 * rngCtl.next01();
                    }
                }
            }

            const double grid = b < 0.2 ? 0.0 : (b < 0.6 ? 16.0 : 8.0);

            setTap (0, 1.0, 1.0, 1.0, 0.55, 0.0);

            for (int k = 1; k < 4; ++k)
            {
                double mult = randomMult[k];
                if (grid > 0.0)
                    mult = std::max (1.0 / grid, std::round (mult * grid) / grid);

                double gl, gr;
                balanceGains (randomPan[k], gl, gr);
                setTap (k, mult, randomGain[k] * gl * 0.8, randomGain[k] * gr * 0.8, 0.15, randomPan[k]);
            }

            clearTapsFrom (4);
            break;
        }

        case Mode::granular:
        case Mode::glitch:
            // The grain cloud / slicer are the readers here. Tap 0 stays alive (silently)
            // so FREEZE and the visualiser still have a reference head.
            setTap (0, 1.0, 0.0, 0.0, 0.0, 0.0);
            clearTapsFrom (1);
            break;

        case Mode::digital:
        case Mode::stereo:
        case Mode::tape:
        case Mode::bbd:
        case Mode::warmAnalog:
        case Mode::degraded:
        case Mode::loFi:
        case Mode::reverse:
        case Mode::pitch:
        case Mode::spectral:
        case Mode::diffused:
        case Mode::shimmer:
        case Mode::numModes:
        default:
            setTap (0, 1.0, 1.0, 1.0, 1.0, 0.0);
            clearTapsFrom (1);
            break;
    }
}

void DlayEngine::updateCollapse() noexcept
{
    const double c   = macro[mCollapse];
    const double amt = std::abs (c);

    if (amt < 0.004)
    {
        bool any = false;
        for (auto& tp : ctaps)
        {
            tp.gTarget = 0.0;
            if (tp.g < 1.0e-3) tp.active = false;
            any = any || tp.active;
        }
        numCollapse = any ? kMaxTaps : 0;
        return;
    }

    // > 0: each gap shorter than the last (a bouncing ball).  < 0: each gap longer.
    const double r = c > 0.0 ? 1.0 - 0.45 * amt : 1.0 + 0.35 * amt;
    const double longest = std::max (timeS2[0], timeS2[1]);

    double acc = 1.0, term = 1.0, g = std::sqrt (amt);

    for (int k = 0; k < kMaxTaps; ++k)
    {
        term *= r;
        acc  += term;
        g    *= 0.84;

        auto& tp = ctaps[k];
        const double pan = ((k & 1) != 0 ? 0.55 : -0.55) * amt;
        double gl, gr;
        balanceGains (pan, gl, gr);

        tp.multTarget = acc;
        tp.glT = g * gl;
        tp.grT = g * gr;
        tp.pan = pan;

        if (! tp.active)
        {
            tp.active = true;
            tp.mult = acc;
            tp.g = 0.0;
            tp.gl = tp.glT;
            tp.gr = tp.grT;
        }

        tp.gTarget = (acc * longest < maxDelay - 64.0) ? 1.0 : 0.0;
    }

    numCollapse = kMaxTaps;
}

// ============================================================================
//  ECHO ORBIT, MOTION pan, RANDOM pan -> a 2x2 stereo placement matrix
// ============================================================================
void DlayEngine::updateOrbitAndPan (const EngineParams& p, double bpm) noexcept
{
    const double amount = macro[mOrbit];
    const double freq = p.orbitSync ? bpm / (60.0 * kDivisions[(size_t) clampT (p.orbitDiv, 0, kNumDivisions - 1)].beats)
                                    : (double) p.orbitRateHz;

    orbitPhase += (double) kTick * freq / sr;
    while (orbitPhase >= 1.0) { orbitPhase -= 1.0; ++orbitCycle; }

    const double ph = kTwoPi * orbitPhase;
    double x = 0.0, y = 0.0;

    switch ((OrbitShape) p.orbitShape)
    {
        case OrbitShape::circle:    x = std::sin (ph);  y = std::cos (ph);              break;
        case OrbitShape::figure8:   x = std::sin (ph);  y = 0.8 * std::sin (2.0 * ph);  break;
        case OrbitShape::pendulum:  x = std::sin (ph);  y = 0.25 * std::cos (2.0 * ph); break;

        case OrbitShape::spiral:
        {
            const double r = 0.2 + 0.8 * std::abs (std::sin (kPi * ((double) (orbitCycle & 7) + orbitPhase) / 4.0));
            x = r * std::sin (ph);
            y = r * std::cos (ph);
            break;
        }

        case OrbitShape::scatter:
            x = hash01 ((uint32_t) orbitCycle, 0x0B17u) * 2.0 - 1.0;
            y = hash01 ((uint32_t) orbitCycle, 0x0B18u) * 2.0 - 1.0;
            break;

        case OrbitShape::numShapes:
        default: break;
    }

    orbitXs += orbitSmooth * (x * amount - orbitXs);
    orbitYs += orbitSmooth * (y * amount - orbitYs);

    const double rp = macro[mRandom] * rndPan * 0.8;
    const double pl = clampT (orbitXs + motion.value (Lane::pan, 0) + rp, -1.0, 1.0);
    const double pr = clampT (orbitXs + motion.value (Lane::pan, 1) + rp, -1.0, 1.0);

    // Placement works on mid and side. The mid is panned with an equal-power law, so a
    // centred echo keeps its power wherever it travels; the side narrows as the image
    // approaches an edge. When the two channels are asked to move differently (the pan
    // lane's L/R offset) they converge or diverge - that is MOTION's width modulation.
    const double pan    = clampT (0.5 * (pl + pr), -1.0, 1.0);
    const double theta  = (pan + 1.0) * (kPi * 0.25);
    const double spread = clampT (1.0 - (pl - pr), -1.0, 2.0);

    const auto set = [this] (Ramp& r, double target)
    {
        if (snapNext) r.snap (target); else r.setTarget (target, kTick);
    };

    set (panMidL, kSqrt2 * std::cos (theta));
    set (panMidR, kSqrt2 * std::sin (theta));
    set (panSide, spread * (1.0 - std::abs (pan)));

    // depth: the far side of the orbit is quieter and darker
    const double back = std::max (0.0, -orbitYs);
    depthOn = back > 0.01;
    set (depthGain, 1.0 - 0.42 * back);

    if (depthOn)
    {
        const double fc = 19000.0 * std::pow (0.11, back);
        depthLp[0].setCutoff (fc, sr);
        depthLp[1].setCutoff (fc, sr);
    }

    visual.orbitX.store ((float) orbitXs, std::memory_order_relaxed);
    visual.orbitY.store ((float) orbitYs, std::memory_order_relaxed);
    visual.panL.store ((float) pl, std::memory_order_relaxed);
    visual.panR.store ((float) pr, std::memory_order_relaxed);
}

// ============================================================================
//  control-rate update (every kTick samples)
// ============================================================================
void DlayEngine::controlTick (const EngineParams& p, const TransportInfo& t, int blockOffset) noexcept
{
    const double tickSec    = (double) kTick / sr;
    const int    wantedMode = clampT (p.mode, 0, kNumModes - 1);

    // ---- housekeeping ---------------------------------------------------------------
    // Something in the loop produced a value that is not a number (it has already been
    // kept off the tape and out of the output). Filters would hold on to it for ever, so
    // start again from silence.
    if (healFlag)
    {
        clearAudioState();
        snapNext = true;
    }

    // finish erasing the tape and the shifter lines a little at a time (see DelayLine)
    for (int c = 0; c < 2; ++c)
    {
        lines[c].sweep (kSweepBlocks);
        ps[c].sweep (kSweepBlocks);
    }

    // ---- soft reset / algorithm change: fade out, rebuild, fade in ------------------
    if (softResetFlag.exchange (false, std::memory_order_acq_rel))
        pendingReset = true;

    const double fadeStep = 1.0 / std::max (4.0, 0.008 * sr / (double) kTick);

    if (snapNext && fadeState == FadeState::steady)
    {
        mode = wantedMode;              // first tick after a hard reset: adopt immediately
        resetModeState();
        pendingReset = false;
    }
    else if (fadeState == FadeState::steady && (pendingReset || wantedMode != mode))
    {
        fadeState = FadeState::fadingOut;
    }

    if (fadeState == FadeState::fadingOut)
    {
        if (fadeLevel <= 0.0)
        {
            if (pendingReset)
                clearAudioState();

            pendingReset = false;
            mode = wantedMode;
            resetModeState();
            snapNext  = true;
            fadeState = FadeState::fadingIn;
        }
        else
        {
            fadeLevel = std::max (0.0, fadeLevel - fadeStep);
        }
    }
    else if (fadeState == FadeState::fadingIn)
    {
        fadeLevel = std::min (1.0, fadeLevel + fadeStep);
        if (fadeLevel >= 1.0)
            fadeState = FadeState::steady;
    }

    fadeRamp.setTarget (fadeLevel, kTick);

    // Two kinds of smoothing. Everything inside the loop and on the wet side jumps to its
    // target on the first tick after any erase: the tape is empty, nothing can click, and
    // the first new echo already has the right settings. The dry level, the output gain
    // and the bypass crossfade are heard directly, so they only jump when the engine has
    // just been prepared - an erase in mid-stream leaves them gliding.
    const bool   snap     = snapNext;
    const bool   snapOut  = snapOutput;
    const double savedMac = macroCoef;
    if (snap) macroCoef = 1.0;
    outCoef = snapOut ? 1.0 : savedMac;

    const auto ramp = [snap] (Ramp& r, double target)
    {
        if (snap) r.snap (target); else r.setTarget (target, kTick);
    };

    const auto rampOut = [snapOut] (Ramp& r, double target)
    {
        if (snapOut) r.snap (target); else r.setTarget (target, kTick);
    };

    // ---- tempo and MOTION -----------------------------------------------------------
    const double bpm = usableBpm (t.bpm);
    const double ppq = t.ppq + (double) blockOffset * bpm / (60.0 * sr);

    motion.advance (kTick, p.lanes, bpm, t.playing, ppq, t.ppqValid);

    // ---- de-zippered macros ---------------------------------------------------------
    const double algoA     = sm (mAlgoA,     p.algoA);
    const double algoB     = sm (mAlgoB,     p.algoB);
    const double spread    = sm (mSpread,    p.spread);
    const double ghost     = sm (mGhost,     p.ghost);
    sm (mCollapse, p.collapse);
    const double random    = sm (mRandom,    p.random);
    const double tapeMem   = sm (mTape,      p.tapeMemory);
    const double reverse   = sm (mReverse,   p.reverse);
    const double diffusion = sm (mDiffusion, p.diffusion);
    const double satAmt    = sm (mSat,       p.saturation);
    const double driveDb   = sm (mDrive,     p.driveDb);
    const double width     = sm (mWidth,     p.width);
    const double offset    = sm (mOffset,    p.offset);
    const double warp      = sm (mWarp,      p.warp);
    const double pingPong  = sm (mPingPong,  p.pingPong);
    sm (mOrbit, p.orbit);
    const double ducking   = sm (mDucking,   p.ducking);
    const double res       = sm (mRes,       p.resonance);
    const double modDepth  = sm (mModDepth,  p.modDepth);
    const double frz       = sm (mFreeze,    p.freeze ? 1.0 : 0.0);

    const Mode m = (Mode) mode;

    monoLoop      = (m == Mode::pingPong || m == Mode::dual);
    stereoModeCur = p.stereoMode;
    spectralOn    = (m == Mode::spectral);
    shimmerOn     = (m == Mode::shimmer);
    bigDiffOn     = (m == Mode::diffused || m == Mode::shimmer);

    // ---- base time ------------------------------------------------------------------
    double baseMs = p.sync ? kDivisions[(size_t) clampT (p.division, 0, kNumDivisions - 1)].beats * 60000.0 / bpm
                           : (double) p.timeMs;
    baseMs = clampT (baseMs, 1.0, 9000.0);

    // ---- RANDOM UNIVERSE: new offsets once per echo cycle -----------------------------
    rndCounter += (double) kTick;

    if (rndCounter >= 0.5 * (loopSamples[0] + loopSamples[1]))
    {
        rndCounter = 0.0;
        rndTime[0] = rngCtl.nextBipolar();
        rndTime[1] = lerpD (rndTime[0], rngCtl.nextBipolar(), 0.5);
        rndPan     = rngCtl.nextBipolar();
        rndFb      = rngCtl.next01();
        rndCut     = rngCtl.next01();
    }

    // ---- slow internal movement -------------------------------------------------------
    evolvePhase  = fracD (evolvePhase + tickSec * 0.07);
    modPhase     = fracD (modPhase + tickSec * (double) p.modRateHz);
    wowPhase     = fracD (wowPhase + tickSec * 0.47);
    flutterPhase = fracD (flutterPhase + tickSec * 7.1);
    wowNoise     += (tickSec / 0.5)  * (rngCtl.nextBipolar() - wowNoise);
    flutterNoise += (tickSec / 0.03) * (rngCtl.nextBipolar() - flutterNoise);

    if (--driftCountdown <= 0)
    {
        driftCountdown = (int) ((0.7 + 1.6 * rngCtl.next01()) / tickSec);
        driftTarget[0] = rngCtl.nextBipolar();
        driftTarget[1] = lerpD (driftTarget[0], rngCtl.nextBipolar(), 0.35);
    }

    driftValue[0] += (tickSec / 1.5) * (driftTarget[0] - driftValue[0]);
    driftValue[1] += (tickSec / 1.5) * (driftTarget[1] - driftValue[1]);

    // ---- diffusion --------------------------------------------------------------------
    {
        const double lenScale = std::pow (diffusion, 0.7) * sr * 0.001;

        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 4; ++i)
                diff[c].setLength (i, std::max (1.0, kDiffBaseMs[c][i] * lenScale));

        ramp (diffG, 0.62 * smoothStep (diffusion * 3.0));

        if (bigDiffOn)
        {
            const double size = 0.3 + 1.7 * (m == Mode::diffused ? algoA : algoB);
            bigModPhase = fracD (bigModPhase + tickSec * 0.31);

            for (int c = 0; c < 2; ++c)
            {
                for (int i = 0; i < 6; ++i)
                {
                    double len = kBigBaseMs[c][i] * size * sr * 0.001;
                    const bool modulated = (i == 1 || i == 4);

                    if (modulated)
                        len += 0.0006 * sr * std::sin (kTwoPi * (bigModPhase * (i == 1 ? 1.0 : 0.63) + 0.25 * (double) c));

                    bigDiff[c].setLength (i, len, ! modulated);
                }
            }

            ramp (bigG, m == Mode::diffused ? 0.42 + 0.3 * algoB : 0.66);
        }

        if (snap)
        {
            for (int c = 0; c < 2; ++c)
            {
                diff[c].snapLengths();
                bigDiff[c].snapLengths();
            }
        }
    }

    // ---- pitch shifter ----------------------------------------------------------------
    {
        const double laneDepth = (double) p.lanes[(size_t) Lane::pitch].depth;

        pitchOn = (m != Mode::granular)
                    && (m == Mode::pitch || m == Mode::shimmer || std::abs (p.pitchSt) > 0.004f || laneDepth != 0.0);

        const double detune = m == Mode::pitch ? 0.5 * algoA : 0.0;       // up to +/- 50 cents
        const double window = m == Mode::pitch ? 0.015 + 0.16 * algoB : (m == Mode::shimmer ? 0.09 : 0.07);

        for (int c = 0; c < 2; ++c)
        {
            const double st = (double) p.pitchSt + 12.0 * motion.value (Lane::pitch, c) + (c == 0 ? detune : -detune);
            ps[c].setWindow (window);
            ps[c].setRatio (semitonesToRatio (st), kTick);
        }

        pitchForUi = (double) p.pitchSt + 6.0 * (motion.value (Lane::pitch, 0) + motion.value (Lane::pitch, 1));

        const double mixNow = sm (mPitchMix, pitchOn ? (m == Mode::shimmer ? algoA : 1.0) : 0.0);
        ramp (pitchMix, mixNow);

        if (m == Mode::shimmer)
        {
            // Shifted and unshifted signals are uncorrelated, so they are blended at equal
            // power - except near unison, where they are the same signal and an equal-power
            // blend would add up to more than unity around the loop.
            const double apart = clampT (std::abs ((double) p.pitchSt) / 3.0, 0.0, 1.0);
            ramp (pitchDry, lerpD (1.0 - mixNow, std::cos (mixNow * kPi * 0.5), apart));
            ramp (pitchWet, lerpD (mixNow, std::sin (mixNow * kPi * 0.5), apart));
        }
        else
        {
            ramp (pitchDry, 1.0 - mixNow);
            ramp (pitchWet, mixNow);
        }
    }

    // ---- saturation -------------------------------------------------------------------
    {
        loopSat.set ((SatType) clampT (p.satType, 0, (int) SatType::numTypes - 1), satAmt);

        double  modeAmt  = 0.0;
        SatType modeType = SatType::tape;

        switch (m)
        {
            case Mode::tape:        modeAmt = 0.22;                             break;
            case Mode::tapeEcho:    modeAmt = 0.36;                             break;
            case Mode::bbd:         modeAmt = 0.28; modeType = SatType::tube;   break;
            case Mode::warmAnalog:  modeAmt = 0.30; modeType = SatType::tube;   break;
            case Mode::degraded:    modeAmt = 0.30;                             break;
            default: break;
        }

        modeSat.set (modeType, std::max (modeAmt, 0.35 * tapeMem));

        driveGain     = dbToGain (driveDb);
        driveComp     = 1.0 / std::sqrt (driveGain);
        inSat.type    = loopSat.type == SatType::crush ? SatType::tape : loopSat.type;
        inSat.active  = driveDb > 0.05;

        osFactor = p.quality <= 0 ? 1 : (p.quality == 1 ? 2 : 4);
    }

    const bool   satOn  = loopSat.active || modeSat.active;

    // The DC blocker is only needed where the loop can create or accumulate DC (asymmetric
    // saturation, the INFINITE leveller). Clean algorithms stay sample-exact.
    {
        const bool wantDc = satOn || p.infinite;
        if (wantDc && ! dcOn) { dcBlock[0].reset(); dcBlock[1].reset(); }
        dcOn = wantDc;
    }
    const double osLat  = satOn ? osLatency[osFactor == 1 ? 0 : (osFactor == 2 ? 1 : 2)] : 0.0;

    // ---- wow / flutter / drift amounts --------------------------------------------------
    double wf = 0.0, drift = 0.0;

    switch (m)
    {
        case Mode::tape:        wf = algoA;                 break;
        case Mode::tapeEcho:    wf = algoB;                 break;
        case Mode::loFi:        wf = 0.22;                  break;
        case Mode::warmAnalog:  drift = algoB;              break;
        case Mode::degraded:    drift = 0.45; wf = 0.15;    break;
        default: break;
    }

    wf    = std::max (wf, 0.85 * tapeMem);
    drift = std::max (drift, 0.7 * tapeMem);

    const bool fastModActive = modDepth > 1.0e-4 || wf > 1.0e-4 || drift > 1.0e-4;

    // ---- delay time targets -------------------------------------------------------------
    {
        const double sL = motion.value (Lane::speed, 0);
        const double sR = motion.value (Lane::speed, 1);

        double ms[2] = { baseMs * std::exp2 (-2.0 * warp - sL + 0.2 * random * rndTime[0]),
                         baseMs * std::exp2 (-2.0 * warp - sR + 0.2 * random * rndTime[1]) };

        if (offset < 0.0) ms[0] *= 1.0 - 0.5 * offset;
        else              ms[1] *= 1.0 + 0.5 * offset;

        const bool tapSpread = (m == Mode::multiTap || m == Mode::fractal || m == Mode::granular
                                 || m == Mode::glitch || m == Mode::spectral);
        if (! tapSpread)
        {
            ms[0] *= 1.0 - 0.012 * spread;
            ms[1] *= 1.0 + 0.012 * spread;
        }

        if (m == Mode::stereo)  ms[1] *= kStereoRatios[macroIndex (algoA, 7)];
        if (m == Mode::dual)    ms[1] *= kDualRatios[macroIndex (algoA, 9)];

        double maxMs = (maxDelay - 64.0) * 1000.0 / sr;
        if (m == Mode::reverse)         maxMs *= 0.49;
        else if (m == Mode::tapeEcho)   maxMs /= 3.0;
        else if (m == Mode::pingPong)   maxMs /= (double) kPatternLen[macroIndex (algoA, 6)];

        for (int c = 0; c < 2; ++c)
        {
            ms[c] = clampT (ms[c], 1.0, maxMs);
            loopSamples[c] = ms[c] * sr * 0.001;

            const double lat = diff[c].totalDelay()
                             + (bigDiffOn ? bigDiff[c].totalDelay() : 0.0)
                             + pitchMix.v * ps[c].latency()
                             + (spectralOn ? (double) spec[c].latency() : 0.0)
                             + osLat;

            chainLatency[c] = lat;
            double target = clampT (loopSamples[c] - lat, minMainDelay, maxDelay);

            if (! fastModActive)
                target = std::round (target);       // static time: integer delay, perfectly flat

            timeTarget[c] = target;

            if (snap)
                timeS1[c] = timeS2[c] = prevD[c] = target;
        }

        visual.timeMsL.store ((float) ms[0], std::memory_order_relaxed);
        visual.timeMsR.store ((float) ms[1], std::memory_order_relaxed);

        // INERTIA: how heavy the "flywheel" is when the time changes
        double tau = 0.005 * std::pow (400.0, (double) p.inertia);
        if (m == Mode::tape || m == Mode::tapeEcho)             tau = std::max (tau, 0.09);
        else if (m == Mode::bbd || m == Mode::warmAnalog)       tau = std::max (tau, 0.03);

        inertiaCoef = 1.0 - std::exp (-1.0 / (0.6 * tau * sr));
    }

    // ---- fast time modulation (chorus LFO, wow, flutter, drift) --------------------------
    {
        const double rate   = (double) p.modRateHz;
        const double lfoMs  = 4.0 * modDepth * modDepth / std::sqrt (1.0 + rate * 0.25);
        const double phaseR = 0.25 * spread;

        for (int c = 0; c < 2; ++c)
        {
            const double d   = timeS2[c];
            const double lfo = std::sin (kTwoPi * (modPhase + (c == 1 ? phaseR : 0.0)));
            const double wow = wf * wf * 2.2 * (0.75 * std::sin (kTwoPi * wowPhase + 0.4 * (double) c) + 0.25 * wowNoise);
            const double flt = wf * 0.11 * (std::sin (kTwoPi * flutterPhase + 1.3 * (double) c) + 0.5 * flutterNoise);

            double fm = (lfo * lfoMs + wow + flt) * sr * 0.001 + drift * 0.008 * d * driftValue[c];
            fm = clampT (fm, -0.45 * d, 0.45 * d);

            ramp (fastMod[c], fm);
        }

        // read-speed estimate -> anti-aliasing kernel for the main tap
        double worst = 1.0;
        for (int c = 0; c < 2; ++c)
        {
            const double now = timeS2[c] + fastMod[c].v;
            worst = std::max (worst, 1.0 - (now - prevD[c]) / (double) kTick);
            prevD[c] = now;
        }
        cutIdx = SincTable::cutoffForRate (worst);
    }

    // ---- feedback, INFINITE, FREEZE -------------------------------------------------------
    double fbBase = (double) p.feedback;
    if (m == Mode::slapback)
        fbBase *= 0.4;

    {
        const double level = std::sqrt (std::max (0.0, loopEnv));
        visual.loopLevel.store ((float) level, std::memory_order_relaxed);

        if (p.infinite)
        {
            // INFINITE UNIVERSE: a PI leveller holds the loop at a fixed level. It makes up
            // exactly what the filters, pitch shifter and diffusion take away on each pass,
            // and backs off as soon as new input pushes the level up - so the texture
            // sustains for ever but can never run away.
            constexpr double targetLevel = 0.2;

            if (level > 0.0015)
            {
                const double errDb = clampT (gainToDb (targetLevel / level), -24.0, 24.0);
                agcInt  = clampT (agcInt + errDb * (tickSec / 2.5), -18.0, 10.0);
                agcGain = dbToGain (clampT (agcInt + errDb, -18.0, 10.0));
            }
            else
            {
                agcInt += (0.0 - agcInt) * (tickSec / 0.5);         // nothing to hold: drift back to unity
                agcGain = dbToGain (agcInt);
            }
        }
        else
        {
            agcInt  = 0.0;
            agcGain = 1.0;
        }

        double fbShown = 0.0;

        for (int c = 0; c < 2; ++c)
        {
            double fb = p.infinite ? agcGain
                                   : clampT (fbBase + 0.5 * motion.value (Lane::feedback, c), 0.0, 1.25)
                                         * (1.0 - 0.6 * random * rndFb);
            ramp (fbRamp[c], fb);
            fbShown += 0.5 * fb;
        }

        visual.feedback.store ((float) fbShown, std::memory_order_relaxed);

        // the pattern bouncer runs one mono loop and pans its taps itself, so it must not
        // steer the input or cross the feedback as well
        ramp (ppRamp, m == Mode::pingPong ? 0.0 : pingPong);
        ramp (freezeRamp, frz);

        if (p.freeze && ! freezeLatched)
        {
            for (int c = 0; c < 2; ++c)
                freezeDelay[c] = (int) clampT (std::round (loopSamples[c]), 1.0, maxDelay);

            freezeLatched = true;
        }
        else if (! p.freeze && frz < 1.0e-3)
        {
            freezeLatched = false;
        }

        visual.freeze.store ((float) frz, std::memory_order_relaxed);
    }

    // ---- feedback filters -----------------------------------------------------------------
    {
        const double q = 0.707 * std::pow (8.5, res);
        const double k = snap ? 1.0 : 1.0 - std::exp (-tickSec / 0.008);
        double lpShown = 0.0, hpShown = 0.0;

        double peakSum = 0.0;

        for (int c = 0; c < 2; ++c)
        {
            const double v = motion.value (Lane::filter, c);

            // MOTION filter behaves like a DJ filter: down closes the low-pass, up opens the high-pass
            const double lpT = clampT ((double) p.highCutHz * std::exp2 (6.0 * std::min (0.0, v) - 2.0 * random * rndCut), 80.0, 20000.0);
            const double hpT = clampT ((double) p.lowCutHz  * std::exp2 (6.0 * std::max (0.0, v)), 20.0, 8000.0);

            lpLog[c] += k * (std::log (lpT) - lpLog[c]);
            hpLog[c] += k * (std::log (hpT) - hpLog[c]);

            const double lpHz = std::exp (lpLog[c]);
            const double hpHz = std::exp (hpLog[c]);

            lpOn[c] = lpHz < 19500.0;
            hpOn[c] = hpHz > 21.0;

            if (lpOn[c]) lpC[c].set (lpHz, q, sr);
            if (hpOn[c]) hpC[c].set (hpHz, q, sr);

            // A resonant filter inside a feedback loop multiplies the loop gain by its peak,
            // which would let RESONANCE push a 50 % feedback setting into runaway. The pair
            // is therefore scaled so its highest point sits at unity: the resonant band is
            // simply the one that decays last. Half of that (in dB) is given back on the wet
            // output, outside the loop.
            const double peak = combinedFilterPeak (hpOn[c], std::tan (kPi * std::min (hpHz, 0.49 * sr) / sr),
                                                    lpOn[c], std::tan (kPi * std::min (lpHz, 0.49 * sr) / sr), q);
            resNorm[c] = 1.0 / peak;
            peakSum += 0.5 * peak;

            lpShown += 0.5 * lpHz;
            hpShown += 0.5 * hpHz;
        }

        visual.lpHz.store ((float) lpShown, std::memory_order_relaxed);
        visual.hpHz.store ((float) hpShown, std::memory_order_relaxed);

        ramp (wetMakeup, loopSat.makeup() * std::sqrt (peakSum));
    }

    // ---- character per algorithm ------------------------------------------------------------
    double age = 0.0, dust = 0.0;
    {
        colorKind   = ColorKind::none;
        softOn      = false;
        bumpOn      = false;
        crushLevels = 0.0;
        shMix       = 0.0;
        tiltAmount  = 0.0;

        const double seconds = 0.5 * (loopSamples[0] + loopSamples[1]) / sr;        // mean loop time
        const double speedTone = clampT (std::pow (0.3 / std::max (0.02, seconds), 0.3), 0.55, 1.3);

        switch (m)
        {
            case Mode::digital:
            {
                // COLOR: centre is pristine. Left band-limits the loop like an early digital
                // unit, right lowers the converter resolution.
                colorKind = ColorKind::digital;
                const double colour = (algoA - 0.5) * 2.0;
                crushLevels = colour > 0.02 ? std::exp2 (15.0 - 10.0 * colour) : 0.0;
                softOn      = colour < -0.02;

                if (softOn)
                {
                    const double fc = 18000.0 * std::pow (1.0 / 6.0, -colour);
                    colorLp[0].setCutoff (fc, sr);
                    colorLp[1].setCutoff (fc, sr);
                }

                tiltAmount = (algoB - 0.5) * 2.0;
                break;
            }

            case Mode::stereo:
                colorKind  = ColorKind::digital;
                tiltAmount = (algoB - 0.5) * 2.0;
                break;

            case Mode::tape:
            {
                colorKind = ColorKind::tape;
                age = algoB;
                bump.setPeak (85.0, 0.8, 2.2, sr);
                bumpOn = true;
                const double fc = 16000.0 * std::pow (0.2, age) * speedTone;
                for (int c = 0; c < 2; ++c) { colorLp[c].setCutoff (fc, sr); colorLp2[c].setCutoff (fc * 2.2, sr); }
                break;
            }

            case Mode::tapeEcho:
            {
                colorKind = ColorKind::tape;
                age = 0.45;
                bump.setPeak (110.0, 0.9, 3.0, sr);
                bumpOn = true;
                const double fc = 5200.0 * speedTone;
                for (int c = 0; c < 2; ++c) { colorLp[c].setCutoff (fc, sr); colorLp2[c].setCutoff (fc * 2.0, sr); }
                break;
            }

            case Mode::bbd:
            {
                // A real BBD's clock - and with it the usable bandwidth - falls as the delay grows.
                colorKind = ColorKind::bbd;
                const double stages = std::exp2 (9.0 + 5.0 * algoA);                // 512 .. 16384
                const double fclk   = stages / (2.0 * std::max (0.001, seconds));
                bbdC.set (clampT (0.4 * fclk, 500.0, 14000.0), 0.75, sr);
                shInc = fclk / sr;
                shMix = shInc < 1.0 ? algoB : 0.0;
                dust  = 0.35 * algoB;
                break;
            }

            case Mode::warmAnalog:
            {
                colorKind = ColorKind::warm;
                const double fc = 7000.0 * std::pow (0.25, algoA);
                for (int c = 0; c < 2; ++c) { colorLp[c].setCutoff (fc * 1.3, sr); colorLp2[c].setCutoff (fc * 1.3, sr); }
                bump.setPeak (240.0, 0.7, 1.2 + 1.0 * algoA, sr);
                bumpOn = true;
                break;
            }

            case Mode::degraded:
            {
                colorKind = ColorKind::degraded;
                for (int c = 0; c < 2; ++c)
                {
                    colorLp[c].setCutoff (7000.0 * std::pow (0.17, algoA), sr);
                    colorHp[c].setCutoff (60.0 * std::pow (7.0, algoA), sr);
                }
                age  = 0.25 + 0.5 * algoB;
                dust = algoB;
                break;
            }

            case Mode::loFi:
            {
                colorKind = ColorKind::loFi;
                crushLevels = std::exp2 (15.0 - 12.0 * algoA);                      // 16 bit .. 4 bit
                const double rate = 32000.0 * std::pow (0.0375, algoB);             // 32 kHz .. 1.2 kHz
                shInc = rate / sr;
                shMix = shInc < 1.0 ? 1.0 : 0.0;
                for (int c = 0; c < 2; ++c) colorLp[c].setCutoff (clampT (rate * 0.6, 800.0, 9000.0), sr);
                break;
            }

            default: break;
        }

        // TAPE MEMORY layers tape wear on top of any algorithm
        memLpOn = tapeMem > 1.0e-3;
        if (memLpOn)
        {
            const double fc = 18000.0 * std::pow (0.19, tapeMem);
            memLp[0].setCutoff (fc, sr);
            memLp[1].setCutoff (fc, sr);
        }

        age = std::max (age, 0.9 * tapeMem);

        // wear
        wearOn      = age > 0.01 || dust > 0.01;
        noiseAmt    = 0.03 * (age * age + 0.6 * dust * dust);
        crackleProb = dust * dust * 30.0 / sr;
        crackleAmt  = 0.45;

        if (age > 0.05)
        {
            if (dropHold > 0)
            {
                if (--dropHold == 0)
                    dropTarget = 1.0;
            }
            else if (--dropCountdown <= 0)
            {
                dropTarget    = 1.0 - age * (0.25 + 0.6 * rngCtl.next01());
                dropHold      = (int) ((0.03 + 0.12 * rngCtl.next01()) / tickSec);
                dropCountdown = (int) ((0.5 + 3.5 * rngCtl.next01()) / ((0.3 + age) * tickSec));
            }
        }
        else
        {
            dropTarget = 1.0;
            dropHold = 0;
        }

        dropSmooth += (tickSec / 0.015) * (dropTarget - dropSmooth);
        if (snap) dropSmooth = dropTarget;
        ramp (dropRamp, dropSmooth);
    }

    // ---- spectral -------------------------------------------------------------------------------
    if (spectralOn)
    {
        const double seconds = 0.5 * (loopSamples[0] + loopSamples[1]) / sr;
        const double disp    = std::min (0.9, 0.75 * seconds);

        spec[0].setParams (algoA * 2.0 - 1.0, algoB, 0.5 * spread + 0.4 * random, 0.0, disp);
        spec[1].setParams (algoA * 2.0 - 1.0, algoB, 0.5 * spread + 0.4 * random, spread, disp);
    }

    // ---- readers ----------------------------------------------------------------------------------
    updateTaps (p, fbBase);
    updateCollapse();

    {
        const bool isRev = (m == Mode::reverse);
        ramp (revAmtR, isRev ? 1.0 : reverse);
        ramp (revFbR,  isRev ? reverse : 0.0);
        revFade  = isRev ? 0.02 + 0.48 * algoA : 0.08;
        revHead2 = isRev ? algoA : 0.0;
        revSwell = isRev ? algoB : 0.0;
        revChunk = clampT (0.5 * (loopSamples[0] + loopSamples[1]), 64.0, maxDelay * 0.5 - 32.0);

        // A head only picks up a new chunk length when its chunk ends. After an erase the
        // length it still carries belongs to the previous sound (or sample rate): start over.
        if (snap)
        {
            rev[0].len = rev[1].len = revChunk;
            rev[0].pos = 0.0;
            rev[1].pos = 0.5 * revChunk;
        }
    }

    if (m == Mode::granular)
    {
        grainLen     = 0.012 * std::pow (0.4 / 0.012, algoA) * sr;
        grainSpray   = algoB;
        grainRatio   = semitonesToRatio ((double) p.pitchSt + 6.0 * (motion.value (Lane::pitch, 0) + motion.value (Lane::pitch, 1)));
        grainRevProb = reverse;
        grainSpread  = spread;
        grainOctProb = 0.35 * random;
        grainNorm    = 1.25;
        pitchForUi   = (double) p.pitchSt;
    }
    else if (m == Mode::glitch)
    {
        sliceDivs  = kSliceDivs[macroIndex (algoA, 8)];
        sliceChaos = algoB;
        sliceFade  = 0.003 * sr;

        if (snap)                           // same for the slicer: the first slice gets the new length
        {
            sliceLen = std::max (96.0, 0.5 * (timeS2[0] + timeS2[1]) / (double) sliceDivs);
            slicePos = 0.0;
        }
    }

    // ---- subsonic guard ---------------------------------------------------------------------------
    // Anything that slows the tape towards a standstill (tape-stops, time dives, wow, reverse
    // chunks, down-shifted grains) drags programme material below the audio band. A 10 Hz
    // high-pass on the wet signal removes it. A static, clean delay does not need the guard
    // and is left sample-exact.
    {
        const bool gliding = std::abs (timeTarget[0] - timeS2[0]) > 0.5 || std::abs (timeTarget[1] - timeS2[1]) > 0.5;

        subsonicOn = fastModActive || gliding || satOn || pitchOn || p.infinite
                       || random > 1.0e-4 || tapeMem > 1.0e-4 || reverse > 1.0e-4
                       || p.lanes[(size_t) Lane::speed].depth != 0.0f
                       || m == Mode::reverse || m == Mode::glitch || m == Mode::granular;
    }

    // ---- slapback snap ----------------------------------------------------------------------------
    snapAmt = (m == Mode::slapback) ? 1.4 * algoA : 0.0;

    // ---- GHOST MODE -------------------------------------------------------------------------------
    {
        ramp (ghostGain, ghost * 1.5);

        const bool running = ghost > 1.0e-4 || ghostGain.v > 1.0e-4;

        if (running)
        {
            for (int j = 0; j < 2; ++j)
            {
                ghostPhase[j] = fracD (ghostPhase[j] + tickSec * (j == 0 ? 0.31 : 0.23));

                const double mult   = j == 0 ? 1.5 : 2.5;
                const double target = 0.012 * mult * timeS2[j] * std::sin (kTwoPi * ghostPhase[j]);

                // waking up: the voices are still silent, so their heads go straight to
                // where they belong instead of sweeping there from wherever they stopped
                if (ghostRunning) ramp (ghostMod[j], target);
                else              ghostMod[j].snap (target);

                const double fc = 900.0 * std::exp2 (1.4 * std::sin (kTwoPi * (ghostPhase[j] * 0.37 + 0.5 * (double) j)));
                ghostC[j].set (fc, 1.1, sr);

                const double pos = 0.8 * std::sin (kTwoPi * (ghostPhase[j] * 0.21 + 0.5 * (double) j));
                panGains (pos, ghostPanL[j], ghostPanR[j]);
            }
        }

        ghostRunning = running;
    }

    // ---- stereo placement and output ----------------------------------------------------------------
    updateOrbitAndPan (p, bpm);

    {
        // MIX and OUTPUT are smoothed like every other control, so a preset change or a
        // fast knob move is a short crossfade on the dry signal and not a step.
        const double mix = smOut (mMix, (double) p.mix);

        for (int c = 0; c < 2; ++c)
        {
            // The MIX lane rides the echo level only; the dry signal stays where the knob put it.
            const double wetPos = clampT (mix + motion.value (Lane::mix, c), 0.0, 1.0);
            rampOut (dryG[c], std::cos (mix * kPi * 0.5));
            rampOut (wetG[c], std::sin (wetPos * kPi * 0.5));
        }

        rampOut (outGain, dbToGain (smOut (mOutGain, (double) p.outGainDb)));

        double byp = smOut (mBypass, p.bypass ? 1.0 : 0.0);
        if (byp > 0.9995) byp = 1.0;
        if (byp < 0.0005) byp = 0.0;
        rampOut (bypassRamp, byp);
    }

    ramp (widthRamp, width);
    ramp (duckRamp, 1.0 - ducking * clampT (std::pow (duckEnv * 4.0, 0.7), 0.0, 1.0));
    limiterOn = p.limiter;

    visual.duckGain.store ((float) duckRamp.v, std::memory_order_relaxed);

    macroCoef  = savedMac;
    snapNext   = false;
    snapOutput = false;

    if (++visualDivider >= 2)
    {
        visualDivider = 0;
        publishVisuals (p, bpm, baseMs);
    }
}

// ============================================================================
void DlayEngine::publishVisuals (const EngineParams&, double bpm, double baseMs) noexcept
{
    const double toMs = 1000.0 / sr;
    int n = 0;

    const auto push = [this, &n] (double delayMs, double pan, double gain, int kind)
    {
        if (n >= VisualData::kMaxHeads) return;
        auto& h = visual.heads[(size_t) n++];
        h.delayMs.store ((float) delayMs, std::memory_order_relaxed);
        h.pan.store ((float) pan, std::memory_order_relaxed);
        h.gain.store ((float) gain, std::memory_order_relaxed);
        h.kind.store (kind, std::memory_order_relaxed);
    };

    const double dL = (timeS2[0] + fastMod[0].v) * toMs;
    const double dR = (timeS2[1] + fastMod[1].v) * toMs;
    const Mode m = (Mode) mode;

    // The tap that carries the feedback is the PLAY head; the others are drawn as taps.
    int mainTap = -1;
    double longestFeedbackTap = -1.0;

    for (int k = 0; k < kMaxTaps; ++k)
    {
        const auto& tp = taps[k];
        if (tp.active && tp.g > 0.01 && tp.fbT > 0.0 && tp.mult > longestFeedbackTap)
        {
            longestFeedbackTap = tp.mult;
            mainTap = k;
        }
    }

    for (int k = 0; k < kMaxTaps; ++k)
    {
        const auto& tp = taps[k];
        if (! tp.active || tp.g < 0.01) continue;

        const double level = std::max (std::abs (tp.gl), std::abs (tp.gr));

        if (k == mainTap)
        {
            push (dL * tp.mult, -1.0, tp.g, VisualData::headMain);
            push (dR * tp.mult,  1.0, tp.g, VisualData::headMain);
        }
        else if (level > 1.0e-4)
        {
            push (0.5 * (dL + dR) * tp.mult, tp.pan, tp.g * std::min (1.0, level), VisualData::headTap);
        }
    }

    for (int k = 0; k < numCollapse; ++k)
    {
        const auto& tp = ctaps[k];
        if (tp.active && tp.g > 0.01)
            push (0.5 * (dL + dR) * tp.mult, tp.pan, tp.g * std::max (tp.gl, tp.gr), VisualData::headCollapse);
    }

    if (ghostGain.v > 0.01)
    {
        push (dL * 1.5 + ghostMod[0].v * toMs, ghostPanR[0] - ghostPanL[0], ghostGain.v, VisualData::headGhost);
        push (dR * 2.5 + ghostMod[1].v * toMs, ghostPanR[1] - ghostPanL[1], ghostGain.v, VisualData::headGhost);
    }

    if (revAmtR.v > 0.01)
    {
        push (2.0 * rev[0].pos * toMs, 0.0, revAmtR.v, VisualData::headReverse);
        if (revHead2 > 0.05)
            push (2.0 * rev[1].pos * toMs, 0.0, revAmtR.v * revHead2, VisualData::headReverse);
    }

    if (m == Mode::granular)
    {
        for (const auto& g : grains)
            if (g.active)
                push ((g.startDelay + (double) g.age * g.slope) * toMs, g.pan,
                      (double) hann[(size_t) clampT ((int) ((double) g.age * g.invLen * 512.0), 0, 512)], VisualData::headGrain);
    }
    else if (m == Mode::glitch)
    {
        push (dL + sliceOffset * toMs, slicePanR - slicePanL, sliceAction == SliceAction::mute ? 0.15 : 1.0, VisualData::headSlice);
    }

    visual.chainMsL.store ((float) (chainLatency[0] * toMs), std::memory_order_relaxed);
    visual.chainMsR.store ((float) (chainLatency[1] * toMs), std::memory_order_relaxed);
    visual.numHeads.store (n, std::memory_order_relaxed);
    visual.mode.store (mode, std::memory_order_relaxed);
    visual.bpm.store ((float) bpm, std::memory_order_relaxed);
    visual.baseTimeMs.store ((float) baseMs, std::memory_order_relaxed);
    visual.pitchSt.store ((float) pitchForUi, std::memory_order_relaxed);
}

// ============================================================================
//  granular / glitch helpers
// ============================================================================
void DlayEngine::spawnGrain() noexcept
{
    Grain* slot = nullptr;
    for (auto& g : grains)
        if (! g.active) { slot = &g; break; }

    const double overlap = 3.0;
    grainCountdown = std::max (8, (int) (grainLen / overlap * (0.6 + 0.8 * rng.next01())));

    if (slot == nullptr)
        return;

    auto& g = *slot;
    const double base = 0.5 * (timeS2[0] + timeS2[1]);

    double ratio = grainRatio * std::exp2 (grainSpray * 0.3 * rng.nextBipolar() / 12.0);
    if (rng.next01() < grainOctProb)
        ratio *= rng.next01() < 0.5 ? 2.0 : 0.5;
    ratio = clampT (ratio, 0.25, 4.0);

    const bool reversed = rng.next01() < grainRevProb;

    g.len    = std::max (32, (int) (grainLen * (0.75 + 0.5 * rng.next01())));
    g.invLen = 1.0 / (double) g.len;
    g.age    = 0;
    g.slope  = reversed ? 1.0 + ratio : 1.0 - ratio;

    const double travel = g.slope * (double) g.len;         // total change in delay over the grain
    const double lo = DelayLine::kMinSincDelay + 2.0 + std::max (0.0, -travel);
    const double hi = maxDelay - 4.0 - std::max (0.0, travel);

    double start = base + grainSpray * 0.6 * base * rng.nextBipolar();
    if (reversed) start -= 0.5 * travel;                    // centre the reversed span on the delay time
    g.startDelay = clampT (start, lo, std::max (lo, hi));

    g.useSinc = std::abs (ratio - 1.0) > 0.02 || reversed;
    g.cut     = SincTable::cutoffForRate (ratio);

    g.pan = grainSpread * rng.nextBipolar();
    balanceGains (g.pan, g.gainL, g.gainR);
    g.active = true;
}

void DlayEngine::startNextSlice() noexcept
{
    const double base = 0.5 * (timeS2[0] + timeS2[1]);

    slicePos = 0.0;
    sliceLen = std::max (96.0, base / (double) sliceDivs);
    ++sliceIndex;

    // A 16-step pattern that repeats, so the glitches are a rhythm rather than noise.
    // RANDOM UNIVERSE breaks the loop.
    const bool freeRunning = rng.next01() < macro[mRandom];
    const auto step = (uint32_t) (freeRunning ? (int) (rng.nextU32() & 0xffff) : (sliceIndex & 15));
    const double roll = hash01 (step, 0xA11CEu);
    const double pick = hash01 (step, 0xBEEFu);

    SliceAction next = SliceAction::normal;

    if (roll < sliceChaos * 0.9)
    {
        if      (pick < 0.24) next = SliceAction::repeat;
        else if (pick < 0.42) next = SliceAction::ratchet;
        else if (pick < 0.58) next = SliceAction::reverse;
        else if (pick < 0.68) next = SliceAction::half;
        else if (pick < 0.78) next = SliceAction::twice;
        else if (pick < 0.86) next = SliceAction::tapeStop;
        else if (pick < 0.95) next = SliceAction::gate;
        else                  next = SliceAction::mute;
    }

    if (next == SliceAction::repeat)
    {
        sliceRepeats = sliceAction == SliceAction::repeat ? sliceRepeats + 1 : 1;
        if (sliceRepeats > 4 || (double) sliceRepeats * sliceLen + base > maxDelay - 64.0)
        {
            next = SliceAction::normal;
            sliceRepeats = 0;
        }
    }
    else
    {
        sliceRepeats = 0;
    }

    sliceAction = next;
    sliceSubs   = next == SliceAction::ratchet ? (hash01 (step, 0x51CEu) < 0.5 ? 2 : 4) : 1;
    sliceCut    = next == SliceAction::twice ? SincTable::cutoffForRate (2.0) : 0;

    const double pan = macro[mSpread] * (hash01 (step, 0x9A7u) * 2.0 - 1.0);
    balanceGains (pan, slicePanL, slicePanR);
}

// ============================================================================
//  write-side chain
// ============================================================================
inline double DlayEngine::colour (int ch, double w) noexcept
{
    switch (colorKind)
    {
        case ColorKind::digital:
        {
            // quantisers truncate towards zero: rounding would leave a limit cycle in the loop
            if (crushLevels > 0.0)
                w = std::trunc (w * crushLevels) / crushLevels;

            if (softOn)
                w = colorLp[ch].lowpass (w);

            if (tiltAmount != 0.0)
            {
                const double low = tiltLp[ch].lowpass (w);
                // cut-only tilt, so it can never raise the loop gain
                w = tiltAmount < 0.0 ? w + 0.5 * tiltAmount * (w - low)      // darker: shave the highs
                                     : w - 0.5 * tiltAmount * low;           // brighter: shave the lows
            }
            break;
        }

        case ColorKind::tape:
            w = colorLp[ch].lowpass (w);
            w = colorLp2[ch].lowpass (w);
            break;

        case ColorKind::bbd:
        {
            w = bbdPre[ch].lowpass (w, bbdC);

            if (shMix > 0.0)
            {
                // bucket clock: sample-and-hold with a fractional (area-averaged) transition
                double held = shHold[ch];
                shPhase[ch] += shInc;

                if (shPhase[ch] >= 1.0)
                {
                    shPhase[ch] -= 1.0;
                    const double f = clampT (shPhase[ch] / shInc, 0.0, 1.0);
                    held = shHold[ch] * (1.0 - f) + w * f;
                    shHold[ch] = w;
                }

                w += shMix * (held - w);
            }

            w = bbdPost[ch].lowpass (w, bbdC);
            break;
        }

        case ColorKind::warm:
            w = colorLp[ch].lowpass (w);
            w = colorLp2[ch].lowpass (w);
            break;

        case ColorKind::degraded:
            w = colorLp[ch].lowpass (w);
            w = colorHp[ch].highpass (w);
            break;

        case ColorKind::loFi:
        {
            if (shMix > 0.0)
            {
                double held = shHold[ch];
                shPhase[ch] += shInc;

                if (shPhase[ch] >= 1.0)
                {
                    shPhase[ch] -= 1.0;
                    const double f = clampT (shPhase[ch] / shInc, 0.0, 1.0);
                    held = shHold[ch] * (1.0 - f) + w * f;
                    shHold[ch] = w;
                }

                w = held;
            }

            w = std::trunc (w * crushLevels) / crushLevels;
            w = colorLp[ch].lowpass (w);
            break;
        }

        case ColorKind::none:
        default: break;
    }

    if (memLpOn)
        w = memLp[ch].lowpass (w);

    if (wearOn)
    {
        const double aw = std::abs (w);
        noiseEnv[ch] = aw > noiseEnv[ch] ? aw : noiseEnv[ch] * noiseEnvRel;

        // noise and crackle ride on the signal, like modulation noise on tape: silence stays silent
        w += noiseAmt * noiseEnv[ch] * rng.nextBipolar();

        if (crackleProb > 0.0 && rng.next01() < crackleProb)
            w += crackleAmt * noiseEnv[ch] * rng.nextBipolar() * 2.0;
    }

    return w;
}

inline double DlayEngine::chain (int ch, double w) noexcept
{
    w = diff[ch].process (w, diffG.v);

    if (bigDiffOn)
        w = bigDiff[ch].process (w, bigG.v);

    if (pitchWet.v > 1.0e-5)
    {
        double shifted = ps[ch].process (w);

        // A pitch shifter leaves DC and the lowest octave untouched, so in SHIMMER - where
        // the shifted signal is blended with the original - the two would add up coherently
        // down there and build a subsonic wave. The halo does not need bass: cut it.
        if (shimmerOn)
            shifted = shimmerHp[ch].highpass (shifted, shimmerHpC);

        w = pitchDry.v * w + pitchWet.v * shifted;
    }
    else
    {
        ps[ch].feed (w);                    // keep the shifter's buffer current
    }

    if (spectralOn)
        w = spec[ch].process (w);

    if (hpOn[ch]) w = hpS[ch].highpass (w, hpC[ch]);
    if (lpOn[ch]) w = lpS[ch].lowpass (w, lpC[ch]);
    w *= resNorm[ch];

    w = colour (ch, w);

    if (loopSat.active || modeSat.active)
    {
        w = osLoop[ch].process (w, osFactor, [this] (double v)
        {
            if (modeSat.active) v = modeSat.apply (v);
            if (loopSat.active) v = loopSat.apply (v);
            return v;
        });
    }

    if (dcOn)
        w = dcBlock[ch].highpass (w);

    return loopSoftLimit (w);
}

// ============================================================================
//  audio
// ============================================================================
void DlayEngine::process (float* left, float* right, int numSamples,
                          const EngineParams& rawParams, const TransportInfo& rawTransport) noexcept
{
    if (! prepared || numSamples <= 0)
        return;

    // nothing from outside is used before it has been forced into range (see EngineTypes.h)
    EngineParams  params    = rawParams;
    TransportInfo transport = rawTransport;
    sanitise (params);
    sanitise (transport);

    if (hardResetFlag.exchange (false, std::memory_order_acq_rel))
    {
        clearAudioState();
        softResetFlag.store (false, std::memory_order_release);
        pendingReset = false;
        fadeState    = FadeState::steady;
        fadeLevel    = 1.0;
        fadeRamp.snap (1.0);
        snapNext     = true;                // the loop starts afresh; the output stage keeps gliding
        tickPos      = 0;
    }

    double inPkL = 0.0, inPkR = 0.0, wetPkL = 0.0, wetPkR = 0.0, outPkL = 0.0, outPkR = 0.0;
    double minLim = 1.0;

    int done = 0;

    while (done < numSamples)
    {
        if (tickPos == 0)
            controlTick (params, transport, done);

        const int chunk = std::min (kTick - tickPos, numSamples - done);
        const Mode m = (Mode) mode;

        for (int i = done; i < done + chunk; ++i)
        {
            double dryL = (double) left[i], dryR = (double) right[i];

            if (! (std::abs (dryL) < 1.0e6)) dryL = 0.0;        // also catches NaN / Inf from upstream
            if (! (std::abs (dryR) < 1.0e6)) dryR = 0.0;

            inPkL = std::max (inPkL, std::abs (dryL));
            inPkR = std::max (inPkR, std::abs (dryR));

            // ---- per-sample parameter ramps ----------------------------------------------
            const double fbL  = fbRamp[0].next(), fbR = fbRamp[1].next();
            const double pp   = ppRamp.next();
            const double frz  = freezeRamp.next();
            const double fade = fadeRamp.next();
            diffG.next();  bigG.next();  pitchMix.next();  pitchDry.next();  pitchWet.next();
            const double drop = dropRamp.next();

            // ---- delay time: inertia (two cascaded poles) + fast modulation ----------------
            double D[2];

            for (int c = 0; c < 2; ++c)
            {
                timeS1[c] += inertiaCoef * (timeTarget[c] - timeS1[c]);
                timeS2[c] += inertiaCoef * (timeS1[c] - timeS2[c]);

                if (std::abs (timeTarget[c] - timeS2[c]) < 1.0e-3 && std::abs (timeTarget[c] - timeS1[c]) < 1.0e-3)
                    timeS1[c] = timeS2[c] = timeTarget[c];

                D[c] = clampT (timeS2[c] + fastMod[c].next(), minMainDelay, maxDelay);
            }

            // ---- input stage -------------------------------------------------------------------
            const double inPeak = std::max (std::abs (dryL), std::abs (dryR));
            duckEnv += (inPeak > duckEnv ? duckAtk : duckRel) * (inPeak - duckEnv);

            double xL = dryL, xR = dryR;

            if (monoLoop || stereoModeCur == (int) StereoMode::mono)
            {
                xL = xR = 0.5 * (dryL + dryR);
            }
            else if (stereoModeCur == (int) StereoMode::midSide)
            {
                xL = 0.5 * (dryL + dryR);
                xR = 0.5 * (dryL - dryR);
            }

            if (inSat.active)
            {
                const auto shaper = [this] (double v) { return shapers::shape (inSat.type, v); };
                xL = osIn[0].process (xL * driveGain, osFactor, shaper) * driveComp;
                xR = osIn[1].process (xR * driveGain, osFactor, shaper) * driveComp;
            }

            // Head bump / low-mid body: applied once on the way in. Inside the loop a boost
            // would raise the loop gain at that frequency and hum at high feedback.
            if (bumpOn)
            {
                xL = bumpS[0].process (xL, bump);
                xR = bumpS[1].process (xR, bump);
            }

            // PING-PONG steers the input to one side while crossing the feedback
            const double ppNorm = 1.0 / std::sqrt (1.0 + pp * pp);
            const double inL = (xL + pp * xR) * ppNorm;
            const double inR = xR * (1.0 - pp) * ppNorm;

            // ---- read the tape --------------------------------------------------------------------
            double outL = 0.0, outR = 0.0, fbSigL = 0.0, fbSigR = 0.0;

            for (int k = 0; k < kMaxTaps; ++k)
            {
                auto& tp = taps[k];
                if (! tp.active) continue;

                tp.g    += tapCoef  * (tp.gTarget - tp.g);
                tp.gl   += tapCoef  * (tp.glT - tp.gl);
                tp.gr   += tapCoef  * (tp.grT - tp.gr);
                tp.fb   += tapCoef  * (tp.fbT - tp.fb);
                tp.mult += multCoef * (tp.multTarget - tp.mult);

                if (tp.g < 1.0e-6 || (tp.gl == 0.0 && tp.gr == 0.0 && tp.fb == 0.0)) continue;

                double tl, tr;

                if (k == 0)
                {
                    tl = (double) lines[0].readSinc (clampT (D[0] * tp.mult, DelayLine::kMinSincDelay, maxDelay), cutIdx);
                    tr = (double) lines[1].readSinc (clampT (D[1] * tp.mult, DelayLine::kMinSincDelay, maxDelay), cutIdx);
                }
                else
                {
                    tl = (double) lines[0].readHermite (clampT (D[0] * tp.mult, DelayLine::kMinHermiteDelay, maxDelay));
                    tr = (double) lines[1].readHermite (clampT (D[1] * tp.mult, DelayLine::kMinHermiteDelay, maxDelay));
                }

                tl *= tp.g;
                tr *= tp.g;
                outL += tl * tp.gl;      outR += tr * tp.gr;
                fbSigL += tl * tp.fb;    fbSigR += tr * tp.fb;
            }

            // ---- reverse heads ------------------------------------------------------------------------
            const double revAmt = revAmtR.next();
            const double revFb  = revFbR.next();

            if (revAmt > 1.0e-4)
            {
                double rL = 0.0, rR = 0.0, revWeight = 0.0;

                for (int h = 0; h < 2; ++h)
                {
                    auto& head = rev[h];
                    const double level = h == 0 ? 1.0 : revHead2;

                    if (level > 1.0e-3)
                    {
                        const double p = head.pos / head.len;
                        double w = 1.0;

                        if (p < revFade)                w = 0.5 - 0.5 * std::cos (kPi * p / revFade);
                        else if (p > 1.0 - revFade)     w = 0.5 - 0.5 * std::cos (kPi * (1.0 - p) / revFade);

                        w *= 1.3 * level * ((1.0 - revSwell) + revSwell * 2.0 * p * std::sqrt (p));

                        const int d = std::min ((int) (2.0 * head.pos) + 4, (int) maxDelay);
                        rL += w * (double) lines[0].readInt (d);
                        rR += w * (double) lines[1].readInt (d);
                        revWeight += w;
                    }

                    head.pos += 1.0;
                    if (head.pos >= head.len)
                    {
                        head.pos = 0.0;
                        head.len = revChunk;
                    }
                }

                outL += revAmt * (rL - outL);        outR += revAmt * (rR - outR);

                // overlapping, swelling heads can exceed unity; what goes back into the loop may not
                const double revNorm = 1.0 / std::max (1.0, revWeight);
                fbSigL += revFb * (rL * revNorm - fbSigL);
                fbSigR += revFb * (rR * revNorm - fbSigR);
            }

            // ---- grain cloud ------------------------------------------------------------------------------
            if (m == Mode::granular)
            {
                if (--grainCountdown <= 0)
                    spawnGrain();

                // Overlapping grains add up, so the cloud is divided by the sum of its windows
                // whenever more than one grain's worth is sounding. That makes it a weighted
                // average of what the grains read: it can never be louder than the tape, and
                // the feedback (taken before panning) can never exceed unity gain.
                double sumL = 0.0, sumR = 0.0, panL = 0.0, panR = 0.0, weight = 0.0;

                for (auto& g : grains)
                {
                    if (! g.active) continue;

                    const double w = (double) hann[(size_t) ((double) g.age * g.invLen * 512.0)];
                    const double d = g.startDelay + (double) g.age * g.slope;
                    double sl, sr2;

                    if (g.useSinc)
                    {
                        sl  = w * (double) lines[0].readSinc (d, g.cut);
                        sr2 = w * (double) lines[1].readSinc (d, g.cut);
                    }
                    else
                    {
                        sl  = w * (double) lines[0].readHermite (d);
                        sr2 = w * (double) lines[1].readHermite (d);
                    }

                    sumL += sl;              sumR += sr2;
                    panL += sl * g.gainL;    panR += sr2 * g.gainR;
                    weight += w;

                    if (++g.age >= g.len)
                        g.active = false;
                }

                const double norm = 1.0 / std::max (1.0, weight);
                outL = panL * norm * grainNorm;   outR = panR * norm * grainNorm;
                fbSigL = sumL * norm;             fbSigR = sumR * norm;
            }
            // ---- glitch slicer ------------------------------------------------------------------------------
            else if (m == Mode::glitch)
            {
                if (slicePos >= sliceLen)
                    startNextSlice();

                double off = 0.0, gain = 1.0, local = slicePos, localLen = sliceLen;

                switch (sliceAction)
                {
                    case SliceAction::normal:   break;
                    case SliceAction::repeat:   off = (double) sliceRepeats * sliceLen;                       break;
                    case SliceAction::reverse:  off = 2.0 * slicePos - std::min (sliceLen, 0.5 * (D[0] + D[1]) - 16.0); break;
                    case SliceAction::half:     off = 0.5 * slicePos;                                          break;
                    case SliceAction::twice:    off = sliceLen - slicePos;                                     break;
                    case SliceAction::tapeStop: off = slicePos * slicePos / (2.0 * sliceLen);                  break;
                    case SliceAction::gate:     gain = slicePos < 0.5 * sliceLen ? 1.0 : 0.0; localLen = 0.5 * sliceLen; break;
                    case SliceAction::mute:     gain = 0.0;                                                    break;

                    case SliceAction::ratchet:
                    {
                        localLen = sliceLen / (double) sliceSubs;
                        const double sub = std::floor (slicePos / localLen);
                        off   = sub * localLen;
                        local = slicePos - off;
                        break;
                    }
                }

                // short fades at every slice edge keep the cuts click-free
                const double edge = std::min (sliceFade, 0.25 * localLen);
                if (local < edge)                   gain *= local / edge;
                else if (local > localLen - edge)   gain *= std::max (0.0, (localLen - local) / edge);

                sliceOffset = off;
                slicePos += 1.0;

                if (gain > 0.0)
                {
                    const double dl = clampT (D[0] + off, DelayLine::kMinSincDelay, maxDelay);
                    const double dr = clampT (D[1] + off, DelayLine::kMinSincDelay, maxDelay);
                    fbSigL = gain * (double) lines[0].readSinc (dl, sliceCut);
                    fbSigR = gain * (double) lines[1].readSinc (dr, sliceCut);
                }
                else
                {
                    fbSigL = fbSigR = 0.0;
                }

                // the pan belongs to the output only: fed back, it would be loop gain
                outL = fbSigL * slicePanL;
                outR = fbSigR * slicePanR;
            }

            // ---- TIME COLLAPSE taps ---------------------------------------------------------------------------
            if (numCollapse > 0)
            {
                for (int k = 0; k < numCollapse; ++k)
                {
                    auto& tp = ctaps[k];
                    if (! tp.active) continue;

                    tp.g    += tapCoef  * (tp.gTarget - tp.g);
                    tp.gl   += tapCoef  * (tp.glT - tp.gl);
                    tp.gr   += tapCoef  * (tp.grT - tp.gr);
                    tp.mult += multCoef * (tp.multTarget - tp.mult);

                    if (tp.g < 1.0e-6) continue;

                    const double dl = D[0] * tp.mult, dr = D[1] * tp.mult;
                    if (! (dl < maxDelay && dr < maxDelay)) continue;      // written so that NaN is skipped too

                    outL += tp.g * tp.gl * (double) lines[0].readLinear (dl);
                    outR += tp.g * tp.gr * (double) lines[1].readLinear (dr);
                }
            }

            // ---- GHOST MODE -------------------------------------------------------------------------------------
            const double gGain = ghostGain.next();

            if (gGain > 1.0e-4)
            {
                for (int j = 0; j < 2; ++j)
                {
                    const double mult = j == 0 ? 1.5 : 2.5;
                    const double d = clampT (D[j] * mult + ghostMod[j].next(), DelayLine::kMinHermiteDelay, maxDelay);

                    double g = (double) lines[j].readHermite (d);
                    g = ghostS[j].bandpass (g, ghostC[j]);
                    g = ghostDiff[j].process (g, 0.6);

                    // strip the attacks: ghosts swell in, they do not hit
                    const double ag = std::abs (g);
                    ghostFast[j] += (ag > ghostFast[j] ? ghostFastA : ghostFastR) * (ag - ghostFast[j]);
                    ghostSlow[j] += (ag > ghostSlow[j] ? ghostSlowA : ghostSlowR) * (ag - ghostSlow[j]);
                    g *= std::min (1.0, ghostSlow[j] / (ghostFast[j] + 1.0e-9));

                    outL += gGain * ghostPanL[j] * g;
                    outR += gGain * ghostPanR[j] * g;
                }
            }

            // ---- write the tape: input + feedback through the character chain --------------------------------------
            double wL, wR;

            if (frz < 0.9999)
            {
                const double gate = 1.0 - frz;
                wL = chain (0, inL * gate + fbL * ((1.0 - pp) * fbSigL + pp * fbSigR)) * drop;
                wR = chain (1, inR * gate + fbR * ((1.0 - pp) * fbSigR + pp * fbSigL)) * drop;

                if (frz > 1.0e-4)
                {
                    wL += frz * ((double) lines[0].readInt (freezeDelay[0]) - wL);
                    wR += frz * ((double) lines[1].readInt (freezeDelay[1]) - wR);
                }
            }
            else
            {
                // FREEZE: lossless integer-sample recirculation
                wL = (double) lines[0].readInt (freezeDelay[0]);
                wR = (double) lines[1].readInt (freezeDelay[1]);
            }

            // The chain ends in a limiter that cannot exceed 2, so this only ever trips on a
            // value that is not a number. It stays off the tape, and the next control tick
            // starts the loop again from silence (filters would never let go of it).
            if (! (std::abs (wL) < 8.0) || ! (std::abs (wR) < 8.0))
            {
                wL = wR = 0.0;
                healFlag = true;
            }

            loopEnv += loopEnvCoef * (0.5 * (wL * wL + wR * wR) - loopEnv);

            lines[0].write ((float) wL);
            lines[1].write ((float) wR);

            // envelope ring for the tape-loop display
            ringPkL  = std::max (ringPkL, std::abs (wL));
            ringPkR  = std::max (ringPkR, std::abs (wR));
            ringPkIn = std::max (ringPkIn, inPeak);

            if (++ringCount >= ringHop)
            {
                ringCount = 0;
                const uint32_t idx = visual.ringWrite.load (std::memory_order_relaxed);
                const auto slot = (size_t) (idx & (uint32_t) (VisualData::kRingSize - 1));
                visual.ringL[slot].store ((float) ringPkL, std::memory_order_relaxed);
                visual.ringR[slot].store ((float) ringPkR, std::memory_order_relaxed);
                visual.ringIn[slot].store ((float) ringPkIn, std::memory_order_relaxed);
                visual.ringWrite.store (idx + 1u, std::memory_order_release);
                ringPkL = ringPkR = ringPkIn = 0.0;

                // keep the erase marker within reach of the 32-bit slot counter
                if (idx + 1u - ringErased > 0x40000000u)
                {
                    ringErased = idx + 1u - (uint32_t) VisualData::kRingSize;
                    visual.ringErasedAt.store (ringErased, std::memory_order_relaxed);
                }
            }

            // ---- wet path: decode, width, placement ---------------------------------------------------------------
            double wetL = outL, wetR = outR;

            if (snapAmt > 0.0)
            {
                wetL += snapAmt * snapHp[0].highpass (wetL);
                wetR += snapAmt * snapHp[1].highpass (wetR);
            }

            if (stereoModeCur == (int) StereoMode::midSide && ! monoLoop)
            {
                const double mid = wetL, side = wetR;
                wetL = mid + side;
                wetR = mid - side;
            }

            {
                // WIDTH, ECHO ORBIT and MOTION pan in one mid/side step
                const double mid  = 0.5 * (wetL + wetR);
                const double side = 0.5 * (wetL - wetR) * widthRamp.next() * panSide.next();
                wetL = mid * panMidL.next() + side;
                wetR = mid * panMidR.next() - side;
            }

            {
                const double dg = depthGain.next();
                if (depthOn)
                {
                    wetL = depthLp[0].lowpass (wetL);
                    wetR = depthLp[1].lowpass (wetR);
                }
                wetL *= dg;
                wetR *= dg;
            }

            {
                // the filter state always tracks the signal, so engaging the guard is seamless
                const double lowL = subsonic[0].lowpass (wetL);
                const double lowR = subsonic[1].lowpass (wetR);

                if (subsonicOn)
                {
                    wetL -= lowL;
                    wetR -= lowR;
                }
            }

            const double wetScale = fade * duckRamp.next() * wetMakeup.next();
            wetL = wetSoftCeiling (wetL * wetScale * wetG[0].next());
            wetR = wetSoftCeiling (wetR * wetScale * wetG[1].next());

            wetPkL = std::max (wetPkL, std::abs (wetL));
            wetPkR = std::max (wetPkR, std::abs (wetR));

            // ---- output stage ------------------------------------------------------------------------------------------
            const double og = outGain.next();
            double yL = (dryL * dryG[0].next() + wetL) * og;
            double yR = (dryR * dryG[1].next() + wetR) * og;

            if (limiterOn)
            {
                constexpr double ceiling = 0.985;
                const double peak   = std::max (std::abs (yL), std::abs (yR));
                const double target = peak > ceiling ? ceiling / peak : 1.0;

                limGain = target < limGain ? target : limGain + limRel * (target - limGain);
                yL *= limGain;
                yR *= limGain;
                minLim = std::min (minLim, limGain);
            }

            const double byp = bypassRamp.next();
            if (byp > 0.0)
            {
                yL += byp * (dryL - yL);
                yR += byp * (dryR - yR);
            }

            // last line of defence: the host never receives anything that is not a number
            if (! (std::abs (yL) < 1.0e9) || ! (std::abs (yR) < 1.0e9))
            {
                yL = yR = 0.0;
                healFlag = true;
            }

            outPkL = std::max (outPkL, std::abs (yL));
            outPkR = std::max (outPkR, std::abs (yR));

            left[i]  = (float) yL;
            right[i] = (float) yR;
        }

        tickPos = (tickPos + chunk) & (kTick - 1);
        done += chunk;
    }

    // peak-hold: the editor takes the value with exchange(), so no block is ever missed
    const auto hold = [] (std::atomic<float>& a, double v)
    {
        a.store (std::max (a.load (std::memory_order_relaxed), (float) v), std::memory_order_relaxed);
    };

    hold (visual.inPeakL, inPkL);     hold (visual.inPeakR, inPkR);
    hold (visual.wetPeakL, wetPkL);   hold (visual.wetPeakR, wetPkR);
    hold (visual.outPeakL, outPkL);   hold (visual.outPeakR, outPkR);
    visual.limiterGr.store (std::min (visual.limiterGr.load (std::memory_order_relaxed), (float) minLim), std::memory_order_relaxed);
}

} // namespace dlay
