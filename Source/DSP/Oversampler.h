#pragma once

#include "DspCommon.h"

namespace dlay
{

/** Designs polyphase IIR half-band filters (two parallel all-pass chains).

    This is the classic elliptic all-pass half-band design used for low-latency up/down
    sampling. IIR instead of FIR because the non-linearities sit *inside* the feedback loop:
    an FIR half-band long enough for the same rejection would add milliseconds to every
    pass around the loop.
*/
struct HalfbandDesign
{
    /** @param transition  transition bandwidth as a fraction of the high sample rate (0 .. 0.5) */
    static void compute (double* coefs, int numCoefs, double transition) noexcept
    {
        double k = std::tan ((1.0 - transition * 2.0) * kPi / 4.0);
        k *= k;

        const double kksqrt = std::pow (1.0 - k * k, 0.25);
        const double e  = 0.5 * (1.0 - kksqrt) / (1.0 + kksqrt);
        const double e4 = e * e * e * e;
        const double q  = e * (1.0 + e4 * (2.0 + e4 * (15.0 + 150.0 * e4)));

        const int order = numCoefs * 2 + 1;

        for (int index = 0; index < numCoefs; ++index)
        {
            const int c = index + 1;

            double num = 0.0;
            {
                int i = 0, j = 1;
                double term;
                do
                {
                    term = std::pow (q, (double) (i * (i + 1))) * std::sin ((double) ((i * 2 + 1) * c) * kPi / (double) order) * (double) j;
                    num += term;
                    j = -j;
                    ++i;
                }
                while (std::abs (term) > 1.0e-100 && i < 64);
                num *= std::pow (q, 0.25);
            }

            double den = 0.5;
            {
                int i = 1, j = -1;
                double term;
                do
                {
                    term = std::pow (q, (double) (i * i)) * std::cos ((double) (i * 2 * c) * kPi / (double) order) * (double) j;
                    den += term;
                    j = -j;
                    ++i;
                }
                while (std::abs (term) > 1.0e-100 && i < 64);
            }

            const double ww   = num / den;
            const double wwsq = ww * ww;
            const double x    = std::sqrt ((1.0 - wwsq * k) * (1.0 - wwsq / k)) / (1.0 + wwsq);
            coefs[index]      = (1.0 - x) / (1.0 + x);
        }
    }
};

/** One 2x stage: up-sampler and down-sampler sharing a coefficient set. */
class Halfband2x
{
public:
    static constexpr int kMaxCoefs = 12;

    void design (int numCoefs, double transition) noexcept
    {
        n = std::min (numCoefs & ~1, kMaxCoefs);
        HalfbandDesign::compute (c.data(), n, transition);
        reset();
    }

    void reset() noexcept
    {
        ux.fill (0.0); uy.fill (0.0);
        dx.fill (0.0); dy.fill (0.0);
    }

    inline void up (double x, double& out0, double& out1) noexcept
    {
        double even = x, odd = x;

        for (int i = 0; i < n; i += 2)
        {
            stage (even, c[(size_t) i],     ux[(size_t) i],     uy[(size_t) i]);
            stage (odd,  c[(size_t) i + 1], ux[(size_t) i + 1], uy[(size_t) i + 1]);
        }

        out0 = even;
        out1 = odd;
    }

    inline double down (double in0, double in1) noexcept
    {
        double s0 = in1, s1 = in0;

        for (int i = 0; i < n; i += 2)
        {
            stage (s0, c[(size_t) i],     dx[(size_t) i],     dy[(size_t) i]);
            stage (s1, c[(size_t) i + 1], dx[(size_t) i + 1], dy[(size_t) i + 1]);
        }

        return 0.5 * (s0 + s1);
    }

private:
    static inline void stage (double& s, double coef, double& x1, double& y1) noexcept
    {
        const double y = coef * (s - y1) + x1;
        x1 = s;
        y1 = y;
        s  = y;
    }

    std::array<double, kMaxCoefs> c {}, ux {}, uy {}, dx {}, dy {};
    int n = 0;
};

/** 1x / 2x / 4x wrapper that runs a memoryless shaper at the higher rate. */
class Oversampler
{
public:
    void prepare() noexcept
    {
        stage1.design (10, 0.07);       // steep: guards the audio band
        stage2.design (4, 0.22);        // relaxed: signal is already band-limited to a quarter
        reset();
    }

    void reset() noexcept
    {
        stage1.reset();
        stage2.reset();
    }

    /** @param factor 1, 2 or 4   @param fn callable double(double) */
    template <typename Fn>
    inline double process (double x, int factor, Fn&& fn) noexcept
    {
        if (factor <= 1)
            return fn (x);

        double a, b;
        stage1.up (x, a, b);

        if (factor == 2)
        {
            a = fn (a);
            b = fn (b);
        }
        else
        {
            double a0, a1, b0, b1;
            stage2.up (a, a0, a1);
            stage2.up (b, b0, b1);
            a = stage2.down (fn (a0), fn (a1));
            b = stage2.down (fn (b0), fn (b1));
        }

        return stage1.down (a, b);
    }

    /** Low-frequency group delay of the up+down pair, in base-rate samples (measured once). */
    static double measureLatency (int factor)
    {
        if (factor <= 1)
            return 0.0;

        Oversampler os;
        os.prepare();

        // Smooth pulse -> centroid shift = group delay near DC.
        constexpr int len = 512;
        double numIn = 0.0, denIn = 0.0, numOut = 0.0, denOut = 0.0;

        for (int i = 0; i < len; ++i)
        {
            const double t = ((double) i - 96.0) / 24.0;
            const double x = std::exp (-0.5 * t * t);
            const double y = os.process (x, factor, [] (double v) { return v; });
            numIn  += (double) i * x;   denIn  += x;
            numOut += (double) i * y;   denOut += y;
        }

        return numOut / denOut - numIn / denIn;
    }

private:
    Halfband2x stage1, stage2;
};

} // namespace dlay
