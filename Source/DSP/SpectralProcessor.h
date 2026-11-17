#pragma once

#include <juce_dsp/juce_dsp.h>
#include "DspCommon.h"

namespace dlay
{

/** STFT processor used by the SPECTRAL algorithm (one instance per channel).

    Every frequency bin gets its own extra delay, read from a ring of past spectra:
      - TILT disperses the spectrum (lows first or highs first, a chirping "pew")
      - SCATTER gives every bin a random extra delay
      - SMEAR holds each bin's magnitude and lets it decay slowly (spectral sustain)

    It sits in the feedback loop, so each repeat is dispersed and smeared further.
    75 % overlap, Hann analysis and synthesis windows. Fixed latency = FFT size.

    The ring of past spectra is megabytes long, so reset() does not zero-fill it: it only
    marks every frame as empty, and an empty frame reads as silence until it is rewritten.
*/
class SpectralProcessor
{
public:
    void prepare (double sampleRate, uint32_t seed)
    {
        sr    = sampleRate;
        order = sampleRate <= 50000.0 ? 10 : (sampleRate <= 100000.0 ? 11 : 12);
        n     = 1 << order;
        hop   = n / 4;
        bins  = n / 2 + 1;

        fft = std::make_unique<juce::dsp::FFT> (order);

        maxFrames = (int) std::ceil (kMaxExtraSeconds * sampleRate / (double) hop) + 2;

        window.resize ((size_t) n);
        for (int i = 0; i < n; ++i)
            window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (kTwoPi * (double) i / (double) n));

        inRing.assign ((size_t) n, 0.0f);
        outRing.assign ((size_t) n, 0.0f);
        work.assign ((size_t) (2 * n), 0.0f);
        history.assign ((size_t) maxFrames * (size_t) bins * 2, 0.0f);
        frameFilled.assign ((size_t) maxFrames, (uint8_t) 0);
        hold.assign ((size_t) bins, 0.0f);
        unitRe.assign ((size_t) bins, 1.0f);
        unitIm.assign ((size_t) bins, 0.0f);
        binDelay.assign ((size_t) bins, 0);
        binPos.resize ((size_t) bins);
        randA.resize ((size_t) bins);
        randB.resize ((size_t) bins);

        const double binHz = sampleRate / (double) n;
        const double kLo = 60.0 / binHz, kHi = 12000.0 / binHz;

        for (int k = 0; k < bins; ++k)
        {
            binPos[(size_t) k] = (float) clampT (std::log2 (std::max (1.0, (double) k) / kLo) / std::log2 (kHi / kLo), 0.0, 1.0);
            randA[(size_t) k]  = (float) hash01 ((uint32_t) k, 0x51u);
            randB[(size_t) k]  = (float) hash01 ((uint32_t) k, seed);
        }

        reset();
        dirty = true;
    }

    void reset() noexcept
    {
        std::fill (inRing.begin(), inRing.end(), 0.0f);
        std::fill (outRing.begin(), outRing.end(), 0.0f);
        std::fill (frameFilled.begin(), frameFilled.end(), (uint8_t) 0);
        std::fill (hold.begin(), hold.end(), 0.0f);
        std::fill (unitRe.begin(), unitRe.end(), 1.0f);
        std::fill (unitIm.begin(), unitIm.end(), 0.0f);
        ringPos = 0;
        hopCount = 0;
        frameIndex = 0;
    }

    int latency() const noexcept { return n; }

    /** @param tilt -1..1   @param smear 0..1   @param scatter 0..1   @param stereoScatter 0..1 */
    void setParams (double tilt, double smear, double scatter, double stereoScatter, double dispersionSeconds) noexcept
    {
        const double disp = clampT (dispersionSeconds, 0.0, kMaxExtraSeconds) * sr / (double) hop;

        if (std::abs (tilt - curTilt) > 1.0e-3 || std::abs (scatter - curScatter) > 1.0e-3
            || std::abs (stereoScatter - curStereo) > 1.0e-3 || std::abs (disp - curDisp) > 0.25)
        {
            curTilt = tilt; curScatter = scatter; curStereo = stereoScatter; curDisp = disp;
            dirty = true;
        }

        // per-frame magnitude decay: 0 = none, 1 = ~6 s tail
        holdDecay = smear <= 1.0e-4 ? 0.0f
                                    : (float) std::exp (-(double) hop / (sr * (0.05 + 6.0 * smear * smear)));

        // sustaining every band adds energy; give part of it back so SMEAR is not a volume control
        smearGain = (float) (1.0 / std::sqrt (1.0 + 4.2 * std::pow (clampT (smear, 0.0, 1.0), 0.8)));
    }

