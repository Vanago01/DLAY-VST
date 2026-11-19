// DLAY headless test runner.
//
//   DLAY_Tests                 run every test (exit code 0 = all passed)
//   DLAY_Tests --verbose       ... and print the level of every preset
//   DLAY_Tests --quick         skip the long renders (for Debug builds: the point there is the assertions)
//   DLAY_Tests --perf          print the CPU cost of every algorithm
//   DLAY_Tests --foreign <n>   only the damaged-data test, with n damaged sessions and n damaged presets
//   DLAY_Tests --render <dir>  render every factory preset to a WAV file (test pings)
//   DLAY_Tests --demo <dir> [--presets "Name A,Name B"]
//                              render a short musical phrase through a selection of presets

#include "TestSupport.h"
#include "DSP/Oversampler.h"

#include <chrono>
#include <limits>
#include <thread>

using namespace dlaytest;
using namespace dlay;

namespace
{

EngineParams cleanParams()
{
    EngineParams p;
    p.mode     = (int) Mode::digital;
    p.sync     = false;
    p.timeMs   = 100.0f;
    p.feedback = 0.5f;
    p.mix      = 1.0f;
    p.limiter  = false;
    p.inertia  = 0.0f;
    p.quality  = 1;
    return p;
}

double goertzel (const std::vector<double>& x, double cyclesPerSample)
{
    double re = 0.0, im = 0.0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (kTwoPi * (double) i / (double) x.size());
        re += w * x[i] * std::cos (kTwoPi * cyclesPerSample * (double) i);
        im += w * x[i] * std::sin (kTwoPi * cyclesPerSample * (double) i);
    }
    return 2.0 * std::sqrt (re * re + im * im) / (0.5 * (double) x.size());
}

// ============================================================================
void testHalfband()
{
    section ("Half-band oversampling filters");

    double worstPass = 0.0, worstStop = -200.0, worstImage = -200.0;

    for (const double f : { 0.01, 0.08, 0.2, 0.3, 0.4, 0.43 })
    {
        Halfband2x hb;
        hb.design (10, 0.07);
        std::vector<double> out;

        for (int n = 0; n < 6000; ++n)
        {
            double a, b;
            hb.up (std::sin (kTwoPi * f * (double) n), a, b);
            const double y = hb.down (a, b);
            if (n >= 2000) out.push_back (y);
        }

        worstPass = std::max (worstPass, std::abs (gainToDb (goertzel (out, f))));
    }

    for (const double fh : { 0.29, 0.32, 0.37, 0.42, 0.47, 0.495 })
    {
        Halfband2x hb;
        hb.design (10, 0.07);
        double acc = 0.0;
        int count = 0;

        for (int n = 0; n < 6000; ++n)
        {
            const double s0 = std::sin (kTwoPi * fh * (double) (2 * n));
            const double s1 = std::sin (kTwoPi * fh * (double) (2 * n + 1));
            const double y = hb.down (s0, s1);
            if (n >= 2000) { acc += y * y; ++count; }
        }

        worstStop = std::max (worstStop, gainToDb (std::sqrt (2.0 * acc / (double) count)));
    }

    for (const double f : { 0.1, 0.2, 0.3, 0.4 })
    {
        Halfband2x hb;
        hb.design (10, 0.07);
        std::vector<double> hi;

        for (int n = 0; n < 6000; ++n)
        {
            double a, b;
            hb.up (std::sin (kTwoPi * f * (double) n), a, b);
            if (n >= 2000) { hi.push_back (a); hi.push_back (b); }
        }

        const double wanted = goertzel (hi, f * 0.5);
        const double image  = goertzel (hi, 0.5 - f * 0.5);
        worstImage = std::max (worstImage, gainToDb (image / wanted));
    }

    std::printf ("    pass-band ripple (to 0.43 fs)  %.4f dB\n", worstPass);
    std::printf ("    stop-band rejection            %.1f dB\n", worstStop);
    std::printf ("    up-sampler image rejection     %.1f dB\n", worstImage);
    std::printf ("    loop latency 2x / 4x           %.2f / %.2f samples\n", Oversampler::measureLatency (2), Oversampler::measureLatency (4));

    check (worstPass < 0.05, "half-band pass-band is flat");
    check (worstStop < -80.0, "half-band stop-band rejection better than 80 dB");
    check (worstImage < -80.0, "up-sampler image rejection better than 80 dB");
}

// ============================================================================
void testInterpolation()
{
    section ("Delay-line interpolation");

    double worstSinc = 0.0, worstHermite = 0.0;

    for (const double f : { 0.02, 0.1, 0.2, 0.3 })
    {
        for (const double frac : { 0.0, 0.2, 0.5, 0.83 })
        {
            DelayLine line;
            line.prepare (4096);
            const int n = 3000;

            for (int i = 0; i < n; ++i)
                line.write ((float) std::sin (kTwoPi * f * (double) i));

            const double delay    = 100.0 + frac;
            const double expected = std::sin (kTwoPi * f * ((double) n - delay));

            worstSinc = std::max (worstSinc, std::abs ((double) line.readSinc (delay) - expected));
            if (f <= 0.1)
                worstHermite = std::max (worstHermite, std::abs ((double) line.readHermite (delay) - expected));
        }
    }

    {
        DelayLine line;
        line.prepare (1024);
        for (int i = 0; i < 700; ++i)
            line.write ((float) i);

        check (std::abs (line.readSinc (37.0) - 663.0f) < 1.0e-3f, "integer sinc read is exact");
        check (std::abs (line.readInt (1) - 699.0f) < 1.0e-6f, "delay of 1 returns the newest sample");
        check (std::abs (line.readLinear (10.5) - 689.5f) < 1.0e-3f, "linear read");
        check (std::abs (line.readHermite (10.5) - 689.5f) < 1.0e-3f, "hermite read");
    }

    std::printf ("    sinc error up to 0.3 fs        %.1f dB\n", gainToDb (worstSinc));
    std::printf ("    hermite error up to 0.1 fs     %.1f dB\n", gainToDb (worstHermite));

    check (worstSinc < 0.004, "sinc interpolation error below -48 dB up to 0.3 fs");
    check (worstHermite < 0.01, "hermite interpolation error below -40 dB up to 0.1 fs");
}

// ============================================================================
void testImpulseResponse()
{
    section ("Echo timing and feedback accuracy");

    for (const double sr : { 44100.0, 48000.0, 96000.0 })
    {
        DlayEngine engine;
        engine.prepare (sr);

        auto p = cleanParams();
        const int period = (int) std::lround (0.1 * sr);

        StereoBuffer in ((int) sr);
        in.l[500] = in.r[500] = 0.5f;

        const auto out = renderThroughEngine (engine, in, p, 256);

        bool ok = std::abs (out.l[500]) < 1.0e-6f;         // mix at 100 %: no dry
        for (int k = 1; k <= 5; ++k)
        {
            const float expected = 0.5f * std::pow (0.5f, (float) (k - 1));
            ok = ok && std::abs (out.l[(size_t) (500 + k * period)] - expected) < 1.0e-4f;
            ok = ok && std::abs (out.r[(size_t) (500 + k * period)] - expected) < 1.0e-4f;
            ok = ok && std::abs (out.l[(size_t) (500 + k * period + 3)]) < 1.0e-5f;
        }

        check (ok, "digital 100 ms / 50 % feedback impulse response at " + juce::String (sr) + " Hz");
    }

    {
        DlayEngine engine;
        engine.prepare (48000.0);
        auto p = cleanParams();
        p.sync = true;
        p.division = Div::n8d;          // dotted eighth at 120 bpm = 375 ms = 18000 samples
        p.feedback = 0.0f;

        StereoBuffer in (48000);
        in.l[100] = in.r[100] = 1.0f;
        const auto out = renderThroughEngine (engine, in, p, 512);

        check (std::abs (out.l[18100] - 1.0f) < 1.0e-4f, "tempo-synced dotted eighth lands on the sample");
    }

    {
        // the write-side chain is latency compensated: diffusion must not move the echo
        DlayEngine engine;
        engine.prepare (48000.0);
        auto p = cleanParams();
        p.timeMs = 250.0f;
        p.feedback = 0.0f;
        p.diffusion = 1.0f;

        StereoBuffer in (48000);
        in.l[100] = in.r[100] = 1.0f;
        const auto out = renderThroughEngine (engine, in, p, 512);

        double num = 0.0, den = 0.0;
        for (int i = 0; i < out.size(); ++i)
        {
            const double e = (double) out.l[(size_t) i] * out.l[(size_t) i];
            num += e * (double) i;
            den += e;
        }

        const double centroidMs = (num / den - 100.0) / 48.0;
        std::printf ("    diffused echo centroid         %.1f ms (target 250)\n", centroidMs);
        check (std::abs (centroidMs - 250.0) < 12.0, "diffusion keeps the echo on time");
        check (std::abs (std::sqrt (den) - 1.0) < 0.02, "diffusion is energy preserving (all-pass)");
    }
}

// ============================================================================
void testBlockSizeInvariance()
{
    section ("Block-size independence");

    const auto input = makeTestSignal (48000.0, 2.5, 1.5);

    for (const Mode mode : { Mode::digital, Mode::tape, Mode::multiTap, Mode::granular, Mode::glitch, Mode::shimmer, Mode::spectral })
    {
        auto p = cleanParams();
        p.mode = (int) mode;
        p.timeMs = 180.0f;
        p.feedback = 0.6f;
        p.modDepth = 0.4f;
        p.mix = 0.5f;
        p.saturation = 0.4f;
        p.lanes[(size_t) Lane::speed] = { 0.3f, (int) LfoShape::sine, 2.0f, false, Div::bar1, 0.0f, 0.25f };
        p.lanes[(size_t) Lane::pan]   = { 0.8f, (int) LfoShape::sampleHold, 5.0f, false, Div::bar1, 0.0f, 0.0f };

        StereoBuffer ref;
        bool same = true;

        for (const int bs : { 480, 1, 17, 64, 1024 })
        {
            DlayEngine engine;
            engine.prepare (48000.0);
            const auto out = renderThroughEngine (engine, input, p, bs);

            if (bs == 480) { ref = out; continue; }

            for (int i = 0; i < out.size(); ++i)
                if (std::abs (out.l[(size_t) i] - ref.l[(size_t) i]) > 1.0e-6f || std::abs (out.r[(size_t) i] - ref.r[(size_t) i]) > 1.0e-6f)
                    { same = false; break; }
        }

        check (same, juce::String (modeInfo ((int) mode).name) + " renders identically at block sizes 1 / 17 / 64 / 480 / 1024");
    }
}

// ============================================================================
EngineParams stressParams (int mode)
{
    EngineParams p;
    p.mode = mode;
    p.sync = false;
    p.timeMs = 140.0f;
    p.feedback = 1.1f;
    p.mix = 0.6f;
    p.pingPong = 0.7f;
    p.width = 1.6f;
    p.offset = 0.4f;
    p.spread = 0.8f;
    p.saturation = 0.7f;
    p.driveDb = 14.0f;
    p.satType = mode % 5;
    p.lowCutHz = 120.0f;
    p.highCutHz = 3500.0f;
    p.resonance = 0.85f;
    p.diffusion = 0.6f;
    p.modDepth = 0.6f;
    p.modRateHz = 3.0f;
    p.pitchSt = 7.0f;
    p.reverse = 0.4f;
    p.ducking = 0.5f;
    p.algoA = 0.8f;
    p.algoB = 0.7f;
    p.warp = 0.35f;
    p.orbit = 0.9f;
    p.orbitRateHz = 2.0f;
    p.orbitShape = mode % 5;
    p.ghost = 0.7f;
    p.collapse = (mode & 1) != 0 ? 0.7f : -0.6f;
    p.random = 0.6f;
    p.tapeMemory = 0.6f;
    p.limiter = true;

    for (int l = 0; l < kNumLanes; ++l)
        p.lanes[(size_t) l] = { (l & 1) != 0 ? 0.7f : -0.6f, (l + mode) % 7, 0.7f + 1.3f * (float) l, false, Div::n2, 0.1f * (float) l, 0.25f };

    return p;
}

void testAllModes()
{
    section ("All 21 algorithms: output, stability, protection");

    for (int mode = 0; mode < kNumModes; ++mode)
    {
        const juce::String name = modeInfo (mode).name;

        // 1. nominal settings must produce an audible, finite tail and return to silence
        {
            DlayEngine engine;
            engine.prepare (48000.0);

            auto p = cleanParams();
            p.mode = mode;
            p.timeMs = 220.0f;
            p.feedback = 0.45f;
            p.mix = 1.0f;
            p.pitchSt = (mode == (int) Mode::pitch || mode == (int) Mode::shimmer) ? 12.0f : 0.0f;

            const auto input = makeTestSignal (48000.0, 30.0, 1.0, (uint32_t) mode + 1);
            const auto out = renderThroughEngine (engine, input, p, 512);

            const double wetRms  = out.rms (48000, 96000);
            const double tailRms = out.rms (48000 * 28, 48000 * 30);

            check (out.allFinite(), name + ": finite output");
            check (wetRms > 1.0e-3, name + ": produces echoes (rms " + juce::String (wetRms, 5) + ")");
            check (tailRms < 2.0e-4, name + ": decays to silence (tail rms " + juce::String (tailRms, 7) + ")");
        }

        // 2. everything turned up, at three sample rates and awkward block sizes
        for (const double sr : { 44100.0, 96000.0, 192000.0 })
        {
            DlayEngine engine;
            engine.prepare (sr);

            const auto p = stressParams (mode);
            const auto input = makeTestSignal (sr, 3.0, 2.0, (uint32_t) mode + 11);
            const auto out = renderThroughEngine (engine, input, p, sr > 100000.0 ? 2048 : 113);

            check (out.allFinite(), name + ": stress test finite at " + juce::String (sr));
            check (out.peak() <= 1.0f, name + ": limiter holds at " + juce::String (sr) + " (peak " + juce::String (out.peak(), 3) + ")");
        }

        // 3. runaway feedback without the output limiter must still be bounded by the loop protection
        {
            DlayEngine engine;
            engine.prepare (48000.0);

            auto p = cleanParams();
            p.mode = mode;
            p.timeMs = 60.0f;
            p.feedback = 1.25f;
            p.resonance = 1.0f;
            p.highCutHz = 1800.0f;
            p.mix = 1.0f;
            p.limiter = false;

            const auto input = makeTestSignal (48000.0, 12.0, 1.0, 5);
            const auto out = renderThroughEngine (engine, input, p, 256);

            check (out.allFinite() && out.peak() < 12.0f, name + ": runaway feedback stays bounded (peak " + juce::String (out.peak(), 2) + ")");
        }
    }
}

