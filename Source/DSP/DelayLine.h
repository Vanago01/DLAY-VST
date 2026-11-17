#pragma once

#include "DspCommon.h"

namespace dlay
{

/** Polyphase windowed-sinc kernels for band-limited fractional delay reads.

    16 taps, 128 fractional phases (linearly interpolated), and five cut-off settings so a
    read head that is travelling faster than the write head (delay time shrinking, pitch-up
    grains, the pitch shifter) can be low-passed below the *new* Nyquist. That is what keeps
    time-warps and pitch shifts free of aliasing.
*/
class SincTable
{
public:
    static constexpr int kTaps    = 16;
    static constexpr int kHalf    = 8;
    static constexpr int kPhases  = 128;
    static constexpr int kCutoffs = 5;

    static const SincTable& get()
    {
        static const SincTable instance;
        return instance;
    }

    /** Row layout: 16 taps followed by 16 deltas towards the next phase row. */
    const float* row (int cutoff, int phase) const noexcept
    {
        return table.data() + (size_t) ((cutoff * (kPhases + 1) + phase) * kTaps * 2);
    }

    /** Picks the kernel whose cut-off is at or below 1/rate (rate = read speed / write speed). */
    static int cutoffForRate (double rate) noexcept
    {
        if (rate <= 1.04) return 0;
        if (rate <= 1.25) return 1;
        if (rate <= 1.62) return 2;
        if (rate <= 2.25) return 3;
        return 4;
    }

private:
    SincTable()
    {
        static constexpr double cutoffs[kCutoffs] = { 1.0, 0.8, 0.62, 0.45, 0.30 };
        constexpr double beta = 6.5;

        table.assign ((size_t) (kCutoffs * (kPhases + 1) * kTaps * 2), 0.0f);
        std::vector<double> rows ((size_t) ((kPhases + 1) * kTaps));

        const double i0Beta = besselI0 (beta);

        for (int c = 0; c < kCutoffs; ++c)
        {
            const double fc = cutoffs[c];

            for (int p = 0; p <= kPhases; ++p)
            {
                const double mu = (double) p / (double) kPhases;
                double sum = 0.0;

                for (int j = 0; j < kTaps; ++j)
                {
                    const double t = (double) (j - (kHalf - 1)) - mu;        // taps at -7 .. +8
                    const double x = t / (double) kHalf;
                    double w = 0.0;

                    if (std::abs (x) < 1.0)
                        w = besselI0 (beta * std::sqrt (1.0 - x * x)) / i0Beta;

                    const double arg = kPi * fc * t;
                    const double s   = std::abs (arg) < 1.0e-9 ? 1.0 : std::sin (arg) / arg;
                    const double v   = fc * s * w;

                    rows[(size_t) (p * kTaps + j)] = v;
                    sum += v;
                }

                for (int j = 0; j < kTaps; ++j)                              // unity DC gain per phase
                    rows[(size_t) (p * kTaps + j)] /= sum;
            }

            for (int p = 0; p <= kPhases; ++p)
            {
                float* dst = table.data() + (size_t) ((c * (kPhases + 1) + p) * kTaps * 2);

                for (int j = 0; j < kTaps; ++j)
                {
                    const double a = rows[(size_t) (p * kTaps + j)];
                    const double b = p < kPhases ? rows[(size_t) ((p + 1) * kTaps + j)] : a;
                    dst[j]         = (float) a;
                    dst[kTaps + j] = (float) (b - a);
                }
            }
        }
    }

    static double besselI0 (double x) noexcept
    {
        double sum = 1.0, term = 1.0;
        const double q = x * x * 0.25;

        for (int k = 1; k < 40; ++k)
        {
            term *= q / ((double) k * (double) k);
            sum += term;
            if (term < 1.0e-12 * sum) break;
        }
        return sum;
    }

    std::vector<float> table;
};

/** Power-of-two circular buffer with several read qualities.

    Convention: read first, then write. A delay of D samples returns the sample written
    D writes ago, so D >= 1 for integer reads, >= 3 for Hermite and >= 8 for sinc.

    Erasing is real-time safe at any size. clear() does not touch the audio memory (tens of
    megabytes for a ten-second stereo tape at a high sample rate - far too much for one
    audio callback). It flags every block as stale instead; a stale block is zero-filled
    the first time a read or a write touches it, and sweep() works through the rest a few
    blocks at a time. What comes back from a read is always exactly what a zero-filled line
    would have returned.
*/
class DelayLine
{
public:
    static constexpr double kMinSincDelay    = 8.0;
    static constexpr double kMinHermiteDelay = 3.0;
    static constexpr int    kBlockSamples    = 1024;        // 4 kB: the most a single touch can cost

    void prepare (int minimumSize)
    {
        const int size = nextPow2 (std::max (64, minimumSize));
        buffer.assign ((size_t) size, 0.0f);
        mask  = size - 1;
        wpos  = 0;
        sinc  = &SincTable::get();

        blockShift = 0;
        while ((1 << blockShift) < std::min (size, kBlockSamples))
            ++blockShift;

        stale.assign ((size_t) (size >> blockShift), (uint8_t) 0);
        staleCount = 0;
        sweepBlock = 0;
        written    = false;
    }

