#pragma once

#include "DelayLine.h"

namespace dlay
{

/** Two-head crossfading delay-line pitch shifter with splice alignment (one per channel).

    Each head sweeps its delay like a tape head on a rotating drum; the two heads are half
    a window apart and Hann-crossfaded, so their gains always sum to one.

    A plain two-head shifter splices wherever the window happens to fall, which detunes
    and roughens sustained notes (the spectrum collapses onto multiples of the grain rate).
    Here every new grain first searches for the read position whose waveform lines up best
    with the grain it is about to replace (normalised cross-correlation over one pitch
    period), so tonal material comes out as a clean, in-tune transposition.

    Every head keeps its own position and travels at the current ratio, sample by sample.
    When the ratio moves (the PITCH lane of MOTION, automation) a head's path bends; it
    never jumps, so a pitch sweep is a glide and not a staircase. A new grain starts far
    enough back to stay clear of the write position for its whole window, allowing for how
    fast the ratio is moving at that moment.

    Reads use the band-limited sinc kernels with a cut-off matched to the transposition,
    so shifting up does not alias. Lives inside the feedback loop: every repeat is shifted
    again.
*/
class PitchShifter
{
public:
    static constexpr double kMinDelay = 9.0;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        line.prepare ((int) (sampleRate * 1.4) + 64);

        searchRange  = (int) (sampleRate / 55.0);                    // one period of the lowest tracked pitch
        matchLength  = (int) (sampleRate / 75.0);
        coarseStep   = std::max (1, (int) std::lround (sampleRate / 12000.0));

        maxHeadroom  = line.maxDelay() - kMinDelay - (double) (searchRange + matchLength) - 16.0;
        maxHead      = line.maxDelay() - (double) matchLength - 8.0;

        setWindow (0.07);
        reset();
    }

    void reset() noexcept
    {
        line.clear();
        phase = 0.0;
        ratio = 1.0;
        ratioSlope = 0.0;
        span = 0.0;
        cutoff = 0;
        offset[0] = offset[1] = 0.0;
        offsetMean = 0.0;
        head[0] = head[1] = kMinDelay;
        parked = true;
        fresh  = true;
    }

    /** Housekeeping for the line's deferred erase (see DelayLine). Call at control rate. */
    void sweep (int maxBlocks) noexcept     { line.sweep (maxBlocks); }

    /** Window (grain) length in seconds: short = tighter but grainier, long = smoother. */
    void setWindow (double seconds) noexcept
    {
        window   = clampT (seconds, 0.012, 0.25) * sr;
        phaseInc = 1.0 / window;
        updateSpan();
    }

    /** Call at control rate. samplesSinceLast is the control interval; it is used to
        estimate how fast the ratio is moving. */
    void setRatio (double r, int samplesSinceLast) noexcept
    {
        const double previous = ratio;

        ratio  = clampT (r, 0.24, 4.1);
        cutoff = SincTable::cutoffForRate (ratio);
        updateSpan();

        if (fresh)
        {
            fresh = false;                              // the first value after a reset is not a movement
            ratioSlope = 0.0;
            return;
        }

        // a parameter jump is not a sweep either: one tick may not suggest more than a
        // change of 1.0 per window
        const double limit = 1.0 / window;
        const double now   = clampT ((ratio - previous) / (double) std::max (1, samplesSinceLast), -limit, limit);

        ratioSlope += 0.25 * (now - ratioSlope);

        if (std::abs (ratioSlope) < 1.0e-12)
            ratioSlope = 0.0;
    }

    /** Mean delay added to the loop (compensated by the main tap). */
    double latency() const noexcept { return kMinDelay + span * 0.5 + offsetMean; }

    inline double process (double x) noexcept
    {
        if (parked)
            unpark();

        const double drift = 1.0 - ratio;               // change of a head's delay per sample
        double out = 0.0;

        for (int h = 0; h < 2; ++h)
        {
            double p = phase + 0.5 * (double) h;
            if (p >= 1.0) p -= 1.0;

            const double s = std::sin (kPi * p);
            out += s * s * (double) line.readSinc (head[h], cutoff);

            head[h] = clampT (head[h] + drift, kMinDelay, maxHead);
        }

        line.write ((float) x);

        const double previous = phase;
        phase += phaseInc;

        if (phase >= 1.0)
        {
            phase -= 1.0;
            startGrain (0);                             // head 0 is silent now: move it
        }
        else if (previous < 0.5 && phase >= 0.5)
        {
            startGrain (1);
        }

        return out;
    }