// ============================================================================
void testFeedbackAlwaysDecays()
{
    section ("Feedback below 100 % always decays (no hidden loop gain)");

    struct Case
    {
        const char* name; int mode; float lowCut, highCut, resonance, algoA, algoB, saturation, diffusion;
        float pitch = 0.0f, reverse = 0.0f, spread = 0.0f, pingPong = 0.0f;
    };

    const Case cases[] =
    {
        { "resonant low-pass",            (int) Mode::digital,     20.0f,  1500.0f, 1.0f, 0.5f, 0.5f, 0.0f, 0.0f },
        { "resonant high-pass",           (int) Mode::digital,    800.0f, 20000.0f, 1.0f, 0.5f, 0.5f, 0.0f, 0.0f },
        { "resonant wide band-pass",      (int) Mode::digital,    300.0f,  3000.0f, 1.0f, 0.5f, 0.5f, 0.0f, 0.0f },
        { "resonant narrow band-pass",    (int) Mode::digital,   1000.0f,  1000.0f, 1.0f, 0.5f, 0.5f, 0.0f, 0.0f },
        { "resonant + saturation",        (int) Mode::warmAnalog, 200.0f,  2500.0f, 0.8f, 0.5f, 0.3f, 0.8f, 0.3f },
        { "tape, filters open",           (int) Mode::tape,        20.0f, 20000.0f, 0.0f, 0.3f, 0.3f, 0.0f, 0.0f },
        { "tape echo, all heads",         (int) Mode::tapeEcho,    20.0f, 20000.0f, 0.0f, 1.0f, 0.3f, 0.0f, 0.0f },
        { "warm analog, full warmth",     (int) Mode::warmAnalog,  20.0f, 20000.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f },
        { "bucket brigade",               (int) Mode::bbd,         20.0f, 20000.0f, 0.0f, 0.5f, 0.5f, 0.0f, 0.0f },
        { "digital with grit (quantiser)",(int) Mode::digital,     20.0f, 20000.0f, 0.0f, 1.0f, 0.5f, 0.0f, 0.0f },
        { "lo-fi, crushed",               (int) Mode::loFi,        20.0f, 20000.0f, 0.0f, 0.8f, 0.5f, 0.0f, 0.0f },
        { "spectral, full smear",         (int) Mode::spectral,    20.0f, 20000.0f, 0.0f, 0.7f, 1.0f, 0.0f, 0.0f },
        { "diffused",                     (int) Mode::diffused,    20.0f, 20000.0f, 0.0f, 0.6f, 1.0f, 0.0f, 1.0f },
        { "multi-tap, 8 taps",            (int) Mode::multiTap,    20.0f, 20000.0f, 0.0f, 1.0f, 0.5f, 0.0f, 0.0f },
        { "fractal, 6 scales",            (int) Mode::fractal,     20.0f, 20000.0f, 0.0f, 1.0f, 0.5f, 0.0f, 0.0f },
        { "granular",                     (int) Mode::granular,    20.0f, 20000.0f, 0.0f, 0.5f, 0.5f, 0.0f, 0.0f },
        { "granular, long grains, wide",  (int) Mode::granular,    20.0f, 20000.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f, 1.0f },
        { "glitch, full chaos, wide",     (int) Mode::glitch,      20.0f, 20000.0f, 0.0f, 0.6f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f },
        { "reverse, re-reversed, swell",  (int) Mode::reverse,     20.0f, 20000.0f, 0.0f, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f },
        { "reverse blend on tape",        (int) Mode::tape,        20.0f, 20000.0f, 0.0f, 0.3f, 0.3f, 0.0f, 0.0f, 0.0f, 1.0f },
        { "pitch +12",                    (int) Mode::pitch,       20.0f, 20000.0f, 0.0f, 0.5f, 0.5f, 0.0f, 0.0f, 12.0f },
        { "pitch -5",                     (int) Mode::pitch,       20.0f, 20000.0f, 0.0f, 0.0f, 0.3f, 0.0f, 0.0f, -5.0f },
        { "shimmer +12",                  (int) Mode::shimmer,     20.0f, 20000.0f, 0.0f, 0.5f, 0.5f, 0.0f, 0.5f, 12.0f },
        { "shimmer at unison",            (int) Mode::shimmer,     20.0f, 20000.0f, 0.0f, 0.5f, 0.5f, 0.0f, 0.5f, 0.0f },
        { "dual, crossed",                (int) Mode::dual,        20.0f, 20000.0f, 0.0f, 0.6f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f },
        { "pattern ping-pong",            (int) Mode::pingPong,    20.0f, 20000.0f, 0.0f, 0.9f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f },
        { "random taps, full chaos",      (int) Mode::random,      20.0f, 20000.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f },
        { "slapback, doubled",            (int) Mode::slapback,    20.0f, 20000.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f },
        { "degraded",                     (int) Mode::degraded,    20.0f, 20000.0f, 0.0f, 0.5f, 1.0f, 0.0f, 0.0f },
        { "stereo, crossed feedback",     (int) Mode::stereo,      20.0f, 20000.0f, 0.0f, 0.9f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.5f },
    };

    for (const auto& c : cases)
    {
        DlayEngine engine;
        engine.prepare (48000.0);

        auto p = cleanParams();
        p.mode       = c.mode;
        p.timeMs     = 240.0f;
        p.feedback   = 0.96f;
        p.lowCutHz   = c.lowCut;
        p.highCutHz  = c.highCut;
        p.resonance  = c.resonance;
        p.algoA      = c.algoA;
        p.algoB      = c.algoB;
        p.saturation = c.saturation;
        p.diffusion  = c.diffusion;
        p.pitchSt    = c.pitch;
        p.reverse    = c.reverse;
        p.spread     = c.spread;
        p.pingPong   = c.pingPong;
        p.limiter    = false;

        const auto input = makeTestSignal (48000.0, 150.0, 1.5, 9);
        const auto out = renderThroughEngine (engine, input, p, 512);

        const double early = out.rms (48000 * 2, 48000 * 6);
        const double late  = out.rms (48000 * 140, 48000 * 150);

        std::printf ("    %-30s early %.5f  after 140 s %.7f  peak %.2f\n", c.name, early, late, (double) out.peak());

        check (out.allFinite(), juce::String (c.name) + ": finite");
        check (early > 1.0e-4, juce::String (c.name) + ": audible");
        check (late < early * 0.02, juce::String (c.name) + ": decays at 96 % feedback");
        check (out.peak() < 1.5f, juce::String (c.name) + ": never reaches the loop protection (peak " + juce::String (out.peak(), 2) + ")");
    }
}

// ============================================================================
void testFreezeAndInfinite()
{
    section ("Freeze and Infinite Universe");

    {
        DlayEngine engine;
        engine.prepare (48000.0);

        auto p = cleanParams();
        p.timeMs = 333.3f;
        p.feedback = 0.3f;
        p.diffusion = 0.4f;
        p.highCutHz = 4000.0f;

        const auto input = makeTestSignal (48000.0, 1.0, 1.0);
        renderThroughEngine (engine, input, p, 256);

        p.freeze = true;
        const auto frozen = renderThroughEngine (engine, StereoBuffer (48000 * 40), p, 256);

        const double early = frozen.rms (48000 * 2, 48000 * 3);
        const double late  = frozen.rms (48000 * 38, 48000 * 39);

        std::printf ("    frozen loop level after 2 s / 38 s   %.6f / %.6f\n", early, late);
        check (early > 1.0e-3, "freeze holds audio");
        check (std::abs (late / early - 1.0) < 0.002, "freeze is lossless over 38 seconds");

        p.freeze = false;
        const auto released = renderThroughEngine (engine, StereoBuffer (48000 * 12), p, 256);
        check (released.rms (48000 * 11, 48000 * 12) < 1.0e-4, "released freeze decays again");
    }

    for (const Mode mode : { Mode::tape, Mode::diffused, Mode::shimmer, Mode::digital, Mode::bbd })
    {
        DlayEngine engine;
        engine.prepare (48000.0);

        auto p = cleanParams();
        p.mode = (int) mode;
        p.timeMs = 400.0f;
        p.infinite = true;
        p.highCutHz = 6000.0f;
        p.lowCutHz = 150.0f;
        p.pitchSt = mode == Mode::shimmer ? 12.0f : 0.0f;
        p.limiter = true;

        const auto input = makeTestSignal (48000.0, 60.0, 3.0);
        const auto out = renderThroughEngine (engine, input, p, 512);

        const double late = out.rms (48000 * 56, 48000 * 60);
        std::printf ("    %-12s infinite level after 56 s   %.4f  (peak %.3f)\n", modeInfo ((int) mode).name, late, out.peak());

        check (out.allFinite(), juce::String (modeInfo ((int) mode).name) + ": infinite stays finite");
        check (late > 0.01 && late < 0.6, juce::String (modeInfo ((int) mode).name) + ": infinite sustains at a controlled level");
        check (out.peak() <= 1.0f, juce::String (modeInfo ((int) mode).name) + ": infinite never clips");
    }
}

// ============================================================================
void testPitchAndSpectral()
{
    section ("Pitch shifter and spectral processor");

    const double sr = 48000.0;

    for (const float semis : { 12.0f, -12.0f, 7.0f })
    {
        DlayEngine engine;
        engine.prepare (sr);

        auto p = cleanParams();
        p.mode = (int) Mode::pitch;
        p.timeMs = 500.0f;
        p.feedback = 0.0f;
        p.pitchSt = semis;
        p.algoA = 0.0f;         // no detune
        p.algoB = 0.4f;

        StereoBuffer in ((int) sr);
        for (int i = 0; i < (int) (0.3 * sr); ++i)
        {
            const double env = std::min (1.0, std::min ((double) i, 0.3 * sr - (double) i) / 500.0);
            in.l[(size_t) i] = in.r[(size_t) i] = (float) (0.5 * env * std::sin (kTwoPi * 440.0 * (double) i / sr));
        }

        const auto out = renderThroughEngine (engine, in, p, 512);

        std::vector<double> seg;
        for (int i = (int) (0.58 * sr); i < (int) (0.74 * sr); ++i)
            seg.push_back ((double) out.l[(size_t) i]);

        // A two-head shifter adds side-bands at the window rate, so judge the pitch by the
        // spectral centroid around the target rather than by the single strongest line.
        const double target = 440.0 * std::exp2 ((double) semis / 12.0);
        double num = 0.0, den = 0.0, best = 0.0;
        for (double f = target * 0.8; f < target * 1.25; f += 1.0)
        {
            const double m = goertzel (seg, f / sr);
            num += f * m * m;
            den += m * m;
            best = std::max (best, m);
        }

        const double centroid = num / den;
        std::printf ("    %+.0f st: echo centred on %.1f Hz (target %.1f), level %.3f\n", (double) semis, centroid, target, best);
        check (std::abs (centroid - target) < target * 0.005, "pitch shift of " + juce::String (semis) + " st is in tune");
        check (best > 0.2, "pitch-shifted echo keeps its level");
    }

    {
        DlayEngine engine;
        engine.prepare (sr);

        auto p = cleanParams();
        p.mode = (int) Mode::spectral;
        p.timeMs = 400.0f;
        p.feedback = 0.0f;
        p.algoA = 0.5f;         // no tilt
        p.algoB = 0.0f;         // no smear

        const auto input = makeTestSignal (sr, 2.0, 0.25);
        const auto out = renderThroughEngine (engine, input, p, 512);

        const int shift = (int) (0.4 * sr);
        double err = 0.0, ref = 0.0;
        for (int i = 0; i + shift < out.size(); ++i)
        {
            const double d = (double) out.l[(size_t) (i + shift)] - (double) input.l[(size_t) i];
            err += d * d;
            ref += (double) input.l[(size_t) i] * input.l[(size_t) i];
        }

        std::printf ("    neutral spectral delay reconstruction error  %.1f dB\n", 10.0 * std::log10 (err / ref));
        check (err / ref < 1.0e-4, "neutral spectral delay reconstructs the input (STFT overlap-add is unity)");
    }
}