    /** Erases the line. Constant, small cost: see the class comment. */
    void clear() noexcept
    {
        wpos = 0;

        if (! written)
            return;                                 // nothing has been recorded since the last erase

        std::fill (stale.begin(), stale.end(), (uint8_t) 1);
        staleCount = (int) stale.size();
        sweepBlock = 0;
        written    = false;
    }

    /** Zero-fills up to maxBlocks stale blocks. Call at control rate; does nothing once clean. */
    void sweep (int maxBlocks) noexcept
    {
        const int numBlocks = (int) stale.size();

        for (int scanned = 0; staleCount > 0 && maxBlocks > 0 && scanned < numBlocks; ++scanned)
        {
            if (stale[(size_t) sweepBlock] != 0)
            {
                wipe (sweepBlock);
                --maxBlocks;
            }

            if (++sweepBlock >= numBlocks)
                sweepBlock = 0;
        }
    }

    bool isClean() const noexcept          { return staleCount == 0; }
    int capacity() const noexcept          { return mask + 1; }
    double maxDelay() const noexcept       { return (double) (mask - SincTable::kTaps - 4); }

    inline void write (float x) noexcept
    {
        if (staleCount != 0) [[unlikely]]
        {
            const int block = wpos >> blockShift;
            if (stale[(size_t) block] != 0)
                wipe (block);
        }

        written = true;
        buffer[(size_t) wpos] = x;
        wpos = (wpos + 1) & mask;
    }

    inline float readInt (int delay) const noexcept
    {
        const int pos = (wpos - delay) & mask;

        if (staleCount != 0 && stale[(size_t) (pos >> blockShift)] != 0) [[unlikely]]
            return 0.0f;

        return buffer[(size_t) pos];
    }

    inline float readLinear (double delay) noexcept
    {
        const int    i = (int) delay;

        if (staleCount != 0) [[unlikely]]
            freshen ((wpos - i - 1) & mask, 2);

        const float  f = (float) (delay - (double) i);
        const float  a = buffer[(size_t) ((wpos - i) & mask)];
        const float  b = buffer[(size_t) ((wpos - i - 1) & mask)];
        return a + (b - a) * f;
    }

    /** 4-point, 3rd-order Hermite (Catmull-Rom). */
    inline float readHermite (double delay) noexcept
    {
        const int   i  = (int) delay;
        const float mu = 1.0f - (float) (delay - (double) i);
        const int   b  = wpos - i - 1;

        if (staleCount != 0) [[unlikely]]
            freshen ((b - 1) & mask, 4);

        const float ym1 = buffer[(size_t) ((b - 1) & mask)];
        const float y0  = buffer[(size_t) (b & mask)];
        const float y1  = buffer[(size_t) ((b + 1) & mask)];
        const float y2  = buffer[(size_t) ((b + 2) & mask)];

        const float c1 = 0.5f * (y1 - ym1);
        const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
        const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        return ((c3 * mu + c2) * mu + c1) * mu + y0;
    }

    /** Band-limited 16-point windowed-sinc read. cutoffIndex comes from SincTable::cutoffForRate(). */
    inline float readSinc (double delay, int cutoffIndex = 0) noexcept
    {
        const int    i  = (int) delay;
        const double mu = 1.0 - (delay - (double) i);                       // (0, 1]
        const double ph = mu * (double) SincTable::kPhases;
        int   pi = (int) ph;
        float pf = (float) (ph - (double) pi);

        if (pi >= SincTable::kPhases) { pi = SincTable::kPhases; pf = 0.0f; }

        const float* r   = sinc->row (cutoffIndex, pi);
        const int    idx = (wpos - i - 1 - (SincTable::kHalf - 1)) & mask;
        float acc = 0.0f;

        if (staleCount != 0) [[unlikely]]
            freshen (idx, SincTable::kTaps);

        if (idx + SincTable::kTaps <= mask + 1)
        {
            const float* src = buffer.data() + idx;
            for (int j = 0; j < SincTable::kTaps; ++j)
                acc += src[j] * (r[j] + pf * r[SincTable::kTaps + j]);
        }
        else
        {
            for (int j = 0; j < SincTable::kTaps; ++j)
                acc += buffer[(size_t) ((idx + j) & mask)] * (r[j] + pf * r[SincTable::kTaps + j]);
        }
        return acc;
    }

private:
    void wipe (int block) noexcept
    {
        std::memset (buffer.data() + ((size_t) block << blockShift), 0, sizeof (float) << blockShift);
        stale[(size_t) block] = 0;
        --staleCount;
    }

    /** Makes the samples [first, first + count) readable. count never exceeds a block, so
        the span touches at most two of them. */
    void freshen (int first, int count) noexcept
    {
        const int a = first >> blockShift;
        const int b = ((first + count - 1) & mask) >> blockShift;

        if (stale[(size_t) a] != 0) wipe (a);
        if (stale[(size_t) b] != 0) wipe (b);
    }

    std::vector<float>   buffer;
    std::vector<uint8_t> stale;                     // one flag per block: contents must read as silence
    const SincTable* sinc = nullptr;
    int  mask = 0, wpos = 0;
    int  blockShift = 0, staleCount = 0, sweepBlock = 0;
    bool written = false;
};

} // namespace dlay
