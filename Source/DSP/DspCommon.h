#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace dlay
{

constexpr double kPi    = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

/** Clamp that also contains NaN: a value that is not above the lower bound - which
    includes "not a number" - comes back as the lower bound. */
template <typename T>
inline T clampT (T v, T lo, T hi) noexcept { return v > lo ? (v < hi ? v : hi) : lo; }

/** True for ordinary numbers; false for NaN, infinities and absurdly large values. */
inline bool isSane (double v) noexcept                  { return std::abs (v) < 1.0e15; }
inline double saneOr (double v, double fallback) noexcept { return isSane (v) ? v : fallback; }

/** Tempo the engine will actually use: the host's if it is plausible, otherwise 120. */
inline double usableBpm (double bpm) noexcept           { return (bpm >= 5.0 && bpm <= 999.0) ? bpm : 120.0; }

inline double lerpD (double a, double b, double t) noexcept { return a + (b - a) * t; }
inline double dbToGain (double db) noexcept             { return std::pow (10.0, db * 0.05); }
inline double gainToDb (double g) noexcept              { return 20.0 * std::log10 (std::max (g, 1.0e-12)); }
inline double semitonesToRatio (double st) noexcept     { return std::exp2 (st / 12.0); }
inline double fracD (double x) noexcept                 { return x - std::floor (x); }
inline double smoothStep (double t) noexcept            { t = clampT (t, 0.0, 1.0); return t * t * (3.0 - 2.0 * t); }

inline int nextPow2 (int v) noexcept
{
    int p = 1;
    while (p < v) p <<= 1;
    return p;
}

/** Small, fast xorshift generator (audio-thread safe, no locks, no allocation). */
struct Rng
{
    uint32_t s = 0x9E3779B9u;

    void seed (uint32_t v) noexcept        { s = v != 0 ? v : 0x9E3779B9u; }
    uint32_t nextU32() noexcept            { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    double next01() noexcept               { return (double) (nextU32() >> 8) * (1.0 / 16777216.0); }
    double nextBipolar() noexcept          { return next01() * 2.0 - 1.0; }
};

inline uint32_t hash32 (uint32_t x) noexcept
{
    x ^= x >> 16; x *= 0x7feb352dU;
    x ^= x >> 15; x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

/** Deterministic value in [0, 1) from two integers — used for repeatable "random" patterns. */
inline double hash01 (uint32_t a, uint32_t b) noexcept
{
    return (double) (hash32 (a * 0x9E3779B1u + hash32 (b + 0x85EBCA6Bu)) >> 8) * (1.0 / 16777216.0);
}

/** One-pole smoother (exponential). */
struct OnePole
{
    double z = 0.0, a = 1.0;

    void setTime (double seconds, double rate) noexcept
    {
        a = seconds <= 0.0 ? 1.0 : 1.0 - std::exp (-1.0 / (seconds * rate));
    }
    void reset (double v = 0.0) noexcept   { z = v; }
    double process (double x) noexcept     { z += a * (x - z); return z; }
};

/** Linear ramp between control ticks: zipper-free parameter interpolation.

    The ramp stops by itself when it arrives and lands on the target exactly, so one that
    is not given a new target on every tick simply holds its value instead of drifting.
    A target that is not a number is ignored. */
struct Ramp
{
    double v = 0.0, step = 0.0, target = 0.0;
    int    left = 0;

    void setTarget (double newTarget, int steps) noexcept
    {
        if (! isSane (newTarget))
            return;

        target = newTarget;
        left   = steps;
        step   = (newTarget - v) / (double) steps;
    }

    void snap (double newTarget) noexcept
    {
        if (isSane (newTarget))
            v = target = newTarget;

        step = 0.0;
        left = 0;
    }

    double next() noexcept
    {
        if (left > 0)
        {
            v += step;

            if (--left == 0)
                v = target;
        }

        return v;
    }
};

/** Equal-power pan gains for a position in [-1, 1] (unity at the extremes, -3 dB centre). */
inline void panGains (double pos, double& gl, double& gr) noexcept
{
    const double a = (clampT (pos, -1.0, 1.0) + 1.0) * (kPi * 0.25);
    gl = std::cos (a);
    gr = std::sin (a);
}

} // namespace dlay