// ============================================================================
void testProcessor()
{
    section ("Plugin: bus layouts, bypass, state recall");

    {
        DlayAudioProcessor proc;

        using Set = juce::AudioChannelSet;
        juce::AudioProcessor::BusesLayout monoMono, monoStereo, stereoStereo, stereoMono;
        monoMono.inputBuses.add (Set::mono());          monoMono.outputBuses.add (Set::mono());
        monoStereo.inputBuses.add (Set::mono());        monoStereo.outputBuses.add (Set::stereo());
        stereoStereo.inputBuses.add (Set::stereo());    stereoStereo.outputBuses.add (Set::stereo());
        stereoMono.inputBuses.add (Set::stereo());      stereoMono.outputBuses.add (Set::mono());

        check (proc.checkBusesLayoutSupported (monoMono), "mono -> mono supported");
        check (proc.checkBusesLayoutSupported (monoStereo), "mono -> stereo supported");
        check (proc.checkBusesLayoutSupported (stereoStereo), "stereo -> stereo supported");
        check (! proc.checkBusesLayoutSupported (stereoMono), "stereo -> mono rejected");

        for (const auto* layout : { &monoMono, &monoStereo, &stereoStereo })
        {
            check (proc.setBusesLayout (*layout), "layout accepted");
            proc.setRateAndBufferSizeDetails (48000.0, 256);
            proc.prepareToPlay (48000.0, 256);

            const int channels = std::max (proc.getTotalNumInputChannels(), proc.getTotalNumOutputChannels());
            juce::AudioBuffer<float> buffer (channels, 256);
            juce::MidiBuffer midi;
            bool finite = true;
            double energy = 0.0;

            for (int block = 0; block < 200; ++block)
            {
                for (int ch = 0; ch < channels; ++ch)
                    for (int i = 0; i < 256; ++i)
                        buffer.setSample (ch, i, block < 20 ? 0.3f * std::sin (0.05f * (float) (i + block * 256)) : 0.0f);

                proc.processBlock (buffer, midi);

                for (int ch = 0; ch < proc.getTotalNumOutputChannels(); ++ch)
                    for (int i = 0; i < 256; ++i)
                    {
                        finite = finite && std::isfinite (buffer.getSample (ch, i));
                        if (block > 40) energy += (double) buffer.getSample (ch, i) * buffer.getSample (ch, i);
                    }
            }

            check (finite, "finite output for layout");
            check (energy > 1.0e-6, "echo tail present for layout");
            proc.releaseResources();
        }
    }

    {
        // bypass: bit-exact dry signal once the crossfade has finished, and a clean return
        DlayAudioProcessor proc;
        setParam (proc, id::mix, 50.0f);
        setParam (proc, id::bypass, 1.0f);

        const auto input = makeTestSignal (48000.0, 2.0, 2.0);
        const auto out = renderThroughProcessor (proc, input, 48000.0, 256);

        bool exact = true;
        for (int i = 24000; i < out.size(); ++i)
            exact = exact && out.l[(size_t) i] == input.l[(size_t) i] && out.r[(size_t) i] == input.r[(size_t) i];
        check (exact, "bypass passes the input bit-exactly");

        setParam (proc, id::bypass, 0.0f);
        const auto back = renderThroughProcessor (proc, input, 48000.0, 256);
        check (back.allFinite() && back.rms (48000, 96000) > 1.0e-3, "plugin wakes up from bypass");
    }

    {
        // host reset (processing stopped and restarted): the old tail must be gone at once
        DlayAudioProcessor proc;
        setParam (proc, id::mix, 100.0f);
        setParam (proc, id::feedback, 80.0f);

        const auto input = makeTestSignal (48000.0, 2.0, 2.0);
        const auto before = renderThroughProcessor (proc, input, 48000.0, 256);
        check (before.rms (48000, 96000) > 1.0e-3, "echoes are running before the reset");

        proc.reset();

        juce::AudioBuffer<float> block (2, 256);
        juce::MidiBuffer midi;
        float residue = 0.0f;

        for (int i = 0; i < 40; ++i)
        {
            block.clear();
            proc.processBlock (block, midi);
            residue = juce::jmax (residue, block.getMagnitude (0, 256));
        }

        check (residue == 0.0f, "a host reset silences the tail immediately (residue " + juce::String (residue, 6) + ")");
    }

    {
        // state round trip, including custom MOTION curves and the preset name
        DlayAudioProcessor a;

        dlay::Rng rng;
        rng.seed (99);
        std::map<juce::String, float> expected;

        for (auto* param : a.getParameters())
        {
            auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param);
            // snap to a legal value first (switches and choices only have a few)
            const float legal = ranged->convertTo0to1 (ranged->convertFrom0to1 ((float) rng.next01()));
            ranged->setValueNotifyingHost (legal);
            expected[ranged->getParameterID()] = ranged->getValue();
        }

        const std::vector<CurvePoint> curve { { 0.0f, 0.1f, 0.3f }, { 0.31f, 0.92f, -0.5f }, { 0.64f, 0.4f, 0.0f }, { 1.0f, 0.77f, 0.0f } };
        a.curves->setPoints ((int) Lane::filter, curve);

        juce::MemoryBlock blob;
        a.getStateInformation (blob);

        DlayAudioProcessor b;
        b.setStateInformation (blob.getData(), (int) blob.getSize());

        bool same = true;
        for (auto* param : b.getParameters())
        {
            auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param);
            if (std::abs (ranged->getValue() - expected[ranged->getParameterID()]) > 1.0e-5f)
            {
                same = false;
                std::printf ("    mismatch %s: %f vs %f\n", ranged->getParameterID().toRawUTF8(), (double) ranged->getValue(), (double) expected[ranged->getParameterID()]);
            }
        }
        check (same, "all " + juce::String (b.getParameters().size()) + " parameters survive a save / restore");

        const auto restored = b.curves->getPoints ((int) Lane::filter);
        bool curveOk = restored.size() == curve.size();
        for (size_t i = 0; curveOk && i < curve.size(); ++i)
            curveOk = std::abs (restored[i].x - curve[i].x) < 1.0e-5f && std::abs (restored[i].y - curve[i].y) < 1.0e-5f
                        && std::abs (restored[i].curve - curve[i].curve) < 1.0e-5f;
        check (curveOk, "custom MOTION curve survives a save / restore");

        bool tableOk = true;
        for (int i = 0; i < MotionEngine::kTableSize; ++i)
            tableOk = tableOk && std::abs (a.engine.motion.tableValue ((int) Lane::filter, i) - b.engine.motion.tableValue ((int) Lane::filter, i)) < 1.0e-5f;
        check (tableOk, "restored curve reaches the audio engine");

        // garbage must be ignored without side effects
        const char junk[] = "this is not a plugin state";
        b.setStateInformation (junk, (int) sizeof (junk));
        b.setStateInformation (nullptr, 0);
        check (std::abs (b.apvts.getParameter (id::time)->getValue() - expected[id::time]) < 1.0e-5f, "invalid state data is ignored");
    }

    check (dlay::kNumLanes == 6, "six MOTION lanes");
}

// ============================================================================
//  Robustness. Each block below pins down one finding of the independent code review.
// ============================================================================
void testPrimitives()
{
    section ("Clamps, ramps and real-time erasing");

    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();

    check (clampT (nan, 1.0, 2.0) == 1.0, "clamp contains a value that is not a number");
    check (clampT (inf, 1.0, 2.0) == 2.0 && clampT (-inf, 1.0, 2.0) == 1.0, "clamp contains infinities");
    check (clampT (1.5, 1.0, 2.0) == 1.5 && clampT (5, 0, 3) == 3 && clampT (-5, 0, 3) == 0 && clampT (0, 0, 3) == 0,
           "clamp is unchanged for ordinary values");

    {
        // a ramp that is not given a new target on every tick must hold, not drift
        Ramp r;
        r.snap (0.25);
        r.setTarget (1.0, 16);

        bool monotonic = true;
        double previous = r.v;
        for (int i = 0; i < 16; ++i)
        {
            const double v = r.next();
            monotonic = monotonic && v > previous;
            previous = v;
        }

        check (monotonic && r.v == 1.0, "a ramp reaches its target in the given steps and lands on it exactly");

        for (int i = 0; i < 2000000; ++i)
            r.next();

        check (r.v == 1.0, "a ramp without a new target holds its value");

        r.setTarget (nan, 16);
        for (int i = 0; i < 16; ++i) r.next();
        r.snap (inf);
        check (r.v == 1.0, "a ramp ignores targets that are not numbers");
    }

    {
        // Erasing a delay line must be indistinguishable from a zero-filled one, whatever
        // touches it afterwards and in whatever order.
        Rng rng;
        rng.seed (4242);
        bool same = true;
        int compared = 0;

        for (int trial = 0; trial < 42 && same; ++trial)
        {
            const int size = 1 << (8 + (trial % 7));                // 256 .. 16384 samples
            DelayLine used, fresh;
            used.prepare (size);
            fresh.prepare (size);

            const int fill = size * 2 + (int) (rng.next01() * (double) size);
            for (int i = 0; i < fill; ++i)
                used.write ((float) rng.nextBipolar());

            used.clear();
            fresh.clear();

            const double maxD = used.maxDelay();

            for (int step = 0; step < 6000 && same; ++step)
            {
                const double pick = rng.next01();

                if (pick < 0.35)
                {
                    const auto v = (float) rng.nextBipolar();
                    used.write (v);
                    fresh.write (v);
                }
                else if (pick < 0.39)
                {
                    used.sweep (1 + (int) (rng.next01() * 3.0));
                }
                else if (pick < 0.395)
                {
                    used.clear();                                    // erase again in mid-flight
                    fresh.prepare (size);
                }
                else
                {
                    const double d = 8.0 + rng.next01() * (maxD - 8.0);
                    float a = 0.0f, b = 0.0f;

                    switch ((int) (rng.next01() * 4.0))
                    {
                        case 0:  a = used.readInt ((int) d);     b = fresh.readInt ((int) d);     break;
                        case 1:  a = used.readLinear (d);        b = fresh.readLinear (d);        break;
                        case 2:  a = used.readHermite (d);       b = fresh.readHermite (d);       break;
                        default:
                        {
                            const int cut = (int) (rng.next01() * 5.0);
                            a = used.readSinc (d, cut);
                            b = fresh.readSinc (d, cut);
                            break;
                        }
                    }

                    same = same && a == b;
                    ++compared;
                }
            }

            used.sweep (1 << 20);
            same = same && used.isClean();
        }

        check (same, "an erased delay line reads exactly like a new one (" + juce::String (compared) + " reads, all four interpolators)");
    }

    {
        Diffuser<4> used, fresh;
        used.prepare (48000.0, 0.026);
        fresh.prepare (48000.0, 0.026);

        for (int i = 0; i < 4; ++i)
        {
            used.setLength (i, 211.0 + 97.0 * (double) i);
            fresh.setLength (i, 211.0 + 97.0 * (double) i);
        }

        used.snapLengths();
        fresh.snapLengths();

        Rng rng;
        rng.seed (7);
        for (int i = 0; i < 30000; ++i)
            used.process (rng.nextBipolar(), 0.6);

        used.reset();

        bool same = true;
        for (int i = 0; i < 20000; ++i)
        {
            const double x = rng.nextBipolar();
            same = same && used.process (x, 0.6) == fresh.process (x, 0.6);
        }

        check (same, "a diffuser after reset() is identical to a new one");
    }

    {
        SpectralProcessor used, fresh;
        used.prepare (48000.0, 0x77u);
        fresh.prepare (48000.0, 0x77u);
        used.setParams (0.6, 0.5, 0.4, 0.0, 0.3);
        fresh.setParams (0.6, 0.5, 0.4, 0.0, 0.3);

        Rng rng;
        rng.seed (11);
        for (int i = 0; i < 60000; ++i)
            used.process (rng.nextBipolar());

        used.reset();

        bool same = true;
        for (int i = 0; i < 40000; ++i)
        {
            const double x = i < 20000 ? rng.nextBipolar() : 0.0;
            same = same && used.process (x) == fresh.process (x);
        }

        check (same, "a spectral processor after reset() is identical to a new one");
    }
}

// ----------------------------------------------------------------------------
void testResets (bool quick)
{
    section ("Resets: nothing of the old tape comes back, and erasing is cheap");

    const double sr = 48000.0;

    // 1. every algorithm, everything switched on: after a host reset and after a preset-style
    //    reset the output for silent input is exactly zero
    {
        int hardFailures = 0, softFailures = 0;

        for (int mode = 0; mode < kNumModes; ++mode)
        {
            DlayEngine engine;
            engine.prepare (sr);

            auto p = stressParams (mode);
            p.feedback = 0.9f;
            p.mix = 1.0f;

            const auto loud = makeTestSignal (sr, 2.0, 2.0, (uint32_t) mode + 3u);
            const StereoBuffer silence ((int) (quick ? 1.0 * sr : 4.0 * sr));

            renderThroughEngine (engine, loud, p, 256);
            engine.requestHardReset();
            const auto afterHard = renderThroughEngine (engine, silence, p, 256);

            if (afterHard.peak() != 0.0f)
            {
                ++hardFailures;
                std::printf ("    %s: residue %g after a hard reset\n", modeInfo (mode).name, (double) afterHard.peak());
            }

            renderThroughEngine (engine, loud, p, 256);
            engine.requestSoftReset();
            const auto afterSoft = renderThroughEngine (engine, silence, p, 256);

            float residue = 0.0f;
            for (int i = (int) (0.05 * sr); i < afterSoft.size(); ++i)
                residue = std::max (residue, std::max (std::abs (afterSoft.l[(size_t) i]), std::abs (afterSoft.r[(size_t) i])));

            if (residue != 0.0f || ! afterSoft.allFinite())
            {
                ++softFailures;
                std::printf ("    %s: residue %g after a soft reset\n", modeInfo (mode).name, (double) residue);
            }
        }

        check (hardFailures == 0, "all 21 algorithms are silent at once after a host reset");
        check (softFailures == 0, "all 21 algorithms are silent 50 ms after a preset-style reset");
    }

    // 2. an erased engine is the same engine as a new one: bit-identical echoes
    {
        auto p = cleanParams();
        p.timeMs = 230.0f;
        p.feedback = 0.7f;
        p.diffusion = 0.5f;

        const auto input = makeTestSignal (sr, 3.0, 1.0);

        DlayEngine fresh;
        fresh.prepare (sr);
        const auto reference = renderThroughEngine (fresh, input, p, 128);

        DlayEngine used;
        used.prepare (sr);
        renderThroughEngine (used, makeTestSignal (sr, 3.0, 3.0, 9), p, 128);
        used.requestHardReset();
        const auto again = renderThroughEngine (used, input, p, 128);

        bool same = true;
        for (int i = 0; i < again.size(); ++i)
            same = same && again.l[(size_t) i] == reference.l[(size_t) i] && again.r[(size_t) i] == reference.r[(size_t) i];

        check (same, "after a host reset the engine renders bit-identically to a new one");
        check (reference.rms ((int) sr, (int) (2.0 * sr)) > 1.0e-3, "... and that render has echoes in it");
    }

    // 3. cost of the audio callback in which the erase happens, at the worst case (192 kHz:
    //    two ten-second tapes, two pitch-shifter lines and the spectral history are ~30 MB)
    {
        const double hiSr = 192000.0;
        const int block = 64;
        const double blockUs = 1.0e6 * (double) block / hiSr;

        DlayEngine engine;
        engine.prepare (hiSr);

        auto p = cleanParams();
        p.mode = (int) Mode::shimmer;
        p.timeMs = 500.0f;
        p.feedback = 0.8f;
        p.ghost = 0.5f;
        p.collapse = 0.5f;

        const auto noise = makeTestSignal (hiSr, 0.5, 0.5, 5);
        TransportInfo transport;

        const auto timeBlock = [&] (int offset)
        {
            StereoBuffer buf (block);
            std::copy (noise.l.begin() + offset, noise.l.begin() + offset + block, buf.l.begin());
            std::copy (noise.r.begin() + offset, noise.r.begin() + offset + block, buf.r.begin());

            const auto start = std::chrono::steady_clock::now();
            engine.process (buf.l.data(), buf.r.data(), block, p, transport);
            return std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - start).count();
        };

        std::vector<double> steady, hard, soft;

        for (int trial = 0; trial < 9; ++trial)
        {
            renderThroughEngine (engine, noise, p, block);              // record onto the tape

            for (int i = 0; i < 40; ++i)
                steady.push_back (timeBlock (i * block));

            engine.requestHardReset();
            hard.push_back (timeBlock (0));

            renderThroughEngine (engine, noise, p, block);
            engine.requestSoftReset();

            double worst = 0.0;
            for (int i = 0; i < 60; ++i)                                // fade-out, erase, fade-in
                worst = std::max (worst, timeBlock (i * block));
            soft.push_back (worst);
        }

        std::sort (steady.begin(), steady.end());
        const double typical  = steady[steady.size() / 2];
        const double bestHard = *std::min_element (hard.begin(), hard.end());
        const double bestSoft = *std::min_element (soft.begin(), soft.end());

        std::printf ("    192 kHz, 64-sample callback (%.0f us of audio): typical %.0f us, with a host reset %.0f us, worst during a preset reset %.0f us\n",
                     blockUs, typical, bestHard, bestSoft);

        // Relative to an ordinary callback, so the check means the same on any machine and
        // in an unoptimised build. Zero-filling the memory in one go costs fifty times as much.
        check (bestHard < 10.0 * typical, "the callback that erases 30 MB of delay memory costs no more than a few ordinary ones");
        check (bestSoft < 10.0 * typical, "so does every callback of a preset change");

        if (typical < 0.25 * blockUs)           // only where this build can run in real time at all
            check (bestHard < blockUs && bestSoft < blockUs, "... and both stay inside the callback's real-time budget");
    }

    // 4. the display is told when the tape has been erased
    {
        DlayEngine engine;
        engine.prepare (sr);
        auto p = cleanParams();
        renderThroughEngine (engine, makeTestSignal (sr, 1.0, 1.0), p, 256);

        const auto before = engine.visual.ringWrite.load() - engine.visual.ringErasedAt.load();
        engine.requestHardReset();
        StereoBuffer one (16);
        engine.process (one.l.data(), one.r.data(), 16, p, {});
        const auto after = engine.visual.ringWrite.load() - engine.visual.ringErasedAt.load();

        check (before > 500u && after <= 1u, "the tape display is told that the tape was erased");
    }
}

