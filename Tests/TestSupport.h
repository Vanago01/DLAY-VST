#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <cstdio>
#include <vector>

#include "PluginProcessor.h"

/** Minimal test scaffolding shared by the DLAY test runner and render tool. */
namespace dlaytest
{

inline int& failures()  { static int n = 0; return n; }
inline int& checks()    { static int n = 0; return n; }

inline void check (bool condition, const juce::String& what)
{
    ++checks();

    if (! condition)
    {
        ++failures();
        std::printf ("    FAIL  %s\n", what.toRawUTF8());
    }
}

inline void section (const char* name)
{
    std::printf ("\n== %s\n", name);
    std::fflush (stdout);
}

struct StereoBuffer
{
    std::vector<float> l, r;

    explicit StereoBuffer (int n = 0) : l ((size_t) n, 0.0f), r ((size_t) n, 0.0f) {}
    int size() const { return (int) l.size(); }

    bool allFinite() const
    {
        for (size_t i = 0; i < l.size(); ++i)
            if (! std::isfinite (l[i]) || ! std::isfinite (r[i]))
                return false;
        return true;
    }

    float peak() const
    {
        float p = 0.0f;
        for (size_t i = 0; i < l.size(); ++i)
            p = std::max (p, std::max (std::abs (l[i]), std::abs (r[i])));
        return p;
    }

    double rms (int from, int to) const
    {
        double acc = 0.0;
        from = std::max (0, from);
        to = std::min (size(), to);
        for (int i = from; i < to; ++i)
            acc += (double) l[(size_t) i] * l[(size_t) i] + (double) r[(size_t) i] * r[(size_t) i];
        return to > from ? std::sqrt (acc / (2.0 * (double) (to - from))) : 0.0;
    }
};

/** A deterministic, vaguely musical test input: pitched pings and noise hits on a 120 bpm
    grid for `activeSeconds`, then silence so the tail can be inspected. */
inline StereoBuffer makeTestSignal (double sampleRate, double totalSeconds, double activeSeconds, uint32_t seed = 1)
{
    const int n = (int) (totalSeconds * sampleRate);
    StereoBuffer buf (n);

    dlay::Rng rng;
    rng.seed (seed * 7919u + 13u);

    const double beat = 0.5;                    // 120 bpm
    static const double notes[] = { 220.0, 277.18, 329.63, 440.0, 554.37, 659.25, 196.0, 392.0 };

    int step = 0;
    for (double t = 0.0; t < activeSeconds; t += beat * 0.5, ++step)
    {
        const int start = (int) (t * sampleRate);
        const bool noiseHit = (step % 4) == 2;
        const double freq = notes[(size_t) ((step * 3 + (int) seed) % 8)];
        const double pan = (step % 3 == 0) ? 0.3 : ((step % 3 == 1) ? 0.7 : 0.5);
        const int len = (int) ((noiseHit ? 0.05 : 0.18) * sampleRate);

        for (int i = 0; i < len && start + i < n; ++i)
        {
            const double env = std::exp (-(double) i / ((noiseHit ? 0.012 : 0.05) * sampleRate));
            const double v = noiseHit ? rng.nextBipolar() * 0.5
                                      : std::sin (dlay::kTwoPi * freq * (double) i / sampleRate) * 0.6;
            buf.l[(size_t) (start + i)] += (float) (v * env * (1.0 - pan) * 1.4);
            buf.r[(size_t) (start + i)] += (float) (v * env * pan * 1.4);
        }
    }

    return buf;
}

/** A short musical phrase for listening tests: two bars of plucked arpeggio and chord stabs
    at 120 bpm (Karplus-Strong strings), followed by silence for the tail. */
inline StereoBuffer makeDemoPhrase (double sampleRate, double totalSeconds)
{
    const int n = (int) (totalSeconds * sampleRate);
    StereoBuffer buf (n);
    dlay::Rng rng;
    rng.seed (2024u);

    const auto pluck = [&] (double startSeconds, double freq, double level, double pan, double brightness)
    {
        const int period = std::max (2, (int) std::lround (sampleRate / freq));
        std::vector<double> string ((size_t) period);

        double lp = 0.0, mean = 0.0;
        for (auto& v : string)
        {
            lp += brightness * (rng.nextBipolar() - lp);       // darker pick for lower brightness
            v = lp;
            mean += lp;
        }

        mean /= (double) period;
        for (auto& v : string)
            v -= mean;                                         // a string has no DC

        const int start = (int) (startSeconds * sampleRate);
        const int length = (int) (2.2 * sampleRate);
        const double damping = 0.996 - 0.25 * freq / sampleRate;
        int idx = 0;

        for (int i = 0; i < length && start + i < n; ++i)
        {
            const int next = (idx + 1) % period;
            const double out = string[(size_t) idx];
            string[(size_t) idx] = damping * 0.5 * (out + string[(size_t) next]);
            idx = next;

            const double fadeIn = std::min (1.0, (double) i / (0.002 * sampleRate));
            buf.l[(size_t) (start + i)] += (float) (out * level * fadeIn * std::cos ((pan + 1.0) * dlay::kPi * 0.25));
            buf.r[(size_t) (start + i)] += (float) (out * level * fadeIn * std::sin ((pan + 1.0) * dlay::kPi * 0.25));
        }
    };

    const double eighth = 0.25;
    static const double arp[] = { 110.0, 164.81, 220.0, 261.63, 329.63, 493.88, 523.25, 659.25 };   // A minor 9

    for (int i = 0; i < 8; ++i)                                             // bar 1: rising arpeggio
        pluck (eighth * (double) i, arp[i], 0.55, (i % 2 == 0) ? -0.25 : 0.25, 0.55);

    for (const double f : { 220.0, 261.63, 329.63 })                        // bar 2: stab, then two answers
        pluck (2.0, f, 0.42, 0.0, 0.7);

    pluck (3.0, 440.0, 0.5, 0.3, 0.6);
    pluck (3.5, 392.0, 0.5, -0.3, 0.6);

    float peak = 0.0f;
    for (int i = 0; i < n; ++i)
        peak = std::max (peak, std::max (std::abs (buf.l[(size_t) i]), std::abs (buf.r[(size_t) i])));

    if (peak > 0.0f)
        for (int i = 0; i < n; ++i)
        {
            buf.l[(size_t) i] *= 0.7f / peak;
            buf.r[(size_t) i] *= 0.7f / peak;
        }

    return buf;
}

/** Host transport stand-in. */
class TestPlayHead : public juce::AudioPlayHead
{
public:
    double bpm = 120.0, ppq = 0.0;
    bool playing = true;

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm (bpm);
        info.setPpqPosition (ppq);
        info.setIsPlaying (playing);
        return info;
    }
};

