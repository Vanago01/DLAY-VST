#pragma once

#include "DspCommon.h"
#include "../Params/Modes.h"

namespace dlay
{

/** Memoryless shapers. Every curve has unity slope at the origin, so the small-signal
    loop gain is never changed by the amount of saturation: FEEDBACK keeps meaning
    "how fast the echoes decay" no matter how hard the loop is driven. */
namespace shapers
{
    inline double tape (double u) noexcept
    {
        // tanh with a small bias: gentle even harmonics, like a slightly mis-biased machine.
        //
        // This is (tanh (u + bias) - tanh (bias)) / (1 - tanh^2 (bias)), written with the
        // addition theorem. In the direct form the two tanh values at u = 0 come from
        // different places (one folded by the compiler, one from the maths library) and
        // may differ in the last bit, which leaves a DC offset of 1e-17 on digital silence.
        // This form is exactly zero for zero input.
        static const double tb = std::tanh (0.18);
        const double t = std::tanh (u);
        return t / (1.0 + tb * t);
    }

    inline double tube (double u) noexcept
    {
        // asymmetric: generous headroom on one half-wave, early clipping on the other
        return u >= 0.0 ? 1.25 * std::tanh (u * 0.8)
                        : 0.80 * std::tanh (u * 1.25);
    }

    inline double diode (double u) noexcept
    {
        // cubic soft-clip that flattens completely at |u| = 1.5
        if (u >  1.5) return  1.0;
        if (u < -1.5) return -1.0;
        return u - (4.0 / 27.0) * u * u * u;
    }

    inline double fold (double u) noexcept
    {
        // sine wave-folder: soft clip up to pi/2, then folds back on itself
        return std::sin (clampT (u, -40.0, 40.0));
    }

    inline double shape (SatType type, double u) noexcept
    {
        switch (type)
        {
            case SatType::tube:   return tube (u);
            case SatType::diode:  return diode (u);
            case SatType::fold:   return fold (u);
            case SatType::tape:
            case SatType::crush:
            case SatType::numTypes:
            default:              return tape (u);
        }
    }
}

/** Parameters shared by both channels of a saturation stage. */
struct SatParams
{
    SatType type      = SatType::tape;
    double  threshold = 2.0;     // knee level: lower = more saturation
    double  invThresh = 0.5;
    double  crushLevels = 0.0;   // > 0 only for SatType::crush
    bool    active    = false;

    /** @param amount 0..1 */
    void set (SatType t, double amount) noexcept
    {
        type   = t;
        active = amount > 1.0e-4;

        threshold = 2.0 * std::pow (0.125, clampT (amount, 0.0, 1.0));     // 2.0 -> 0.25
        invThresh = 1.0 / threshold;

        // 12 bits down to 3 bits
        crushLevels = type == SatType::crush ? std::exp2 (11.0 - 9.0 * clampT (amount, 0.0, 1.0)) : 0.0;
    }

    /** Wet-side loudness compensation (never applied inside the feedback loop). */
    double makeup() const noexcept
    {
        return active ? std::pow (2.0 * invThresh, 0.2) : 1.0;
    }

    inline double apply (double x) const noexcept
    {
        if (type == SatType::crush)
        {
            const double q = std::trunc (x * crushLevels) / crushLevels;     // towards zero: no limit cycles
            return clampT (q, -1.0, 1.0);
        }

        return threshold * shapers::shape (type, x * invThresh);
    }
};

/** Transparent below 0 dBFS, then bends smoothly towards +6 dBFS and never beyond.
    This is the feedback loop's runaway protection: whatever FEEDBACK, RESONANCE or
    INFINITE do, the delay line can not hold more than this. */
inline double loopSoftLimit (double x) noexcept
{
    const double ax = std::abs (x);

    if (ax <= 1.0)
        return x;

    const double y = 1.0 + std::tanh (ax - 1.0);
    return x > 0.0 ? y : -y;
}

/** Last line of defence on the wet signal, independent of the output limiter switch:
    untouched below +6 dBFS, never above +12 dBFS. Only self-oscillating patches with the
    limiter switched off ever reach it. */
inline double wetSoftCeiling (double x) noexcept
{
    const double ax = std::abs (x);

    if (ax <= 2.0)
        return x;

    const double y = 2.0 + 2.0 * std::tanh ((ax - 2.0) * 0.5);
    return x > 0.0 ? y : -y;
}

} // namespace dlay