// ----------------------------------------------------------------------------
void testNonFiniteValues()
{
    section ("Values that are not numbers are contained");

    const double sr = 48000.0;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    static const std::vector<float EngineParams::*> fields {
        &EngineParams::timeMs, &EngineParams::feedback, &EngineParams::mix, &EngineParams::pingPong, &EngineParams::width,
        &EngineParams::offset, &EngineParams::spread, &EngineParams::saturation, &EngineParams::driveDb, &EngineParams::lowCutHz,
        &EngineParams::highCutHz, &EngineParams::resonance, &EngineParams::diffusion, &EngineParams::modDepth, &EngineParams::modRateHz,
        &EngineParams::pitchSt, &EngineParams::reverse, &EngineParams::ducking, &EngineParams::algoA, &EngineParams::algoB,
        &EngineParams::warp, &EngineParams::inertia, &EngineParams::orbit, &EngineParams::orbitRateHz, &EngineParams::ghost,
        &EngineParams::collapse, &EngineParams::random, &EngineParams::tapeMemory, &EngineParams::outGainDb };

    static const std::vector<float LaneParams::*> laneFields { &LaneParams::depth, &LaneParams::rateHz, &LaneParams::phase, &LaneParams::stereo };

    const auto chunk = makeTestSignal (sr, 0.05, 0.05);
    int cases = 0;
    bool finite = true;

    for (const Mode mode : { Mode::tape, Mode::shimmer, Mode::granular, Mode::reverse, Mode::spectral })
    {
        DlayEngine engine;
        engine.prepare (sr);
        const auto good = stressParams ((int) mode);
        renderThroughEngine (engine, chunk, good, 64);

        const auto run = [&] (const EngineParams& p, const TransportInfo& t)
        {
            const auto out = renderThroughEngine (engine, chunk, p, 64, t);
            finite = finite && out.allFinite();
            ++cases;
        };

        for (const float bad : { nan, inf, -inf, 1.0e30f, -1.0e30f })
        {
            for (const auto field : fields)
            {
                auto p = good;
                p.*field = bad;
                run (p, {});
            }

            for (const auto field : laneFields)
            {
                auto p = good;
                for (auto& lane : p.lanes)
                {
                    lane.*field = bad;
                    lane.sync = false;
                }
                run (p, {});
            }
        }

        for (const int bad : { std::numeric_limits<int>::max(), std::numeric_limits<int>::min(), -1, 9999 })
        {
            auto p = good;
            p.mode = p.division = p.stereoMode = p.satType = p.orbitShape = p.orbitDiv = p.quality = bad;
            for (auto& lane : p.lanes) { lane.shape = bad; lane.division = bad; }
            run (p, {});
        }

        // the host's transport
        for (const double bad : { (double) nan, (double) inf, -(double) inf, 1.0e300, -1.0e300, 0.0, -120.0 })
        {
            auto p = good;
            for (auto& lane : p.lanes) lane.sync = true;
            p.sync = true;
            p.orbitSync = true;

            TransportInfo t;
            t.playing = true;
            t.ppqValid = true;

            t.bpm = bad;   t.ppq = 4.0;   run (p, t);
            t.bpm = 120.0; t.ppq = bad;   run (p, t);
            t.bpm = bad;   t.ppq = bad;   run (p, t);
        }

        // a curve table can only ever hold -1 .. 1
        for (int i = 0; i < MotionEngine::kTableSize; ++i)
            engine.motion.setTableValue ((int) Lane::speed, i, (i & 1) != 0 ? nan : inf);

        {
            auto p = good;
            for (auto& lane : p.lanes) lane.shape = (int) LfoShape::custom;
            run (p, {});
        }

        bool lanesSane = true;
        for (int l = 0; l < kNumLanes; ++l)
            for (int c = 0; c < 2; ++c)
                lanesSane = lanesSane && std::abs (engine.motion.value ((Lane) l, c)) <= 1.0;

        check (lanesSane, juce::String (modeInfo ((int) mode).name) + ": MOTION outputs stay within -1 .. 1 whatever is thrown at them");

        // ... and afterwards the engine is in working order: a clean delay echoes an impulse exactly
        auto p = cleanParams();
        engine.requestHardReset();
        renderThroughEngine (engine, StereoBuffer ((int) sr), p, 64);

        StereoBuffer impulse ((int) (0.25 * sr));
        impulse.l[100] = impulse.r[100] = 1.0f;
        const auto out = renderThroughEngine (engine, impulse, p, 64);

        const int at = 100 + (int) (0.1 * sr);
        check (out.allFinite() && std::abs (out.l[(size_t) at] - 1.0f) < 1.0e-4f && std::abs (out.r[(size_t) at] - 1.0f) < 1.0e-4f,
               juce::String (modeInfo ((int) mode).name) + ": the engine works normally afterwards (echo " + juce::String (out.l[(size_t) at], 5) + ")");
    }

    check (finite, "finite output for " + juce::String (cases) + " cases of NaN, infinite and out-of-range parameters and transport");

    // audio input that is not a number does not get in either
    {
        DlayEngine engine;
        engine.prepare (sr);
        auto p = cleanParams();
        p.mix = 0.5f;

        StereoBuffer bad ((int) (0.2 * sr));
        for (int i = 0; i < bad.size(); ++i)
        {
            bad.l[(size_t) i] = (i % 3) == 0 ? nan : ((i % 3) == 1 ? inf : 1.0e20f);
            bad.r[(size_t) i] = -inf;
        }

        const auto out = renderThroughEngine (engine, bad, p, 64);
        const auto tail = renderThroughEngine (engine, StereoBuffer ((int) sr), p, 64);
        check (out.allFinite() && tail.allFinite() && tail.peak() == 0.0f, "audio input that is not a number is replaced by silence");
    }

    // MOTION curve points that are not numbers: from a session, from a preset, or set directly
    {
        struct Access : DlayAudioProcessor
        {
            using DlayAudioProcessor::copyXmlToBinary;
            using DlayAudioProcessor::getXmlFromBinary;
        };

        DlayAudioProcessor source;
        juce::MemoryBlock blob;
        source.getStateInformation (blob);

        auto xml = Access::getXmlFromBinary (blob.getData(), (int) blob.getSize());
        int damaged = 0;

        if (xml != nullptr)
            if (auto* motion = xml->getChildByName ("MOTION"))
                for (auto* lane : motion->getChildIterator())
                    for (auto* point : lane->getChildIterator())
                    {
                        point->setAttribute ("y", (damaged % 3) == 0 ? "nan" : ((damaged % 3) == 1 ? "inf" : "-inf"));
                        if ((damaged % 2) == 0) point->setAttribute ("x", "nan");
                        if ((damaged % 4) == 0) point->setAttribute ("c", "nan");
                        ++damaged;
                    }

        check (damaged >= 12, "test set-up: the session carries MOTION points to damage (" + juce::String (damaged) + ")");
        check (std::isnan (juce::String ("nan").getFloatValue()), "test set-up: the text 'nan' really parses as not-a-number");

        juce::MemoryBlock damagedBlob;
        Access::copyXmlToBinary (*xml, damagedBlob);

        DlayAudioProcessor proc;
        proc.setStateInformation (damagedBlob.getData(), (int) damagedBlob.getSize());

        for (int l = 0; l < kNumLanes; ++l)
        {
            setParam (proc, id::lane (l, id::laneShape), (float) (int) LfoShape::custom);
            setParam (proc, id::lane (l, id::laneDepth), l % 2 == 0 ? 100.0f : -100.0f);
        }

        // and once more through the editor's route, with real NaNs
        proc.curves->setPoints ((int) Lane::pitch, { { nan, nan, nan }, { 0.5f, inf, -inf }, { 1.0f, 0.5f, 0.0f } }, false);

        bool pointsOk = true, tablesOk = true;

        for (int l = 0; l < kNumLanes; ++l)
        {
            for (const auto& pt : proc.curves->getPoints (l))
                pointsOk = pointsOk && pt.x >= 0.0f && pt.x <= 1.0f && pt.y >= 0.0f && pt.y <= 1.0f && pt.curve >= -1.0f && pt.curve <= 1.0f;

            for (int i = 0; i < MotionEngine::kTableSize; ++i)
                tablesOk = tablesOk && std::abs (proc.engine.motion.tableValue (l, i)) <= 1.0f;
        }

        check (pointsOk, "damaged MOTION points are repaired when they are read");
        check (tablesOk, "no damaged value reaches the curve tables of the audio thread");

        const auto out = renderThroughProcessor (proc, makeTestSignal (sr, 3.0, 2.0), sr, 256);
        check (out.allFinite() && out.rms (0, out.size()) > 1.0e-3, "a session with damaged MOTION curves plays (rms " + juce::String (out.rms (0, out.size()), 4) + ")");

        // the transport, through the plugin's own entry point
        struct BadPlayHead : juce::AudioPlayHead
        {
            juce::Optional<PositionInfo> getPosition() const override
            {
                PositionInfo info;
                info.setBpm (std::numeric_limits<double>::quiet_NaN());
                info.setPpqPosition (std::numeric_limits<double>::quiet_NaN());
                info.setIsPlaying (true);
                return info;
            }
        };

        BadPlayHead badPlayHead;
        proc.setPlayHead (&badPlayHead);

        juce::AudioBuffer<float> block (2, 256);
        juce::MidiBuffer midi;
        bool ok = true;

        for (int i = 0; i < 200; ++i)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int n = 0; n < 256; ++n)
                    block.setSample (ch, n, 0.3f * std::sin (0.02f * (float) (n + i * 256)));

            proc.processBlock (block, midi);

            for (int ch = 0; ch < 2; ++ch)
                for (int n = 0; n < 256; ++n)
                    ok = ok && std::isfinite (block.getSample (ch, n));
        }

        proc.setPlayHead (nullptr);
        check (ok, "a host transport that reports NaN does no harm");
    }
}

// ----------------------------------------------------------------------------
void testReverseAfterReset()
{
    section ("Reverse heads pick up the new time straight after a reset");

    const double sr = 48000.0;

    const auto echoAfterChange = [sr] (bool changeSampleRate)
    {
        DlayEngine engine;
        engine.prepare (sr);

        auto p = cleanParams();
        p.mode = (int) Mode::reverse;
        p.timeMs = 4000.0f;
        p.feedback = 0.0f;

        renderThroughEngine (engine, makeTestSignal (sr, 1.5, 1.5), p, 256);    // heads are deep inside a 4 s chunk

        p.timeMs = 250.0f;
        const double rate = changeSampleRate ? 96000.0 : sr;

        if (changeSampleRate) engine.prepare (rate);
        else                  engine.requestSoftReset();                         // what loading a preset does

        renderThroughEngine (engine, StereoBuffer ((int) (0.1 * rate)), p, 256);

        StereoBuffer impulse ((int) (0.7 * rate));
        impulse.l[0] = impulse.r[0] = 1.0f;
        return renderThroughEngine (engine, impulse, p, 256).peak();
    };

    const float afterPreset = echoAfterChange (false);
    const float afterRate   = echoAfterChange (true);

    std::printf ("    reversed echo within 0.7 s of an impulse: %.2f after a preset change, %.2f after a sample-rate change\n",
                 (double) afterPreset, (double) afterRate);

    check (afterPreset > 0.3f, "4 s reverse -> 250 ms reverse preset: the first chunk already has the new length");
    check (afterRate > 0.3f, "the same after the engine is prepared at another sample rate");
}

