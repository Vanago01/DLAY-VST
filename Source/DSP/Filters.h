#pragma once

#include "DspCommon.h"

namespace dlay
{

/** Coefficients for a TPT state-variable filter (Zavalishin / Cytomic form).
    Stable under audio-rate modulation, which matters because MOTION sweeps it. */
struct SvfCoeffs
{
    double a1 = 1.0, a2 = 0.0, a3 = 0.0, k = 1.414;

    void set (double freq, double q, double sampleRate) noexcept
    {
        const double f = clampT (freq, 5.0, sampleRate * 0.49);
        const double g = std::tan (kPi * f / sampleRate);
        k  = 1.0 / std::max (q, 0.05);
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
};

struct SvfState
{
    double ic1 = 0.0, ic2 = 0.0;

    void reset() noexcept { ic1 = ic2 = 0.0; }

    inline void tick (double x, const SvfCoeffs& c, double& lp, double& bp, double& hp) noexcept
    {
        const double v3 = x - ic2;
        const double v1 = c.a1 * ic1 + c.a2 * v3;
        const double v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
        ic1 = 2.0 * v1 - ic1;
        ic2 = 2.0 * v2 - ic2;
        lp = v2;
        bp = v1;
        hp = x - c.k * v1 - v2;
    }

    inline double lowpass (double x, const SvfCoeffs& c) noexcept   { double l, b, h; tick (x, c, l, b, h); return l; }
    inline double highpass (double x, const SvfCoeffs& c) noexcept  { double l, b, h; tick (x, c, l, b, h); return h; }
    inline double bandpass (double x, const SvfCoeffs& c) noexcept  { double l, b, h; tick (x, c, l, b, h); return b; }
};

/** Largest gain of a resonant 2-pole high-pass followed by a resonant 2-pole low-pass
    (same Q). Corner frequencies are given pre-warped, as tan (pi * f / fs), which makes the
    result exact for the TPT filters above. Used to keep the feedback filters' peak at unity:
    a resonant filter inside a loop would otherwise multiply the loop gain by its peak. */
inline double combinedFilterPeak (bool hpOn, double wHp, bool lpOn, double wLp, double q) noexcept
{
    const auto response = [=] (double w) noexcept
    {
        double mag = 1.0;

        if (lpOn)
        {
            const double r = w / wLp;
            mag /= std::sqrt ((1.0 - r * r) * (1.0 - r * r) + (r / q) * (r / q));
        }

        if (hpOn)
        {
            const double r = w / wHp;
            mag *= r * r / std::sqrt ((1.0 - r * r) * (1.0 - r * r) + (r / q) * (r / q));
        }

        return mag;
    };

    // candidates: each filter's own peak and corner, and the middle of the band between them
    const double offset = q > 0.7072 ? std::sqrt (1.0 - 1.0 / (2.0 * q * q)) : 1.0;
    double best = 1.0;

    if (lpOn)           best = std::max ({ best, response (wLp * offset), response (wLp) });
    if (hpOn)           best = std::max ({ best, response (wHp / offset), response (wHp) });
    if (lpOn && hpOn)   best = std::max (best, response (std::sqrt (wLp * wHp)));

    return best;
}

/** First-order low-pass / high-pass pair sharing one state. */
struct OnePoleFilter
{
    double z = 0.0, a = 1.0;

    void setCutoff (double freq, double sampleRate) noexcept
    {
        a = 1.0 - std::exp (-kTwoPi * clampT (freq, 1.0, sampleRate * 0.49) / sampleRate);
    }
    void reset() noexcept                         { z = 0.0; }
    inline double lowpass (double x) noexcept     { z += a * (x - z); return z; }
    inline double highpass (double x) noexcept    { z += a * (x - z); return x - z; }
};

/** RBJ biquad (transposed direct form II, double precision). */
struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

    void setPeak (double freq, double q, double gainDb, double sampleRate) noexcept
    {
        const double A     = std::pow (10.0, gainDb / 40.0);
        const double w0    = kTwoPi * clampT (freq, 10.0, sampleRate * 0.45) / sampleRate;
        const double alpha = std::sin (w0) / (2.0 * q);
        const double cs    = std::cos (w0);
        const double a0    = 1.0 + alpha / A;
        b0 = (1.0 + alpha * A) / a0;
        b1 = (-2.0 * cs) / a0;
        b2 = (1.0 - alpha * A) / a0;
        a1 = (-2.0 * cs) / a0;
        a2 = (1.0 - alpha / A) / a0;
    }

    void setHighShelf (double freq, double gainDb, double sampleRate) noexcept
    {
        const double A     = std::pow (10.0, gainDb / 40.0);
        const double w0    = kTwoPi * clampT (freq, 10.0, sampleRate * 0.45) / sampleRate;
        const double cs    = std::cos (w0);
        const double alpha = std::sin (w0) * 0.5 * std::sqrt (2.0);
        const double sq    = 2.0 * std::sqrt (A) * alpha;
        const double a0    = (A + 1.0) - (A - 1.0) * cs + sq;
        b0 = A * ((A + 1.0) + (A - 1.0) * cs + sq) / a0;
        b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cs) / a0;
        b2 = A * ((A + 1.0) + (A - 1.0) * cs - sq) / a0;
        a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cs) / a0;
        a2 = ((A + 1.0) - (A - 1.0) * cs - sq) / a0;
    }

    void setLowShelf (double freq, double gainDb, double sampleRate) noexcept
    {
        const double A     = std::pow (10.0, gainDb / 40.0);
        const double w0    = kTwoPi * clampT (freq, 10.0, sampleRate * 0.45) / sampleRate;
        const double cs    = std::cos (w0);
        const double alpha = std::sin (w0) * 0.5 * std::sqrt (2.0);
        const double sq    = 2.0 * std::sqrt (A) * alpha;
        const double a0    = (A + 1.0) + (A - 1.0) * cs + sq;
        b0 = A * ((A + 1.0) - (A - 1.0) * cs + sq) / a0;
        b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cs) / a0;
        b2 = A * ((A + 1.0) - (A - 1.0) * cs - sq) / a0;
        a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cs) / a0;
        a2 = ((A + 1.0) + (A - 1.0) * cs - sq) / a0;
    }
};

struct BiquadState
{
    double s1 = 0.0, s2 = 0.0;

    void reset() noexcept { s1 = s2 = 0.0; }

    inline double process (double x, const Biquad& c) noexcept
    {
        const double y = c.b0 * x + s1;
        s1 = c.b1 * x - c.a1 * y + s2;
        s2 = c.b2 * x - c.a2 * y;
        return y;
    }
};

} // namespace dlay