    inline double process (double x) noexcept
    {
        inRing[(size_t) ringPos] = (float) x;
        const double y = (double) outRing[(size_t) ringPos];
        outRing[(size_t) ringPos] = 0.0f;
        ringPos = (ringPos + 1) & (n - 1);

        if (++hopCount >= hop)
        {
            hopCount = 0;
            processFrame();
        }

        return y;
    }

private:
    static constexpr double kMaxExtraSeconds = 1.0;

    void updateBinDelays() noexcept
    {
        const double amount = std::abs (curTilt);

        for (int k = 0; k < bins; ++k)
        {
            const double pos = curTilt >= 0.0 ? (double) binPos[(size_t) k] : 1.0 - (double) binPos[(size_t) k];
            const double rnd = lerpD ((double) randA[(size_t) k], (double) randB[(size_t) k], curStereo);
            const double fr  = curDisp * (amount * pos + curScatter * rnd);
            binDelay[(size_t) k] = (uint16_t) clampT ((int) std::lround (fr), 0, maxFrames - 1);
        }

        dirty = false;
    }

    void processFrame() noexcept
    {
        if (dirty)
            updateBinDelays();

        for (int i = 0; i < n; ++i)
            work[(size_t) i] = inRing[(size_t) ((ringPos + i) & (n - 1))] * window[(size_t) i];

        std::fill (work.begin() + n, work.end(), 0.0f);
        fft->performRealOnlyForwardTransform (work.data(), true);

        float* frame = history.data() + (size_t) frameIndex * (size_t) bins * 2;
        std::memcpy (frame, work.data(), sizeof (float) * (size_t) bins * 2);
        frameFilled[(size_t) frameIndex] = 1;

        for (int k = 0; k < bins; ++k)
        {
            int src = frameIndex - (int) binDelay[(size_t) k];
            if (src < 0) src += maxFrames;

            float re = 0.0f, im = 0.0f;

            if (frameFilled[(size_t) src] != 0)
            {
                const float* h = history.data() + ((size_t) src * (size_t) bins + (size_t) k) * 2;
                re = h[0];
                im = h[1];
            }

            const float mag  = std::sqrt (re * re + im * im);
            const float held = hold[(size_t) k] * holdDecay;

            if (mag >= held)
            {
                hold[(size_t) k] = mag;

                if (mag > 1.0e-12f)
                {
                    unitRe[(size_t) k] = re / mag;
                    unitIm[(size_t) k] = im / mag;
                }
            }
            else
            {
                // sustain: keep the held magnitude and advance the phase by the bin's
                // centre-frequency increment (k * pi/2 for a quarter-window hop)
                hold[(size_t) k] = held;
                const float ur = unitRe[(size_t) k], ui = unitIm[(size_t) k];

                switch (k & 3)
                {
                    case 1:  unitRe[(size_t) k] = -ui; unitIm[(size_t) k] =  ur; break;
                    case 2:  unitRe[(size_t) k] = -ur; unitIm[(size_t) k] = -ui; break;
                    case 3:  unitRe[(size_t) k] =  ui; unitIm[(size_t) k] = -ur; break;
                    default: break;
                }

                re = held * unitRe[(size_t) k];
                im = held * unitIm[(size_t) k];
            }

            work[(size_t) (2 * k)]     = re;
            work[(size_t) (2 * k + 1)] = im;
        }

        for (int k = 1; k < n / 2; ++k)                 // conjugate-symmetric upper half
        {
            work[(size_t) (2 * (n - k))]     =  work[(size_t) (2 * k)];
            work[(size_t) (2 * (n - k) + 1)] = -work[(size_t) (2 * k + 1)];
        }

        fft->performRealOnlyInverseTransform (work.data());

        const float norm = smearGain / 1.5f;            // 1.5 = sum of Hann^2 at 75 % overlap

        for (int i = 0; i < n; ++i)
            outRing[(size_t) ((ringPos + i) & (n - 1))] += work[(size_t) i] * window[(size_t) i] * norm;

        if (++frameIndex >= maxFrames)
            frameIndex = 0;
    }

    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window, inRing, outRing, work, history, hold, unitRe, unitIm, binPos, randA, randB;
    std::vector<uint16_t> binDelay;
    std::vector<uint8_t>  frameFilled;              // per history frame: written since the last reset()

    double sr = 48000.0;
    int order = 10, n = 1024, hop = 256, bins = 513, maxFrames = 1;
    int ringPos = 0, hopCount = 0, frameIndex = 0;

    double curTilt = 0.0, curScatter = 0.0, curStereo = 0.0, curDisp = 0.0;
    float holdDecay = 0.0f, smearGain = 1.0f;
    bool dirty = true;
};

} // namespace dlay