// ----------------------------------------------------------------------------
void testPitchSweep()
{
    section ("Pitch sweeps glide");

    const double sr = 48000.0;

    struct Highpass
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0, z1 = 0.0, z2 = 0.0;

        Highpass (double fc, double rate)
        {
            const double w = kTwoPi * fc / rate, c = std::cos (w), alpha = std::sin (w) / (2.0 * 0.7071), a0 = 1.0 + alpha;
            b0 = (1.0 + c) * 0.5 / a0;  b1 = -(1.0 + c) / a0;  b2 = b0;
            a1 = -2.0 * c / a0;         a2 = (1.0 - alpha) / a0;
        }

        double run (double x)
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    struct Result { double residueDb, pitchErrorPct; };

    // A sine goes in while the ratio is swept. A glide leaves nothing above 5 kHz; a head that
    // jumps on every control tick sprays energy up there, and the pitch wobbles around the target.
    const auto measure = [sr] (double lfoHz, double depthSt, double centreSt, double f0) -> Result
    {
        PitchShifter shifter;
        shifter.prepare (sr);

        const int n = (int) (4.0 * sr), from = (int) sr;
        std::vector<double> y ((size_t) n), ratioAt ((size_t) n);
        Highpass h1 (5000.0, sr), h2 (5000.0, sr);
        double high = 0.0, all = 0.0, ratio = 1.0;

        for (int i = 0; i < n; ++i)
        {
            if ((i & 15) == 0)
            {
                ratio = std::exp2 ((centreSt + depthSt * std::sin (kTwoPi * lfoHz * (double) i / sr)) / 12.0);
                shifter.setRatio (ratio, 16);
            }

            const double out = shifter.process (0.5 * std::sin (kTwoPi * f0 * (double) i / sr));
            y[(size_t) i] = out;
            ratioAt[(size_t) i] = ratio;

            const double h = h2.run (h1.run (out));
            if (i >= from) { high += h * h; all += out * out; }
        }

        double worst = 0.0;
        const int window = (int) (0.02 * sr);

        for (int start = from; start + window < n; start += window)
        {
            double first = -1.0, last = -1.0, meanRatio = 0.0;
            int crossings = 0;

            for (int i = start + 1; i < start + window; ++i)
            {
                meanRatio += ratioAt[(size_t) i];

                if (y[(size_t) (i - 1)] < 0.0 && y[(size_t) i] >= 0.0)
                {
                    const double t = (double) (i - 1) + y[(size_t) (i - 1)] / (y[(size_t) (i - 1)] - y[(size_t) i]);
                    if (first < 0.0) first = t;
                    last = t;
                    ++crossings;
                }
            }

            if (crossings >= 3)
            {
                const double f = (double) (crossings - 1) * sr / (last - first);
                worst = std::max (worst, 100.0 * std::abs (f / (f0 * meanRatio / (double) (window - 1)) - 1.0));
            }
        }

        return { 10.0 * std::log10 (high / all + 1.0e-30), worst };
    };

    struct Case { const char* name; double lfoHz, depthSt, centreSt, f0, maxResidueDb, maxErrorPct; };

    const Case cases[] = {
        { "+/-12 st at 1 Hz",          1.0,  12.0,  0.0, 440.0, -58.0, 2.5 },
        { "+/-12 st at 0.25 Hz",       0.25, 12.0,  0.0, 440.0, -58.0, 2.5 },
        { "+/-2 st at 5 Hz (vibrato)", 5.0,   2.0,  0.0, 440.0, -65.0, 2.5 },
        { "+7 +/-5 st at 1 Hz",        1.0,   5.0,  7.0, 220.0, -65.0, 2.5 },
        { "-7 +/-5 st at 1 Hz",        1.0,   5.0, -7.0, 880.0, -58.0, 2.5 },
    };

    for (const auto& c : cases)
    {
        const auto r = measure (c.lfoHz, c.depthSt, c.centreSt, c.f0);
        std::printf ("    sweep %-27s residue above 5 kHz %6.1f dB, worst pitch error over 20 ms %4.1f %%\n", c.name, r.residueDb, r.pitchErrorPct);
        check (r.residueDb < c.maxResidueDb, juce::String ("sweep ") + c.name + " is free of zipper noise");
        check (r.pitchErrorPct < c.maxErrorPct, juce::String ("sweep ") + c.name + " follows the pitch");
    }

    // the same through the whole engine: the PITCH lane of MOTION on a sustained tone
    {
        DlayEngine engine;
        engine.prepare (sr);

        auto p = cleanParams();
        p.mode = (int) Mode::pitch;
        p.timeMs = 120.0f;
        p.feedback = 0.0f;
        p.algoA = 0.0f;                                  // no detune
        p.lanes[(size_t) Lane::pitch] = { 1.0f, (int) LfoShape::sine, 1.0f, false, Div::bar1, 0.0f, 0.0f };

        const int n = (int) (4.0 * sr);
        StereoBuffer tone (n);
        for (int i = 0; i < n; ++i)
            tone.l[(size_t) i] = tone.r[(size_t) i] = 0.5f * (float) std::sin (kTwoPi * 440.0 * (double) i / sr);

        const auto out = renderThroughEngine (engine, tone, p, 256);

        Highpass h1 (5000.0, sr), h2 (5000.0, sr);
        double high = 0.0, all = 0.0;

        for (int i = 0; i < n; ++i)
        {
            const double h = h2.run (h1.run ((double) out.l[(size_t) i]));
            if (i >= (int) sr) { high += h * h; all += (double) out.l[(size_t) i] * out.l[(size_t) i]; }
        }

        const double residue = 10.0 * std::log10 (high / all + 1.0e-30);
        std::printf ("    PITCH lane at full depth, 1 Hz, through the engine: residue above 5 kHz %.1f dB\n", residue);
        check (all > 1.0 && residue < -55.0, "the PITCH lane of MOTION sweeps an echo without zipper noise");
    }
}

// ----------------------------------------------------------------------------
void testOutputStageGlides()
{
    section ("What is heard directly never steps");

    const double sr = 48000.0;
    const int blockSize = 256;

    // a steady signal, so that any step in level shows up as a step in the output
    const auto run = [&] (DlayAudioProcessor& proc, int blocks, float& last)
    {
        juce::AudioBuffer<float> block (2, blockSize);
        juce::MidiBuffer midi;
        float worst = 0.0f;

        for (int b = 0; b < blocks; ++b)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < blockSize; ++i)
                    block.setSample (ch, i, 0.5f);

            proc.processBlock (block, midi);

            for (int i = 0; i < blockSize; ++i)
            {
                worst = std::max (worst, std::abs (block.getSample (0, i) - last));
                last = block.getSample (0, i);
            }
        }

        return worst;
    };

    const auto makeProcessor = [&] (DlayAudioProcessor& proc)
    {
        proc.presets->loadInit();
        setParam (proc, id::sync, 0.0f);
        setParam (proc, id::time, 900.0f);              // the first echo arrives after the windows measured here
        setParam (proc, id::feedback, 0.0f);
        setParam (proc, id::mix, 50.0f);
        setParam (proc, id::limiter, 0.0f);
        proc.setRateAndBufferSizeDetails (sr, blockSize);
        proc.prepareToPlay (sr, blockSize);
    };

    {
        // host reset while bypassed and asleep, then un-bypass: the crossfade must still happen
        DlayAudioProcessor proc;
        makeProcessor (proc);
        setParam (proc, id::bypass, 1.0f);

        float last = 0.5f;
        run (proc, 40, last);
        check (last == 0.5f, "test set-up: bypassed and passing the input through");

        proc.reset();
        setParam (proc, id::bypass, 0.0f);

        const float worst = run (proc, 150, last);          // 0.8 s
        std::printf ("    un-bypass after a host reset: largest step %.5f, settles at %.4f (dry at 50 %% mix = 0.3536)\n", (double) worst, (double) last);
        check (worst < 0.002f, "un-bypass after a host reset crossfades (no step in the dry signal)");
        check (std::abs (last - 0.35355f) < 0.002f, "... and arrives at the processed signal");
    }

    {
        // a preset with a different MIX and OUTPUT: the dry signal crossfades to its new level
        DlayAudioProcessor proc;
        makeProcessor (proc);
        setParam (proc, id::mix, 20.0f);

        float last = 0.0f;
        run (proc, 60, last);
        const float before = last;

        setParam (proc, id::mix, 80.0f);
        setParam (proc, id::outGain, -6.0f);
        proc.engine.requestSoftReset();

        const float worst = run (proc, 150, last);
        const float expected = 0.5f * (float) (std::cos (0.8 * kPi * 0.5) * std::pow (10.0, -6.0 / 20.0));
        std::printf ("    MIX 20 -> 80 %% and OUTPUT 0 -> -6 dB with a reset: %.4f -> %.4f, largest step %.5f\n", (double) before, (double) last, (double) worst);
        check (std::abs (before - 0.5f * (float) std::cos (0.2 * kPi * 0.5)) < 0.002f, "test set-up: dry level at 20 % mix");
        check (worst < 0.002f, "a preset change moves the dry level without a step");
        check (std::abs (last - expected) < 0.002f, "... to the new level");
    }
}

// ----------------------------------------------------------------------------
/** A host restores a session from a thread of its own. Nothing may be touched over there,
    the state must already be reported back, and the message thread applies it.

    This thread is JUCE's message thread, and the test plays that part itself instead of
    running the system's message loop (a command-line tool does not have one on every
    platform): applyPendingState() is exactly what the loop calls when it gets round to it. */
void testStateRestoreFromAnotherThread()
{
    section ("Session restore from a thread that is not the message thread");

    DlayAudioProcessor source, target, overridden;
    juce::MemoryBlock sourceBlob, otherBlob;

    source.presets->load (source.presets->getNumFactoryPresets() / 2);
    setParam (source, id::feedback, 77.0f);
    setParam (source, id::time, 1234.0f);
    source.curves->setPoints ((int) Lane::pan, { { 0.0f, 0.2f, 0.0f }, { 0.4f, 0.9f, 0.5f }, { 1.0f, 0.1f, 0.0f } });
    source.getStateInformation (sourceBlob);

    const float sourceFeedback  = getParam (source, id::feedback);
    const float defaultFeedback = getParam (target, id::feedback);

    setParam (overridden, id::feedback, 12.0f);
    overridden.getStateInformation (otherBlob);
    setParam (overridden, id::feedback, 55.0f);

    check (juce::MessageManager::getInstance()->isThisTheMessageThread(), "test set-up: this is the message thread");
    check (std::abs (sourceFeedback - defaultFeedback) > 1.0f, "test set-up: the two states differ");

    // an edit, so that there is an undo history to protect
    target.undoManager.beginNewTransaction();
    target.curves->setPoints ((int) Lane::mix, { { 0.0f, 0.3f, 0.0f }, { 1.0f, 0.6f, 0.0f } }, true);
    const bool hadUndo = target.undoManager.canUndo();

    std::thread firstCall ([&]
    {
        target.setStateInformation (sourceBlob.getData(), (int) sourceBlob.getSize());
        overridden.setStateInformation (sourceBlob.getData(), (int) sourceBlob.getSize());
    });
    firstCall.join();

    // 1. nothing that belongs to the message thread was touched over there
    check (std::abs (getParam (target, id::feedback) - defaultFeedback) < 1.0e-4f,
           "the calling thread does not touch the parameter tree");
    check (hadUndo && target.undoManager.canUndo(), "... nor the undo history");

    // 2. but the host already gets the new state back when it asks
    {
        struct Access : DlayAudioProcessor { using DlayAudioProcessor::getXmlFromBinary; };

        juce::MemoryBlock now;
        target.getStateInformation (now);
        const auto a = Access::getXmlFromBinary (now.getData(), (int) now.getSize());
        const auto b = Access::getXmlFromBinary (sourceBlob.getData(), (int) sourceBlob.getSize());
        check (a != nullptr && b != nullptr && a->isEquivalentTo (b.get(), true), "the state is reported back at once, before it has been applied");
    }

    // 3. a restore on the message thread in the meantime supersedes the one that is waiting
    overridden.setStateInformation (otherBlob.getData(), (int) otherBlob.getSize());

    // 4. the message thread gets round to the hand-over
    target.applyPendingState();
    overridden.applyPendingState();

    check (std::abs (getParam (target, id::feedback) - 77.0f) < 0.01f && std::abs (getParam (target, id::time) - 1234.0f) < 0.01f,
           "the message thread applies the restored parameters");

    {
        const auto points = target.curves->getPoints ((int) Lane::pan);
        check (points.size() == 3 && std::abs (points[1].y - 0.9f) < 1.0e-5f, "... and the MOTION curves");
    }

    check (target.presets->getCurrentName() == source.presets->getCurrentName(), "... and the preset name");
    check (! target.undoManager.canUndo(), "... and clears the undo history there");
    check (std::abs (getParam (overridden, id::feedback) - 12.0f) < 0.01f, "a later restore is not overwritten by an earlier one that was still waiting");

    // 5. A host that keeps restoring from its own thread, while audio runs on another and
    //    this thread reads everything an open editor reads. In a normal build this must
    //    simply survive and end on the last state; in a ThreadSanitizer build
    //    (-DDLAY_SANITIZE=thread) it shows that the three threads never touch the same data
    //    without an atomic or a lock in between.
    target.setRateAndBufferSizeDetails (48000.0, 128);
    target.prepareToPlay (48000.0, 128);

    std::atomic<bool> audioRunning { true }, audioFinite { true }, hostDone { false };
    std::atomic<int> audioBlocks { 0 };

    std::thread audioThread ([&]
    {
        juce::AudioBuffer<float> block (2, 128);
        juce::MidiBuffer midi;
        int n = 0;

        while (audioRunning.load())
        {
            for (int i = 0; i < 128; ++i, ++n)
            {
                const float v = 0.4f * std::sin (0.031f * (float) n);
                block.setSample (0, i, v);
                block.setSample (1, i, -v);
            }

            target.processBlock (block, midi);

            for (int i = 0; i < 128; ++i)
                if (! std::isfinite (block.getSample (0, i)) || ! std::isfinite (block.getSample (1, i)))
                    audioFinite = false;

            ++audioBlocks;
        }
    });

    std::thread hostThread ([&]
    {
        for (int i = 0; i < 300; ++i)
        {
            const auto& blob = (i % 2) == 0 ? otherBlob : sourceBlob;
            target.setStateInformation (blob.getData(), (int) blob.getSize());
            std::this_thread::sleep_for (std::chrono::microseconds (400));
        }

        hostDone = true;
    });

    int reads = 0, applied = 0;
    double checksum = 0.0;
    float lastSeen = getParam (target, id::feedback);

    while (! hostDone.load())
    {
        target.applyPendingState();                     // the message loop delivering a hand-over

        // what an open editor reads
        checksum += target.getUiAdvanced() ? 1.0 : 0.0;
        checksum += target.getUiScale() + (double) target.getUiLane();

        for (int l = 0; l < kNumLanes; ++l)
            for (const auto& point : target.curves->getPoints (l))
                checksum += (double) point.y;

        checksum += (double) target.presets->getCurrentName().length();
        checksum += target.presets->isModified() ? 1.0 : 0.0;
        checksum += target.undoManager.canUndo() ? 1.0 : 0.0;

        const float now = getParam (target, id::feedback);
        checksum += (double) now;
        if (now != lastSeen) { lastSeen = now; ++applied; }
        ++reads;
    }

    hostThread.join();
    target.applyPendingState();
    audioRunning = false;
    audioThread.join();

    check (reads > 20 && applied > 2,
           "the message thread kept reading and applying while the host thread restored 300 times ("
             + juce::String (reads) + " reads, " + juce::String (applied) + " states applied)");
    check (audioBlocks.load() > 20 && audioFinite.load(), "... and audio kept running through it (" + juce::String (audioBlocks.load()) + " blocks)");
    check (std::abs (getParam (target, id::feedback) - 77.0f) < 0.01f && std::isfinite (checksum), "... and the last restore is the one in effect");
}

