#pragma once

#include "../DSP/DspCommon.h"
#include "../Params/Modes.h"

namespace dlay
{

/** Per-lane settings, refreshed from the parameter tree once per block. */
struct LaneParams
{
    float depth    = 0.0f;     // -1 .. 1
    int   shape    = 0;        // LfoShape
    float rateHz   = 0.5f;
    bool  sync     = true;
    int   division = Div::bar1;
    float phase    = 0.0f;     // 0 .. 1
    float stereo   = 0.0f;     // 0 .. 0.5 (right-channel phase offset, in cycles)
};

/** DLAY MOTION: six independent modulation lanes (speed, pan, feedback, filter, pitch, mix).

    Each lane is an LFO or a user-drawn curve with its own depth, rate, phase, tempo-sync
    and left/right phase offset. Custom curves live in a 256-point table of atomics so the
    editor can redraw them while audio is running without locks or allocation.
*/
class MotionEngine
{
public:
    static constexpr int kTableSize = 256;

    MotionEngine()
    {
        for (int l = 0; l < kNumLanes; ++l)
        {
            for (int i = 0; i < kTableSize; ++i)
                tables[(size_t) l][(size_t) i].store (0.0f, std::memory_order_relaxed);

            uiPhase[(size_t) l].store (0.0f);
            uiValue[(size_t) l][0].store (0.0f);
            uiValue[(size_t) l][1].store (0.0f);
        }
    }

    void prepare (double sampleRate, int tickSamples) noexcept
    {
        sr = sampleRate;
        smooth = 1.0 - std::exp (-(double) tickSamples / (0.004 * sampleRate));
        reset();
    }

    void reset() noexcept
    {
        for (int l = 0; l < kNumLanes; ++l)
        {
            phase[(size_t) l] = 0.0;
            cycle[(size_t) l] = 0;
            out[(size_t) l][0] = out[(size_t) l][1] = 0.0;
        }
    }

    // ---- custom curve tables (written by the message thread) ----------------
    void setTableValue (int lane, int index, float bipolarValue) noexcept
    {
        // the table is what the audio thread reads: nothing but -1 .. 1 may get in
        const float v = bipolarValue >= -1.0f ? (bipolarValue <= 1.0f ? bipolarValue : 1.0f)
                                              : (bipolarValue < -1.0f ? -1.0f : 0.0f);

        tables[(size_t) lane][(size_t) index].store (v, std::memory_order_relaxed);
    }

    float tableValue (int lane, int index) const noexcept
    {
        return tables[(size_t) lane][(size_t) index].load (std::memory_order_relaxed);
    }

    /** Evaluates a lane shape. Shared by the audio thread and the editor's curve display. */
    double evaluate (int lane, int shape, double totalPhase) const noexcept
    {
        const double cyc = std::floor (totalPhase);
        const double ph  = totalPhase - cyc;
        const auto   ci  = (uint32_t) (int64_t) cyc;

        switch ((LfoShape) shape)
        {
            case LfoShape::sine:        return std::sin (kTwoPi * ph);
            case LfoShape::triangle:    return 1.0 - 4.0 * std::abs (fracD (ph + 0.25) - 0.5);
            case LfoShape::square:      return ph < 0.5 ? 1.0 : -1.0;
            case LfoShape::saw:         return 2.0 * ph - 1.0;
            case LfoShape::sampleHold:  return hash01 (ci, 0x100u + (uint32_t) lane) * 2.0 - 1.0;

            case LfoShape::random:
            {
                const double a = hash01 (ci,      0x200u + (uint32_t) lane) * 2.0 - 1.0;
                const double b = hash01 (ci + 1u, 0x200u + (uint32_t) lane) * 2.0 - 1.0;
                return lerpD (a, b, smoothStep (ph));
            }

            case LfoShape::custom:
            {
                const double pos = ph * (double) (kTableSize - 1);
                const int    i   = (int) pos;
                const double f   = pos - (double) i;
                const double a   = (double) tableValue (lane, i);
                const double b   = (double) tableValue (lane, std::min (i + 1, kTableSize - 1));
                return a + (b - a) * f;
            }

            case LfoShape::numShapes:
            default:                    return 0.0;
        }
    }

    /** Advances all lanes by numSamples and updates the smoothed, depth-scaled outputs. */
    void advance (int numSamples, const std::array<LaneParams, (size_t) kNumLanes>& lp,
                  double bpm, bool playing, double ppq, bool ppqValid) noexcept
    {
        // A song position or tempo that is not a number must not reach the phase
        // accumulators: they would never recover from it.
        if (! (std::abs (ppq) < 1.0e12))
            ppqValid = false;

        bpm = usableBpm (bpm);

        for (int l = 0; l < kNumLanes; ++l)
        {
            const auto& p = lp[(size_t) l];
            const double lengthBeats = kDivisions[(size_t) clampT (p.division, 0, kNumDivisions - 1)].beats;
            const double phaseOffset = saneOr ((double) p.phase, 0.0);
            const double stereoShift = saneOr ((double) p.stereo, 0.0);

            if (p.sync && playing && ppqValid)
            {
                // locked to song position: the same bar always gets the same motion
                const double total = ppq / lengthBeats;
                const double cyc   = std::floor (total);
                cycle[(size_t) l]  = (int64_t) cyc;
                phase[(size_t) l]  = total - cyc;
            }
            else
            {
                double freq = p.sync ? bpm / (60.0 * lengthBeats) : (double) p.rateHz;

                if (! (freq >= 0.0 && freq <= 1000.0))
                    freq = 0.0;

                phase[(size_t) l] += (double) numSamples * freq / sr;

                while (phase[(size_t) l] >= 1.0)
                {
                    phase[(size_t) l] -= 1.0;
                    ++cycle[(size_t) l];
                }
            }

            const double base = (double) cycle[(size_t) l] + phase[(size_t) l] + phaseOffset;

            for (int ch = 0; ch < 2; ++ch)
            {
                double v = 0.0;

                if (p.depth != 0.0f)
                    v = evaluate (l, p.shape, base + (ch == 1 ? stereoShift : 0.0)) * (double) p.depth;

                if (! (std::abs (v) <= 1.0))            // also catches NaN: the output stays in -1 .. 1
                    v = v > 1.0 ? 1.0 : (v < -1.0 ? -1.0 : 0.0);

                out[(size_t) l][(size_t) ch] += smooth * (v - out[(size_t) l][(size_t) ch]);
                uiValue[(size_t) l][(size_t) ch].store ((float) out[(size_t) l][(size_t) ch], std::memory_order_relaxed);
            }

            uiPhase[(size_t) l].store ((float) fracD (phase[(size_t) l] + phaseOffset), std::memory_order_relaxed);
        }
    }

    /** Smoothed lane output in [-1, 1], already scaled by depth. */
    double value (Lane lane, int channel) const noexcept
    {
        return out[(size_t) lane][(size_t) channel];
    }

    // read by the editor
    std::array<std::atomic<float>, (size_t) kNumLanes> uiPhase;
    std::array<std::array<std::atomic<float>, 2>, (size_t) kNumLanes> uiValue;

private:
    std::array<std::array<std::atomic<float>, (size_t) kTableSize>, (size_t) kNumLanes> tables;
    std::array<double, (size_t) kNumLanes> phase {};
    std::array<int64_t, (size_t) kNumLanes> cycle {};
    std::array<std::array<double, 2>, (size_t) kNumLanes> out {};
    double sr = 48000.0, smooth = 1.0;
};

} // namespace dlay
