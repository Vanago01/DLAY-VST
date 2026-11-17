#pragma once

#include "DspCommon.h"

namespace dlay
{

/** Chain of Schroeder all-pass stages with glide-able lengths.

    With g = 0 every stage is a pure delay, so the chain can stay in the signal path
    permanently and the main delay tap simply compensates for totalDelay(). That makes the
    DIFFUSION control completely click-free: there is no bypass switch to toggle.
    Lengths snap to whole samples once they stop moving, so a static setting has a perfectly
    flat response (no interpolation loss accumulating around the feedback loop).

    reset() costs nothing: each stage only counts how many samples it has stored since, and
    anything older than that reads as silence. No memory is zero-filled on the audio thread.
*/
template <int NumStages>
class Diffuser
{
public:
    void prepare (double sampleRate, double maxStageSeconds)
    {
        sr = sampleRate;
        const int size = nextPow2 ((int) (maxStageSeconds * sampleRate) + 8);

        for (auto& s : stages)
        {
            s.buffer.assign ((size_t) size, 0.0f);
            s.mask = size - 1;
            s.w = 0;
            s.stored = 0;
            s.len = s.target = 1.0;
        }

        glide = 1.0 - std::exp (-1.0 / (0.06 * sampleRate));
    }

    void reset() noexcept
    {
        for (auto& s : stages)
        {
            s.stored = 0;
            s.len = s.target;
        }
    }

    /** Target stage length in samples. integerSnap = false keeps it fractional (modulated use). */
    void setLength (int stage, double samples, bool integerSnap = true) noexcept
    {
        auto& s = stages[(size_t) stage];
        const double lim = (double) (s.mask - 2);
        s.target = clampT (integerSnap ? std::round (samples) : samples, 1.0, lim);
    }

    void snapLengths() noexcept
    {
        for (auto& s : stages)
            s.len = s.target;
    }

    double totalDelay() const noexcept
    {
        double sum = 0.0;
        for (const auto& s : stages)
            sum += s.len;
        return sum;
    }

    inline double process (double x, double g) noexcept
    {
        for (auto& s : stages)
        {
            const double diff = s.target - s.len;

            if (diff != 0.0)
            {
                s.len += glide * diff;
                if (std::abs (s.target - s.len) < 1.0e-3)
                    s.len = s.target;
            }

            // a sample i writes back exists only if at least i have been stored since reset()
            const int    i = (int) s.len;
            const double f = s.len - (double) i;
            const double a = i <= s.stored ? (double) s.buffer[(size_t) ((s.w - i) & s.mask)] : 0.0;
            double d = a;

            if (f > 0.0)
            {
                const double b = i < s.stored ? (double) s.buffer[(size_t) ((s.w - i - 1) & s.mask)] : 0.0;
                d = a + (b - a) * f;
            }

            const double v = x - g * d;
            x = g * v + d;

            s.buffer[(size_t) s.w] = (float) v;
            s.w = (s.w + 1) & s.mask;
            s.stored += (int) (s.stored <= s.mask);         // saturates at the buffer size
        }

        return x;
    }

private:
    struct Stage
    {
        std::vector<float> buffer;
        int mask = 0, w = 0, stored = 0;
        double len = 1.0, target = 1.0;
    };

    std::array<Stage, (size_t) NumStages> stages;
    double sr = 48000.0, glide = 1.0;
};

} // namespace dlay