// ============================================================================
struct PresetFeatures
{
    std::vector<double> v;
};

PresetFeatures analyse (const StereoBuffer& wet, double sr)
{
    // coarse time/level/stereo/brightness fingerprint of the wet signal
    PresetFeatures f;
    const int hop = (int) (0.05 * sr);

    for (int start = 0; start + hop <= wet.size(); start += hop)
    {
        double el = 0.0, er = 0.0, diff = 0.0;
        for (int i = start; i < start + hop; ++i)
        {
            el += (double) wet.l[(size_t) i] * wet.l[(size_t) i];
            er += (double) wet.r[(size_t) i] * wet.r[(size_t) i];
            if (i > start)
            {
                const double d = (double) wet.l[(size_t) i] - wet.l[(size_t) (i - 1)] + (double) wet.r[(size_t) i] - wet.r[(size_t) (i - 1)];
                diff += d * d;
            }
        }

        const double level = std::sqrt ((el + er) / (2.0 * hop));
        f.v.push_back (std::log10 (level + 1.0e-4) + 4.0);                              // 0 .. ~4
        f.v.push_back (level > 1.0e-4 ? (er - el) / (er + el + 1.0e-12) : 0.0);         // stereo balance
        f.v.push_back (level > 1.0e-4 ? std::min (2.0, std::sqrt (diff / (el + er + 1.0e-12))) : 0.0);   // brightness
    }

    return f;
}

double distance (const PresetFeatures& a, const PresetFeatures& b)
{
    double acc = 0.0;
    const size_t n = std::min (a.v.size(), b.v.size());
    for (size_t i = 0; i < n; ++i)
        acc += (a.v[i] - b.v[i]) * (a.v[i] - b.v[i]);
    return std::sqrt (acc / (double) std::max<size_t> (1, n));
}

void testPresets (bool verbose, bool quick)
{
    section ("Factory preset library");

    DlayAudioProcessor proc;
    const auto& entries = proc.presets->getEntries();
    const int numFactory = proc.presets->getNumFactoryPresets();

    std::printf ("    %d factory presets\n", numFactory);
    check (numFactory >= 150, "at least 150 factory presets");

    // names unique, every category populated
    {
        juce::StringArray names;
        std::map<juce::String, int> perCategory;

        for (int i = 0; i < numFactory; ++i)
        {
            check (! names.contains (entries[(size_t) i].name), "preset name is unique: " + entries[(size_t) i].name);
            names.add (entries[(size_t) i].name);
            ++perCategory[entries[(size_t) i].category];
            check (entries[(size_t) i].description.isNotEmpty(), "preset has a description: " + entries[(size_t) i].name);
        }

        for (const auto* cat : kCategoryNames)
        {
            std::printf ("    %-26s %3d\n", cat, perCategory[cat]);
            check (perCategory[cat] >= 8, juce::String ("category has at least 8 presets: ") + cat);
        }

        static const char* required[] = { "Ghost Machine", "Time Traveler", "VHS Dreams", "Infinite Corridor", "Neon Rain",
                                          "Broken Clock", "Reverse Universe", "Melting Memories", "Digital Ghosts", "Tape Collapse",
                                          "Spiral Galaxy", "Frozen Echo", "Liquid Motion", "Midnight Transmission", "Hyperdrive",
                                          "Dream Sequence", "Quantum Echo", "Falling Through Time", "Radioactive Tape", "The Last Transmission" };
        for (const auto* r : required)
            check (names.contains (r), juce::String ("named preset exists: ") + r);
    }

    // every preset: loads real values, renders cleanly, and sounds different from every other
    const double sr = 48000.0;
    const auto input = makeTestSignal (sr, 9.0, 3.0, 3);
    std::vector<PresetFeatures> features;
    std::vector<juce::String> signatures;

    for (int i = 0; i < numFactory; ++i)
    {
        proc.presets->load (i);
        setParam (proc, id::mix, 100.0f);       // compare the wet signal only

        juce::String signature;
        for (auto* param : proc.getParameters())
            signature << juce::String (param->getValue(), 4) << ",";
        for (int l = 0; l < kNumLanes; ++l)
            for (const auto& pt : proc.curves->getPoints (l))
                signature << juce::String (pt.x, 3) << juce::String (pt.y, 3);

        check (! signatures.empty() ? std::find (signatures.begin(), signatures.end(), signature) == signatures.end() : true,
               "preset has a unique parameter set: " + entries[(size_t) i].name);
        signatures.push_back (signature);

        if (quick)
            continue;                   // loading and comparing parameter sets only

        proc.engine.reset();
        const auto out = renderThroughProcessor (proc, input, sr, 512);

        const double level = out.rms (0, (int) (4.0 * sr));
        const bool ok = out.allFinite() && out.peak() <= 1.0f && level > 2.0e-3;

        if (! ok || verbose)
            std::printf ("    %-28s rms %.4f  peak %.3f\n", entries[(size_t) i].name.toRawUTF8(), level, out.peak());

        check (out.allFinite(), "finite: " + entries[(size_t) i].name);
        check (out.peak() <= 1.0f, "no clipping: " + entries[(size_t) i].name);
        check (level > 2.0e-3, "audible: " + entries[(size_t) i].name + " (rms " + juce::String (level, 5) + ")");
        check (level < 0.5, "not excessively loud: " + entries[(size_t) i].name + " (rms " + juce::String (level, 4) + ")");

        features.push_back (analyse (out, sr));
    }

    double closest = 1.0e9;
    int ca = 0, cb = 0, tooClose = 0;

    for (int a = 0; a < (int) features.size(); ++a)
    {
        for (int b = a + 1; b < (int) features.size(); ++b)
        {
            const double d = distance (features[(size_t) a], features[(size_t) b]);
            if (d < closest) { closest = d; ca = a; cb = b; }

            if (d < 0.06)
            {
                ++tooClose;
                std::printf ("    too similar (%.3f): %s  <->  %s\n", d, entries[(size_t) a].name.toRawUTF8(), entries[(size_t) b].name.toRawUTF8());
            }
        }
    }

    if (features.size() > 1)
        std::printf ("    closest pair: %s <-> %s (distance %.3f)\n", entries[(size_t) ca].name.toRawUTF8(), entries[(size_t) cb].name.toRawUTF8(), closest);

    check (tooClose == 0, "every preset renders an audibly different result");

    // user preset save / reload / delete
    {
        proc.presets->load (std::min (3, numFactory - 1));
        setParam (proc, id::time, 777.0f);
        setParam (proc, id::sync, 0.0f);
        const auto before = getParam (proc, id::feedback);

        const auto result = proc.presets->saveUserPreset ("__dlay_selftest__", "User", "written by the test runner");
        check (result.wasOk(), "user preset saved");

        proc.presets->loadInit();
        check (std::abs (getParam (proc, id::time) - 375.0f) < 0.01f, "init restores defaults");

        int index = -1;
        for (int i = 0; i < (int) proc.presets->getEntries().size(); ++i)
            if (proc.presets->getEntries()[(size_t) i].name == "__dlay_selftest__")
                index = i;

        check (index >= 0, "saved preset appears in the library");

        if (index >= 0)
        {
            proc.presets->load (index);
            check (std::abs (getParam (proc, id::time) - 777.0f) < 0.01f && std::abs (getParam (proc, id::feedback) - before) < 0.01f,
                   "user preset recalls its values");
            proc.presets->setFavourite (index, true);
            check (proc.presets->isFavourite (index), "favourite flag is stored");
            check (! proc.presets->filter ("selftest", {}, true).empty(), "search + favourites filter finds it");
            check (proc.presets->deleteUserPreset (index), "user preset deleted");
        }
    }
}