    /** Write-only: keeps the buffer current while the shifter is idle. */
    inline void feed (double x) noexcept
    {
        line.write ((float) x);

        phase += phaseInc;
        if (phase >= 1.0) phase -= 1.0;

        parked = true;
    }

private:
    void updateSpan() noexcept
    {
        span = std::min (window * std::abs (1.0 - ratio), maxHeadroom);
    }

    /** Coming back from idle: put both heads where a steady shift would have them. */
    void unpark() noexcept
    {
        const bool up = ratio > 1.0;

        for (int h = 0; h < 2; ++h)
        {
            double p = phase + 0.5 * (double) h;
            if (p >= 1.0) p -= 1.0;

            offset[h] = 0.0;
            head[h]   = kMinDelay + span * (up ? 1.0 - p : p);
        }

        offsetMean = 0.0;
        parked = false;
    }

    /** How far above the minimum delay a new grain must start so that its head stays behind
        the write position for the whole window.

        With the ratio at r and moving by r' per sample, the head's delay after t samples is
        start + (1 - r) t - r' t^2 / 2. The lowest point of that curve inside the window is
        the head-room needed. For a steady shift up this is the classic window * (r - 1);
        for a steady shift down it is zero. */
    double headroom() const noexcept
    {
        const double a = 1.0 - ratio;
        const double b = ratioSlope;

        double lowest = std::min (0.0, a * window - 0.5 * b * window * window);

        if (a < 0.0 && b < 0.0)                         // shifting up but slowing: the dip is inside the window
        {
            const double t = a / b;
            if (t < window)
                lowest = std::min (lowest, 0.5 * a * a / b);
        }

        return clampT (-lowest, 0.0, maxHeadroom);
    }

    /** Starts a new grain on a head, at the position where it is in phase with the other one. */
    void startGrain (int h) noexcept
    {
        const int other = 1 - h;

        // the other head is half-way through its grain; this one starts at the far end of its path
        const double base      = kMinDelay + headroom();
        const int    refDelay  = (int) head[other] + 1;
        const int    baseDelay = (int) base + 1;

        double refEnergy = 0.0;
        for (int j = 0; j < matchLength; j += coarseStep)
        {
            const double a = (double) line.readInt (refDelay + j);
            refEnergy += a * a;
        }

        double bestLag = 0.0;

        if (refEnergy > 1.0e-9)
        {
            const auto score = [this, refDelay, baseDelay] (int lag, int step) noexcept
            {
                double cross = 0.0, energy = 0.0;

                for (int j = 0; j < matchLength; j += step)
                {
                    const double a = (double) line.readInt (refDelay + j);
                    const double b = (double) line.readInt (baseDelay + lag + j);
                    cross  += a * b;
                    energy += b * b;
                }

                // slight preference for small lags keeps the added delay low
                return cross / std::sqrt (energy + 1.0e-12) * (1.0 - 0.03 * (double) lag / (double) searchRange);
            };

            int best = 0;
            double bestScore = -1.0e30;

            for (int lag = 0; lag <= searchRange; lag += coarseStep)
            {
                const double sc = score (lag, coarseStep);
                if (sc > bestScore) { bestScore = sc; best = lag; }
            }

            if (coarseStep > 1)
            {
                int fine = best;
                bestScore = -1.0e30;

                for (int lag = std::max (0, best - coarseStep); lag <= std::min (searchRange, best + coarseStep); ++lag)
                {
                    const double sc = score (lag, 1);
                    if (sc > bestScore) { bestScore = sc; fine = lag; }
                }

                best = fine;
            }

            bestLag = (double) best;
        }

        offset[h]   = bestLag;
        head[h]     = base + bestLag;
        offsetMean += 0.15 * (0.5 * (offset[0] + offset[1]) - offsetMean);
    }

    DelayLine line;
    double sr = 48000.0, window = 3360.0, phaseInc = 1.0 / 3360.0;
    double phase = 0.0, ratio = 1.0, ratioSlope = 0.0, span = 0.0;
    double head[2] { kMinDelay, kMinDelay };            // each head's delay behind the write position
    double offset[2] { 0.0, 0.0 }, offsetMean = 0.0;
    double maxHeadroom = 1000.0, maxHead = 1000.0;
    int cutoff = 0, searchRange = 872, matchLength = 640, coarseStep = 4;
    bool parked = true, fresh = true;
};

} // namespace dlay