/** Runs a buffer through the full plugin (parameters, presets, MOTION and all). */
inline StereoBuffer renderThroughProcessor (DlayAudioProcessor& proc, const StereoBuffer& input,
                                            double sampleRate, int blockSize, bool playing = true)
{
    TestPlayHead playHead;
    playHead.playing = playing;
    proc.setPlayHead (&playHead);
    proc.setRateAndBufferSizeDetails (sampleRate, blockSize);
    proc.prepareToPlay (sampleRate, blockSize);

    StereoBuffer out (input.size());
    juce::AudioBuffer<float> block (2, blockSize);
    juce::MidiBuffer midi;

    for (int pos = 0; pos < input.size(); pos += blockSize)
    {
        const int n = std::min (blockSize, input.size() - pos);
        block.setSize (2, n, false, false, true);
        block.copyFrom (0, 0, input.l.data() + pos, n);
        block.copyFrom (1, 0, input.r.data() + pos, n);

        proc.processBlock (block, midi);

        std::copy (block.getReadPointer (0), block.getReadPointer (0) + n, out.l.begin() + pos);
        std::copy (block.getReadPointer (1), block.getReadPointer (1) + n, out.r.begin() + pos);
        playHead.ppq += (double) n * playHead.bpm / (60.0 * sampleRate);
    }

    proc.setPlayHead (nullptr);
    return out;
}

/** Runs a buffer through the bare engine. */
inline StereoBuffer renderThroughEngine (dlay::DlayEngine& engine, const StereoBuffer& input,
                                         const dlay::EngineParams& params, int blockSize,
                                         dlay::TransportInfo transport = {})
{
    StereoBuffer out = input;
    const double sr = engine.getSampleRate();

    for (int pos = 0; pos < out.size(); pos += blockSize)
    {
        const int n = std::min (blockSize, out.size() - pos);
        engine.process (out.l.data() + pos, out.r.data() + pos, n, params, transport);
        transport.ppq += (double) n * transport.bpm / (60.0 * sr);
    }

    return out;
}

inline void setParam (DlayAudioProcessor& proc, const juce::String& paramId, float realValue)
{
    if (auto* p = proc.apvts.getParameter (paramId))
        p->setValueNotifyingHost (p->convertTo0to1 (realValue));
    else
        check (false, "unknown parameter " + paramId);
}

inline float getParam (DlayAudioProcessor& proc, const juce::String& paramId)
{
    if (auto* p = proc.apvts.getParameter (paramId))
        return p->convertFrom0to1 (p->getValue());
    return 0.0f;
}

/** 24-bit PCM WAV writer (self-contained so the tools have no codec dependencies). */
inline bool writeWav (const juce::File& file, const StereoBuffer& buf, double sampleRate)
{
    file.getParentDirectory().createDirectory();
    file.deleteFile();

    juce::FileOutputStream out (file);
    if (! out.openedOk())
        return false;

    const int numSamples = buf.size();
    const int dataBytes  = numSamples * 2 * 3;

    out.write ("RIFF", 4);  out.writeInt (36 + dataBytes);
    out.write ("WAVE", 4);
    out.write ("fmt ", 4);  out.writeInt (16);
    out.writeShort (1);     out.writeShort (2);
    out.writeInt ((int) sampleRate);
    out.writeInt ((int) sampleRate * 2 * 3);
    out.writeShort (6);     out.writeShort (24);
    out.write ("data", 4);  out.writeInt (dataBytes);

    for (int i = 0; i < numSamples; ++i)
    {
        for (const float v : { buf.l[(size_t) i], buf.r[(size_t) i] })
        {
            const int s = (int) std::lround (juce::jlimit (-1.0f, 1.0f, v) * 8388607.0f);
            out.writeByte ((char) (s & 0xff));
            out.writeByte ((char) ((s >> 8) & 0xff));
            out.writeByte ((char) ((s >> 16) & 0xff));
        }
    }

    out.flush();
    return true;
}

} // namespace dlaytest