// ============================================================================
//  Data the plug-in did not write itself. There are two kinds: the session state a host
//  hands back, and preset files that people pass around. Neither may crash the plug-in,
//  stall it, make it read another file, or leave a value in it that is not a usable number.
// ============================================================================
void testForeignData (int rounds)
{
    section ("Session and preset data that is damaged, or written to do damage");

    struct Access : DlayAudioProcessor
    {
        using DlayAudioProcessor::getXmlFromBinary;
    };

    const double sr = 48000.0;
    dlay::Rng rng;
    rng.seed (20261119u);
    const auto pick = [&rng] (int n) { return n <= 1 ? 0 : (int) (rng.nextU32() % (uint32_t) n); };
    const auto now  = [] { return juce::Time::getMillisecondCounterHiRes(); };

    // text that numbers, names and paths are replaced with
    const juce::StringArray hostile { "nan", "-nan", "inf", "-inf", "1e308", "-1e308", "1e-320", "1e99999", "", " ", "abc",
                                      "0x7fffffff", "99999999999999999999999999", "-0", "1.#QNAN", "true", "&undefined;",
                                      "../../../../etc/passwd", "\\\\host\\share\\x", "%s%s%s%n", "]]>", "&#0;", "&#x110000;",
                                      juce::String::repeatedString ("A", 70000), juce::String::repeatedString ("9", 400) };

    // one edit to a piece of XML text: the result is sometimes still XML and often not
    const auto mutate = [&] (std::string text)
    {
        const int edits = 1 + pick (4);

        for (int e = 0; e < edits && ! text.empty(); ++e)
        {
            const size_t at = (size_t) pick ((int) text.size());

            switch (pick (9))
            {
                case 0:  text[at] = (char) pick (256); break;                                           // any byte
                case 1:  text[at] = "<>&\"'=/ \t\n"[pick (10)]; break;                                  // a byte that means something
                case 2:  text.resize (at); break;                                                       // cut off
                case 3:  text.insert (at, text.substr ((size_t) pick ((int) text.size()), (size_t) pick (400))); break;   // a piece twice
                case 4:  text.erase (at, (size_t) pick (200)); break;                                   // a piece missing
                case 5:                                                                                  // a value replaced
                case 6:
                {
                    const auto open = text.find ("=\"", at);
                    const auto close = open == std::string::npos ? open : text.find ('"', open + 2);
                    if (close != std::string::npos)
                        text.replace (open + 2, close - open - 2, hostile[pick (hostile.size())].toStdString());
                    break;
                }
                case 7:                                                                                  // an element many times
                {
                    const auto open = text.find ('<', at);
                    const auto close = open == std::string::npos ? open : text.find ('>', open);
                    if (close != std::string::npos && close - open < 400)
                    {
                        const auto element = text.substr (open, close - open + 1);
                        std::string many;
                        for (int i = 1 + pick (3000); --i >= 0;) many += element;
                        text.insert (open, many);
                    }
                    break;
                }
                default: text.insert (at, std::string ((size_t) pick (64), (char) pick (256))); break;  // noise
            }
        }

        return text;
    };

    // a session as a host stores it: an 8-byte header, then the text
    const auto asSession = [] (const std::string& text, int statedLength)
    {
        juce::MemoryBlock block;
        juce::MemoryOutputStream out (block, false);
        out.writeInt (0x21324356);
        out.writeInt (statedLength);
        out.write (text.data(), text.size());
        out.writeByte (0);
        out.flush();
        return block;
    };

    // everything the plug-in holds is a usable number, and it plays
    juce::AudioBuffer<float> block (2, 256);
    juce::MidiBuffer midi;

    const auto healthy = [&] (DlayAudioProcessor& proc) -> juce::String
    {
        for (auto* param : proc.getParameters())
        {
            const float v = param->getValue();
            if (! (v >= 0.0f && v <= 1.0f))
                return "parameter " + param->getName (40) + " = " + juce::String (v);
        }

        for (int l = 0; l < kNumLanes; ++l)
        {
            for (const auto& pt : proc.curves->getPoints (l))
                if (! (pt.x >= 0.0f && pt.x <= 1.0f && pt.y >= 0.0f && pt.y <= 1.0f && pt.curve >= -1.0f && pt.curve <= 1.0f))
                    return "a curve point of lane " + juce::String (l);

            for (int i = 0; i < MotionEngine::kTableSize; i += 17)
                if (! (std::abs (proc.engine.motion.tableValue (l, i)) <= 1.0f))
                    return "the curve table of lane " + juce::String (l);
        }

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < block.getNumSamples(); ++i)
                block.setSample (ch, i, 0.4f * std::sin (0.05f * (float) i * (float) (ch + 1)));

        proc.processBlock (block, midi);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < block.getNumSamples(); ++i)
                if (! std::isfinite (block.getSample (ch, i)))
                    return "the audio output";

        return {};
    };

    // ------------------------------------------------------------------ sessions
    DlayAudioProcessor proc;
    proc.setRateAndBufferSizeDetails (sr, 256);
    proc.prepareToPlay (sr, 256);

    std::string sessionText;
    {
        DlayAudioProcessor source;

        for (auto* param : source.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
                ranged->setValueNotifyingHost (ranged->convertTo0to1 (ranged->convertFrom0to1 ((float) rng.next01())));

        for (int l = 0; l < kNumLanes; ++l)
            source.curves->setPoints (l, { { 0.0f, 0.2f, 0.3f }, { 0.3f, 0.9f, -0.6f }, { 0.7f, 0.1f, 0.0f }, { 1.0f, 0.6f, 0.0f } });

        juce::MemoryBlock blob;
        source.getStateInformation (blob);

        if (const auto xml = Access::getXmlFromBinary (blob.getData(), (int) blob.getSize()))
            sessionText = xml->toString (juce::XmlElement::TextFormat().singleLine()).toStdString();
    }

    check (sessionText.size() > 2000, "test set-up: a full session to damage (" + juce::String ((int) sessionText.size()) + " characters)");

    {
        juce::String firstProblem;
        int taken = 0;
        const double start = now();

        for (int round = 0; round < rounds && firstProblem.isEmpty(); ++round)
        {
            const auto text = mutate (sessionText);
            const int stated = pick (8) == 0 ? (pick (2) == 0 ? -1 - pick (1000) : std::numeric_limits<int>::max() - pick (1000))
                                             : (int) text.size();
            const auto data = asSession (text, stated);

            // also handed over short, as a host with a damaged project file would
            const int size = pick (6) == 0 ? pick ((int) data.getSize() + 1) : (int) data.getSize();
            proc.setStateInformation (data.getData(), size);

            // (counted the way the plug-in looks at it: is it text, then: is it XML)
            const int textBytes = juce::jmin (size - 8, stated);

            if (size > 8 && textBytes > 0
                 && juce::CharPointer_UTF8::isValidString (static_cast<const char*> (data.getData()) + 8, textBytes)
                 && Access::getXmlFromBinary (data.getData(), size) != nullptr)
                ++taken;

            const auto problem = healthy (proc);
            if (problem.isNotEmpty())
                firstProblem = "round " + juce::String (round) + ": " + problem;
        }

        std::printf ("  %d damaged sessions in %.1f s; %d of them were still readable as XML\n", rounds, (now() - start) / 1000.0, taken);
        check (firstProblem.isEmpty(), "after every damaged session all values are usable numbers and audio comes out (" + firstProblem + ")");
        check (firstProblem.isNotEmpty() || taken > rounds / 20,
               "test set-up: a fair share of the damaged sessions gets past the XML reader (" + juce::String (taken) + ")");
    }

    // ------------------------------------------------------------------ every value replaced at once
    // The interface is opened on each of these as well: it reads the same parameters, and
    // uses several of them to pick an entry from a list.
    const auto opensAndPaints = [] (DlayAudioProcessor& p) -> bool
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorAndMakeActive());

        if (editor == nullptr)
            return false;

        editor->setVisible (true);
        const auto picture = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        const bool ok = picture.isValid() && picture.getWidth() >= 600 && picture.getHeight() >= 300;

        p.editorBeingDeleted (editor.get());
        editor.reset();
        return ok;
    };

    {
        const auto withEveryValue = [&] (const std::string& value)
        {
            auto text = sessionText;

            for (size_t at = text.find (" value=\""); at != std::string::npos; at = text.find (" value=\"", at + 1))
            {
                const auto end = text.find ('"', at + 8);
                text.replace (at + 8, end - at - 8, value);
            }

            return text;
        };

        check (withEveryValue ("nan") != sessionText, "test set-up: the parameter values of a session can be replaced");

        // the defaults, to compare with
        DlayAudioProcessor fresh;
        fresh.presets->loadInit();
        std::map<juce::String, float> defaults;
        for (auto* param : fresh.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
                defaults[ranged->getParameterID()] = ranged->getDefaultValue();

        for (const char* value : { "nan", "-nan", "inf", "-inf", "1e308", "-1e308", "abc", "" })
        {
            const auto text = withEveryValue (value);
            const auto data = asSession (text, (int) text.size());
            proc.setStateInformation (data.getData(), (int) data.getSize());

            const auto problem = healthy (proc);
            check (problem.isEmpty(), juce::String ("a session in which every value reads \"") + value + "\" leaves usable numbers and audio (" + problem + ")");
            check (opensAndPaints (proc), juce::String ("... and the interface opens and paints on it"));

            // what is not a number at all must end at the default, not at whatever was there before
            if (juce::String (value).containsIgnoreCase ("nan") || juce::String (value).containsIgnoreCase ("inf") || juce::String (value).contains ("e308"))
            {
                bool atDefault = true;
                for (auto* param : proc.getParameters())
                    if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
                        atDefault = atDefault && std::abs (ranged->getValue() - defaults[ranged->getParameterID()]) < 1.0e-5f;

                check (atDefault, juce::String ("... and every parameter is at its default after \"") + value + "\"");
            }
        }

        // the settings of the editor, out of range and not numbers
        for (const char* value : { "nan", "-5", "999999999999", "1e308", "abc" })
        {
            auto text = sessionText;
            const auto root = text.find ("<DLAY");
            text.insert (root + 5, std::string (" uiZoom=\"") + value + "\" uiPage=\"" + value + "\" uiLane=\"" + value + "\" uiAdvanced=\"" + value + "\"");
            const auto data = asSession (text, (int) text.size());
            proc.setStateInformation (data.getData(), (int) data.getSize());

            const double zoom = proc.getUiScale();
            check (std::isfinite (zoom) && zoom >= 0.0 && zoom <= 4.0 && proc.getUiPage() >= 0 && proc.getUiPage() <= 15
                    && proc.getUiLane() >= 0 && proc.getUiLane() < kNumLanes,
                   juce::String ("editor settings that read \"") + value + "\" are not taken over (zoom " + juce::String (zoom) + ", page "
                    + juce::String (proc.getUiPage()) + ", lane " + juce::String (proc.getUiLane()) + ")");
            check (opensAndPaints (proc), juce::String ("... and the interface opens and paints"));
        }
    }

    // ------------------------------------------------------------------ sessions made to do damage
    const juce::String leak ("TEXT-FROM-ANOTHER-FILE");
    const auto elementsNested = [] (int depth)
    {
        std::string s;
        for (int i = 0; i < depth; ++i) s += "<a>";
        for (int i = 0; i < depth; ++i) s += "</a>";
        return s;
    };
    const auto manyPoints = [] (int count)
    {
        std::string s = "<MOTION><LANE lane=\"3\">";
        for (int i = 0; i < count; ++i) s += "<PT x=\"0." + std::to_string (i % 1000) + "\" y=\"0." + std::to_string ((i * 7) % 1000) + "\" c=\"0\"/>";
        return s + "</LANE></MOTION>";
    };
    const auto withCurves = [] (std::string text, const std::string& motion)
    {
        const auto from = text.find ("<MOTION"), to = text.find ("</MOTION>");
        if (from != std::string::npos && to != std::string::npos)
            text.replace (from, to + 9 - from, motion);
        return text;
    };
    const std::string laughs = "<?xml version=\"1.0\"?><!DOCTYPE x [<!ENTITY a \"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\">"
                               "<!ENTITY b \"&a;&a;&a;&a;&a;&a;&a;&a;&a;&a;\"><!ENTITY c \"&b;&b;&b;&b;&b;&b;&b;&b;&b;&b;\">"
                               "<!ENTITY d \"&c;&c;&c;&c;&c;&c;&c;&c;&c;&c;\"><!ENTITY e \"&d;&d;&d;&d;&d;&d;&d;&d;&d;&d;\">"
                               "<!ENTITY f \"&e;&e;&e;&e;&e;&e;&e;&e;&e;&e;\"><!ENTITY g \"&f;&f;&f;&f;&f;&f;&f;&f;&f;&f;\">"
                               "<!ENTITY h \"&g;&g;&g;&g;&g;&g;&g;&g;&g;&g;\"><!ENTITY i \"&h;&h;&h;&h;&h;&h;&h;&h;&h;&h;\">]>";

    {
        const auto closing = sessionText.rfind ("</");
        check (closing != std::string::npos, "test set-up: the session text ends in a closing tag");
        const auto inject = [&] (const std::string& piece) { auto t = sessionText; t.insert (closing, piece); return t; };

        const auto withName = [&] (const std::string& name)
        {
            auto t = sessionText;
            const auto root = t.find ("<DLAY");
            t.insert (root + 5, " presetName=\"" + name + "\"");
            return t;
        };

        struct Case { const char* what; std::string text; };
        const std::vector<Case> cases {
            { "100,000 elements inside each other",        inject (elementsNested (100000)) },
            { "300 elements inside each other",            inject (elementsNested (300)) },
            { "a curve of 300,000 points",                 withCurves (sessionText, manyPoints (300000)) },
            { "a curve of 9,000 points",                   withCurves (sessionText, manyPoints (9000)) },
            { "a second set of curves, 9,000 points",      inject (manyPoints (9000)) },
            { "text that multiplies itself (entities)",    laughs + inject ("<n v=\"&i;\"/>") },
            { "8 million characters of something else",    inject ("<n v=\"" + std::string ((size_t) 8000000, 'x') + "\"/>") },
            { "300,000 characters of something else",      inject ("<n v=\"" + std::string ((size_t) 300000, 'x') + "\"/><o>" + std::string ((size_t) 100000, 'y') + "</o>") },
            { "a preset name of 300,000 characters",       withName (std::string ((size_t) 300000, 'N')) },
            { "2,000 settings it does not know",           [&] { auto t = sessionText; std::string extra;
                                                                 for (int i = 0; i < 2000; ++i) extra += " x" + std::to_string (i) + "=\"" + std::string (200, 'z') + "\"";
                                                                 t.insert (t.find ("<DLAY") + 5, extra); return t; }() },
            { "every parameter 300 times",                 [&] { auto t = sessionText; const auto from = t.find ("<PARAM"); const auto to = t.rfind ("<MOTION");
                                                                 const auto params = t.substr (from, to - from); std::string many;
                                                                 for (int i = 0; i < 300 && many.size() + t.size() < 450000; ++i) many += params;
                                                                 t.insert (from, many); return t; }() },
        };

        for (const auto& c : cases)
        {
            const auto data = asSession (c.text, (int) c.text.size());
            const double start = now();
            proc.setStateInformation (data.getData(), (int) data.getSize());
            const auto problem = healthy (proc);
            juce::MemoryBlock saved;
            proc.getStateInformation (saved);
            const double ms = now() - start;

            size_t mostPoints = 0;
            for (int l = 0; l < kNumLanes; ++l)
                mostPoints = std::max (mostPoints, proc.curves->getPoints (l).size());

            std::printf ("  session with %-42s %7.0f ms, saved again as %d kB\n", c.what, ms, (int) (saved.getSize() / 1024));
            check (problem.isEmpty(), juce::String ("a session with ") + c.what + " leaves the plug-in working (" + problem + ")");
            check (ms < 1500.0, juce::String ("a session with ") + c.what + " does not stall the host (" + juce::String (ms, 0) + " ms)");
            check (saved.getSize() < 64 * 1024, juce::String ("a session with ") + c.what + " is not carried into the next save ("
                                                 + juce::String ((int) (saved.getSize() / 1024)) + " kB)");
            check (mostPoints <= (size_t) MotionCurves::kMaxPoints && proc.presets->getCurrentName().length() <= 80,
                   juce::String ("... and no curve or name is longer than the plug-in makes them (") + juce::String ((int) mostPoints) + " points, "
                    + juce::String (proc.presets->getCurrentName().length()) + " characters)");
        }

        // a long curve that is accepted keeps its shape: thinned out, not cut off
        {
            std::string lane = "<MOTION><LANE lane=\"2\">";
            for (int i = 0; i <= 4000; ++i)
                lane += "<PT x=\"" + std::to_string ((double) i / 4000.0) + "\" y=\"" + std::to_string (0.5 + 0.4 * std::sin (6.2831853 * (double) i / 4000.0)) + "\" c=\"0\"/>";
            lane += "</LANE></MOTION>";

            const auto text = withCurves (sessionText, lane);
            check (text != sessionText, "test set-up: the session text holds the curves");

            const auto data = asSession (text, (int) text.size());
            proc.setStateInformation (data.getData(), (int) data.getSize());

            const auto points = proc.curves->getPoints (2);
            double worst = 0.0;
            for (int i = 0; i <= 200; ++i)
            {
                const float x = (float) i / 200.0f;
                worst = std::max (worst, std::abs ((double) MotionCurves::evaluate (points, x) - (0.5 + 0.4 * std::sin (6.2831853 * (double) x))));
            }

            check ((int) points.size() == MotionCurves::kMaxPoints && points.front().x == 0.0f && points.back().x == 1.0f && worst < 0.005,
                   "a curve of 4,001 points is thinned to " + juce::String (MotionCurves::kMaxPoints) + " and keeps its shape (off by "
                    + juce::String (worst, 5) + " at most, " + juce::String ((int) points.size()) + " points)");
        }
    }

    // ------------------------------------------------------------------ preset files
    const auto presetDir = proc.presets->getUserPresetDirectory();
    const auto workDir   = presetDir.getChildFile ("__dlay_selftest__");
    workDir.deleteRecursively();
    check (workDir.createDirectory().wasOk(), "test set-up: a folder for the damaged presets");

    std::string presetText;
    {
        proc.presets->loadInit();
        proc.curves->setPoints ((int) Lane::pan, { { 0.0f, 0.1f, 0.2f }, { 0.5f, 0.9f, -0.2f }, { 1.0f, 0.4f, 0.0f } });
        check (proc.presets->saveUserPreset ("__dlay_selftest_seed__", "User", "the preset the damaged ones are made from").wasOk(), "test set-up: a preset to damage");

        for (const auto& e : proc.presets->getEntries())
            if (e.name == "__dlay_selftest_seed__")
            {
                presetText = e.file.loadFileAsString().toStdString();
                e.file.deleteFile();
            }
    }

    check (presetText.size() > 1000, "test set-up: the preset file was read back (" + juce::String ((int) presetText.size()) + " characters)");

    const auto loadEveryPresetIn = [&] (const juce::String& marker, juce::String& firstProblem)
    {
        proc.presets->rescan();
        int loaded = 0;

        for (int i = proc.presets->getNumFactoryPresets(); i < (int) proc.presets->getEntries().size(); ++i)
        {
            const auto entry = proc.presets->getEntries()[(size_t) i];
            if (! entry.file.isAChildOf (workDir))
                continue;                                   // somebody's own presets: not ours to load

            if ((entry.name + entry.category + entry.description).contains (leak) && firstProblem.isEmpty())
                firstProblem = marker + ": text of another file shows up in " + entry.file.getFileName();

            proc.presets->load (i);
            ++loaded;

            const auto problem = healthy (proc);
            if (problem.isNotEmpty() && firstProblem.isEmpty())
                firstProblem = marker + " " + entry.file.getFileName() + ": " + problem;

            if ((proc.presets->getCurrentName() + proc.presets->getCurrentCategory()).contains (leak) && firstProblem.isEmpty())
                firstProblem = marker + ": text of another file shows up in the loaded preset";
        }

        return loaded;
    };

    {
        juce::String firstProblem;
        int listed = 0;
        const int batch = 40;
        const double start = now();

        for (int done = 0; done < rounds && firstProblem.isEmpty(); done += batch)
        {
            for (int i = 0; i < batch; ++i)
            {
                const auto text = mutate (presetText);
                workDir.getChildFile ("damaged " + juce::String (i) + PresetManager::fileExtension).replaceWithData (text.data(), text.size());
            }

            listed += loadEveryPresetIn ("batch " + juce::String (done / batch), firstProblem);
        }

        std::printf ("  %d damaged preset files in %.1f s; %d of them were listed and loaded\n", rounds, (now() - start) / 1000.0, listed);
        check (firstProblem.isEmpty(), "after every damaged preset file all values are usable numbers and audio comes out (" + firstProblem + ")");
        check (listed > rounds / 20, "test set-up: a fair share of the damaged presets is still listed (" + juce::String (listed) + ")");

        for (int i = 0; i < batch; ++i)
            workDir.getChildFile ("damaged " + juce::String (i) + PresetManager::fileExtension).deleteFile();
    }

    // ------------------------------------------------------------------ preset files made to do damage
    {
        // a second file next to the preset, which the preset asks the reader to pull in
        const auto other = workDir.getChildFile ("other.dtd");
        other.replaceWithText ("<!ENTITY leak \"" + leak + "\">");

        const auto root = presetText.find ("<DLAYPRESET");
        check (root != std::string::npos, "test set-up: the preset text has the expected root element");
        const auto body = presetText.substr (root == std::string::npos ? 0 : root);
        const auto closing = body.rfind ("</");
        const auto inject = [&] (const std::string& piece) { auto t = body; t.insert (closing, piece); return t; };
        const auto named = [&] (const std::string& name)
        {
            auto t = body;
            const auto at = t.find ("name=\"");
            const auto end = t.find ('"', at + 6);
            t.replace (at + 6, end - at - 6, name);
            return t;
        };

        struct Case { const char* what; std::string text; };
        const std::vector<Case> cases {
            { "a pointer to another file (relative)",      "<?xml version=\"1.0\"?><!DOCTYPE DLAYPRESET SYSTEM \"other.dtd\">" + named ("&leak;") },
            { "a pointer to another file (full path)",     "<?xml version=\"1.0\"?><!DOCTYPE DLAYPRESET SYSTEM \"" + other.getFullPathName().toStdString() + "\">" + named ("&leak;") },
            { "a pointer to another file (in the header)", "<?xml version=\"1.0\"?><!DOCTYPE DLAYPRESET [<!ENTITY % ext SYSTEM \"other.dtd\"> %ext;]>" + named ("&leak;") },
            { "an entity that names another file",         "<?xml version=\"1.0\"?><!DOCTYPE DLAYPRESET [<!ENTITY leak SYSTEM \"other.dtd\">]>" + named ("&leak;") },
            { "100,000 elements inside each other",        inject (elementsNested (100000)) },
            { "300 elements inside each other",            inject (elementsNested (300)) },
            { "a curve of 300,000 points",                 withCurves (body, manyPoints (300000)) },
            { "a curve of 9,000 points",                   withCurves (body, manyPoints (9000)) },
            { "text that multiplies itself (entities)",    laughs + named ("&i;") },
            { "a name of 8 million characters",            named (std::string ((size_t) 8000000, 'x')) },
            { "a name of 300,000 characters",              named (std::string ((size_t) 300000, 'x')) },
            { "a name made of line breaks and tabs",       named ("&#10;&#13;&#9;Evil&#10;&#10;Name&#9;") },
            { "values that are not numbers",               [&] { auto t = body;
                                                                 for (size_t at = t.find (" v=\""); at != std::string::npos; at = t.find (" v=\"", at + 1))
                                                                 { const auto end = t.find ('"', at + 4); t.replace (at + 4, end - at - 4, "nan"); }
                                                                 return t; }() },
            { "9,000 values for parameters it has not",    [&] { std::string extra; for (int i = 0; i < 9000; ++i) extra += "<P id=\"nope" + std::to_string (i) + "\" v=\"1\"/>";
                                                                 return inject (extra); }() },
        };

        for (const auto& c : cases)
        {
            const auto file = workDir.getChildFile (juce::String ("hostile") + PresetManager::fileExtension);
            file.replaceWithData (c.text.data(), c.text.size());

            juce::String problem;
            const double start = now();
            loadEveryPresetIn (c.what, problem);

            // ... and through the IMPORT button, from a folder outside the library
            const auto outside = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("__dlay_selftest_import__");
            outside.createDirectory();
            const auto source = outside.getChildFile (juce::String ("__dlay_selftest_imported__") + PresetManager::fileExtension);
            source.replaceWithData (c.text.data(), c.text.size());
            other.copyFileTo (outside.getChildFile ("other.dtd"));
            const bool imported = proc.presets->importPresetFile (source).wasOk();

            for (const auto& e : proc.presets->getEntries())
                if (! e.isFactory && (e.name + e.category + e.description).contains (leak) && problem.isEmpty())
                    problem = "text of another file shows up after an import";

            if (problem.isEmpty())
                problem = healthy (proc);

            juce::MemoryBlock saved;
            proc.getStateInformation (saved);
            const double ms = now() - start;

            // what an import leaves in the library is small, and is DLAY's own writing
            const auto kept = presetDir.getChildFile (juce::String ("__dlay_selftest_imported__") + PresetManager::fileExtension);
            const auto keptText = kept.loadFileAsString();
            size_t longest = 0, mostPoints = 0;

            for (const auto& e : proc.presets->getEntries())
                if (! e.isFactory && (e.file.isAChildOf (workDir) || e.file == kept))
                    longest = std::max ({ longest, (size_t) e.name.length(), (size_t) e.category.length(), (size_t) e.description.length() / 8 });

            for (int l = 0; l < kNumLanes; ++l)
                mostPoints = std::max (mostPoints, proc.curves->getPoints (l).size());

            std::printf ("  preset with %-43s %7.0f ms, %s, session then %d kB\n", c.what, ms, imported ? "imported" : "refused  ", (int) (saved.getSize() / 1024));
            check (problem.isEmpty(), juce::String ("a preset file with ") + c.what + " does no harm (" + problem + ")");
            check (ms < 1500.0, juce::String ("a preset file with ") + c.what + " does not stall the host (" + juce::String (ms, 0) + " ms)");
            check (saved.getSize() < 64 * 1024, juce::String ("a preset file with ") + c.what + " does not swell the session ("
                                                 + juce::String ((int) (saved.getSize() / 1024)) + " kB)");
            check (longest <= 80 && mostPoints <= (size_t) MotionCurves::kMaxPoints,
                   juce::String ("... and no name or curve is longer than the plug-in makes them (") + juce::String ((int) longest) + " characters, "
                    + juce::String ((int) mostPoints) + " points)");
            check (! imported || (kept.getSize() < 64 * 1024 && ! keptText.containsIgnoreCase ("DOCTYPE") && ! keptText.contains ("ENTITY")
                                   && ! keptText.contains ("nope") && ! keptText.containsIgnoreCase ("\"nan\"")),
                   juce::String ("... and an import stores DLAY's own writing of it, not the file as it came (") + juce::String ((int) (kept.getSize() / 1024)) + " kB)");

            file.deleteFile();
            outside.deleteRecursively();
            presetDir.getChildFile (juce::String ("__dlay_selftest_imported__") + PresetManager::fileExtension).deleteFile();
        }
    }

    workDir.deleteRecursively();
    proc.presets->rescan();

    bool clean = true;
    for (const auto& e : proc.presets->getEntries())
        clean = clean && ! e.name.startsWith ("__dlay_selftest");
    check (clean && ! workDir.exists(), "the test leaves nothing behind in the preset folder");
}

// ============================================================================
void runPerformance()
{
    section ("CPU cost per algorithm (48 kHz, 512-sample blocks, 2x oversampling)");

    const double sr = 48000.0;
    const auto input = makeTestSignal (sr, 10.0, 10.0);
    double worst = 0.0;

    for (int mode = 0; mode < kNumModes; ++mode)
    {
        DlayEngine engine;
        engine.prepare (sr);

        auto p = cleanParams();
        p.mode = mode;
        p.timeMs = 300.0f;
        p.feedback = 0.6f;
        p.mix = 0.5f;
        p.saturation = 0.5f;
        p.diffusion = 0.5f;
        p.modDepth = 0.3f;
        p.highCutHz = 6000.0f;
        p.lowCutHz = 100.0f;
        p.ghost = 0.5f;
        p.collapse = 0.5f;
        p.orbit = 0.5f;
        p.pitchSt = (mode == (int) Mode::pitch || mode == (int) Mode::shimmer) ? 12.0f : 0.0f;

        const auto start = juce::Time::getHighResolutionTicks();
        const auto out = renderThroughEngine (engine, input, p, 512);
        const double seconds = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - start);
        const double cpu = 100.0 * seconds / 10.0;
        worst = std::max (worst, cpu);

        std::printf ("    %-12s %5.2f %% of one core  (everything on)\n", modeInfo (mode).name, cpu);
        juce::ignoreUnused (out);
    }

    check (worst < 40.0, "no algorithm needs more than 40 % of a core with every feature enabled");
}

// ============================================================================
int renderPresets (const juce::File& dir)
{
    DlayAudioProcessor proc;
    const double sr = 48000.0;
    const auto input = makeTestSignal (sr, 12.0, 4.0, 3);
    int written = 0;

    for (int i = 0; i < proc.presets->getNumFactoryPresets(); ++i)
    {
        proc.presets->load (i);
        proc.engine.reset();
        const auto out = renderThroughProcessor (proc, input, sr, 512);
        const auto& e = proc.presets->getEntries()[(size_t) i];
        const auto file = dir.getChildFile (juce::File::createLegalFileName (e.category))
                             .getChildFile (juce::File::createLegalFileName (e.name) + ".wav");

        if (writeWav (file, out, sr))
            ++written;
    }

    std::printf ("rendered %d presets to %s\n", written, dir.getFullPathName().toRawUTF8());
    return written > 0 ? 0 : 1;
}

int renderDemo (const juce::File& dir, juce::StringArray names)
{
    if (names.isEmpty())
        names = { "Golden Reel", "Ghost Machine", "Time Traveler", "VHS Dreams", "Neon Rain", "Reverse Universe",
                  "Spiral Galaxy", "Hyperdrive", "Quantum Echo", "Infinite Corridor", "Broken Clock", "Tape Collapse" };

    DlayAudioProcessor proc;
    const double sr = 48000.0;
    const auto input = makeDemoPhrase (sr, 12.0);

    writeWav (dir.getChildFile ("00 - dry phrase.wav"), input, sr);
    std::printf ("%-26s peak %.3f  rms %.4f\n", "dry phrase", (double) input.peak(), input.rms (0, input.size()));

    int number = 1, written = 0;

    for (const auto& name : names)
    {
        int index = -1;
        for (int i = 0; i < (int) proc.presets->getEntries().size(); ++i)
            if (proc.presets->getEntries()[(size_t) i].name.equalsIgnoreCase (name.trim()))
                index = i;

        if (index < 0)
        {
            std::printf ("%-26s not found\n", name.toRawUTF8());
            continue;
        }

        proc.presets->load (index);
        proc.engine.reset();
        const auto out = renderThroughProcessor (proc, input, sr, 512);

        double dc = 0.0;
        for (int i = 0; i < out.size(); ++i)
            dc += (double) out.l[(size_t) i] + (double) out.r[(size_t) i];
        dc /= 2.0 * (double) out.size();

        std::printf ("%-26s peak %.3f  rms %.4f  tail(10-12 s) %.4f  dc %+.5f  %s\n", name.trim().toRawUTF8(), (double) out.peak(),
                     out.rms (0, out.size()), out.rms ((int) (10.0 * sr), (int) (12.0 * sr)), dc, out.allFinite() ? "" : "NOT FINITE");

        const auto file = dir.getChildFile (juce::String (number++).paddedLeft ('0', 2) + " - " + juce::File::createLegalFileName (name.trim()) + ".wav");
        if (writeWav (file, out, sr))
            ++written;
    }

    std::printf ("wrote %d demo files to %s\n", written, dir.getFullPathName().toRawUTF8());
    return written > 0 ? 0 : 1;
}

} // namespace

// ============================================================================
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const juce::StringArray args (argv + 1, argc - 1);

    if (args.contains ("--render"))
    {
        const int i = args.indexOf ("--render");
        return renderPresets (juce::File::getCurrentWorkingDirectory().getChildFile (i + 1 < args.size() ? args[i + 1] : "renders"));
    }

    if (args.contains ("--demo"))
    {
        const int i = args.indexOf ("--demo");
        const int j = args.indexOf ("--presets");
        return renderDemo (juce::File::getCurrentWorkingDirectory().getChildFile (i + 1 < args.size() ? args[i + 1] : "demo"),
                           j >= 0 && j + 1 < args.size() ? juce::StringArray::fromTokens (args[j + 1], ",", "\"") : juce::StringArray());
    }

    if (args.contains ("--perf"))
    {
        runPerformance();
        return failures();
    }

    if (args.contains ("--foreign"))
    {
        // only the damaged-data test, with as many rounds as asked for (for a sanitizer build)
        const int i = args.indexOf ("--foreign");
        testForeignData (juce::jmax (1, i + 1 < args.size() ? args[i + 1].getIntValue() : 400));
        std::printf ("\n%d checks, %d failures\n", checks(), failures());
        return failures() == 0 ? 0 : 1;
    }

    std::printf ("DLAY %s test runner\n", DLAY_VERSION_STRING);

    // --quick skips the long renders (useful in Debug builds, where the point is the assertions)
    const bool quick = args.contains ("--quick");

    testHalfband();
    testInterpolation();
    testImpulseResponse();

    if (! quick)
    {
        testBlockSizeInvariance();
        testAllModes();
        testFeedbackAlwaysDecays();
        testFreezeAndInfinite();
    }

    testPitchAndSpectral();
    testPrimitives();
    testResets (quick);
    testNonFiniteValues();
    testReverseAfterReset();
    testPitchSweep();
    testProcessor();
    testOutputStageGlides();
    testPresets (args.contains ("--verbose"), quick);
    testForeignData (quick ? 80 : 400);

    if (! quick)
        runPerformance();

    testStateRestoreFromAnotherThread();

    std::printf ("\n%d checks, %d failures\n", checks(), failures());
    return failures() == 0 ? 0 : 1;
}
