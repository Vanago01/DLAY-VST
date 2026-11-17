#include "Parameters.h"

namespace dlay
{

using APF  = juce::AudioParameterFloat;
using APC  = juce::AudioParameterChoice;
using APB  = juce::AudioParameterBool;
using Attr = juce::AudioParameterFloatAttributes;

// ----------------------------------------------------------------------------
//  value formatting
// ----------------------------------------------------------------------------
namespace
{
    juce::String fmtMs (float v, int)
    {
        // (juce::String (value, 0) means "as many decimals as it takes", so whole numbers are rounded explicitly)
        if (v >= 1000.0f)   return juce::String (v / 1000.0f, 2) + " s";
        if (v >= 100.0f)    return juce::String (juce::roundToInt (v)) + " ms";
        return juce::String (v, 1) + " ms";
    }

    juce::String fmtHz (float v, int)
    {
        if (v >= 1000.0f) return juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz";
        if (v >= 100.0f) return juce::String (juce::roundToInt (v)) + " Hz";
        return juce::String (v, v < 10.0f ? 2 : 1) + " Hz";
    }

    juce::String fmtPercent (float v, int)      { return juce::String (juce::roundToInt (v)) + " %"; }
    juce::String fmtBipolar (float v, int)      { return (v > 0.5f ? "+" : "") + juce::String (juce::roundToInt (v)) + " %"; }
    juce::String fmtDb (float v, int)           { return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB"; }
    juce::String fmtDegrees (float v, int)      { return juce::String (juce::roundToInt (v)) + juce::String::fromUTF8 ("\xc2\xb0"); }

    juce::String fmtSemitones (float v, int)
    {
        const bool whole = std::abs (v - std::round (v)) < 0.005f;
        return (v > 0.004f ? "+" : "") + (whole ? juce::String (juce::roundToInt (v)) : juce::String (v, 2)) + " st";
    }

    juce::String fmtWarp (float v, int)
    {
        const double factor = std::exp2 (2.0 * (double) v / 100.0);        // echo speed factor
        return "x" + juce::String (factor, 2);
    }

    // ---- typed-in values: "1.2 s", "350", "2.5k", "x2" ...
    float parseMs (const juce::String& text)
    {
        const auto t = text.trim().toLowerCase();
        const float v = t.getFloatValue();
        return (t.endsWith ("s") && ! t.endsWith ("ms")) ? v * 1000.0f : v;
    }

    float parseHz (const juce::String& text)
    {
        const auto t = text.trim().toLowerCase();
        return t.containsChar ('k') ? t.getFloatValue() * 1000.0f : t.getFloatValue();
    }

    float parseWarp (const juce::String& text)
    {
        const auto t = text.trim().toLowerCase();

        if (t.startsWithChar ('x'))
        {
            const float factor = juce::jlimit (0.25f, 4.0f, t.substring (1).getFloatValue());
            return 50.0f * std::log2 (factor);
        }

        return t.getFloatValue();
    }

    juce::NormalisableRange<float> skewed (float lo, float hi, float centre, float step = 0.0f)
    {
        juce::NormalisableRange<float> r (lo, hi, step);
        r.setSkewForCentre (centre);
        return r;
    }

    juce::ParameterID pid (const juce::String& s) { return { s, 1 }; }

    std::unique_ptr<APF> pct (const char* idStr, const char* name, float def, float lo = 0.0f, float hi = 100.0f)
    {
        return std::make_unique<APF> (pid (idStr), name, juce::NormalisableRange<float> (lo, hi, 0.01f), def,
                                      Attr().withStringFromValueFunction (lo < 0.0f ? fmtBipolar : fmtPercent));
    }
}

juce::StringArray modeNames()
{
    juce::StringArray a;
    for (const auto& m : kModes) a.add (m.name);
    return a;
}

juce::StringArray divisionNames()
{
    juce::StringArray a;
    for (const auto& d : kDivisions) a.add (d.name);
    return a;
}

juce::StringArray lfoShapeNames()     { return { "SINE", "TRIANGLE", "SQUARE", "SAW", "RANDOM", "S&H", "CUSTOM" }; }
juce::StringArray orbitShapeNames()   { return { "CIRCLE", "FIGURE 8", "PENDULUM", "SPIRAL", "SCATTER" }; }
juce::StringArray satTypeNames()      { return { "TAPE", "TUBE", "DIODE", "FOLD", "CRUSH" }; }
juce::StringArray stereoModeNames()   { return { "STEREO", "MONO", "MID/SIDE" }; }

bool isSessionParameter (const juce::String& paramId)
{
    return paramId == id::bypass || paramId == id::quality;
}

// ----------------------------------------------------------------------------
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // ---- delay engine ---------------------------------------------------------
    layout.add (std::make_unique<APC> (pid (id::mode), "Mode", modeNames(), 0));

    layout.add (std::make_unique<APF> (pid (id::time), "Time", skewed (1.0f, 4000.0f, 300.0f, 0.01f), 375.0f,
                                       Attr().withStringFromValueFunction (fmtMs).withValueFromStringFunction (parseMs)));
    layout.add (std::make_unique<APB> (pid (id::sync), "Sync", true));
    layout.add (std::make_unique<APC> (pid (id::division), "Division", divisionNames(), Div::n4));

    layout.add (pct (id::feedback, "Feedback", 45.0f, 0.0f, 125.0f));
    layout.add (pct (id::mix,      "Mix",      35.0f));

    layout.add (std::make_unique<APC> (pid (id::stereoMode), "Stereo Mode", stereoModeNames(), 0));
    layout.add (pct (id::pingPong, "Ping-Pong", 0.0f));
    layout.add (pct (id::width,    "Width",   100.0f, 0.0f, 200.0f));
    layout.add (pct (id::offset,   "Offset",    0.0f, -100.0f, 100.0f));
    layout.add (pct (id::spread,   "Spread",    0.0f));

    // ---- character ------------------------------------------------------------
    layout.add (pct (id::saturation, "Saturation", 0.0f));
    layout.add (std::make_unique<APF> (pid (id::drive), "Drive", juce::NormalisableRange<float> (0.0f, 24.0f, 0.01f), 0.0f,
                                       Attr().withStringFromValueFunction (fmtDb)));
    layout.add (std::make_unique<APC> (pid (id::satType), "Saturation Type", satTypeNames(), 0));

    layout.add (std::make_unique<APF> (pid (id::lowCut), "Low Cut", skewed (20.0f, 2000.0f, 200.0f, 0.01f), 20.0f,
                                       Attr().withStringFromValueFunction (fmtHz).withValueFromStringFunction (parseHz)));
    layout.add (std::make_unique<APF> (pid (id::highCut), "High Cut", skewed (200.0f, 20000.0f, 3000.0f, 0.01f), 20000.0f,
                                       Attr().withStringFromValueFunction (fmtHz).withValueFromStringFunction (parseHz)));
    layout.add (pct (id::resonance, "Resonance", 0.0f));

    layout.add (pct (id::diffusion, "Diffusion", 0.0f));
    layout.add (pct (id::modDepth,  "Modulation", 0.0f));
    layout.add (std::make_unique<APF> (pid (id::modRate), "Mod Rate", skewed (0.02f, 20.0f, 1.0f, 0.001f), 0.5f,
                                       Attr().withStringFromValueFunction (fmtHz).withValueFromStringFunction (parseHz)));

    layout.add (std::make_unique<APF> (pid (id::pitch), "Pitch", juce::NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f,
                                       Attr().withStringFromValueFunction (fmtSemitones)));
    layout.add (pct (id::reverse, "Reverse", 0.0f));
    layout.add (pct (id::ducking, "Ducking", 0.0f));

    layout.add (pct (id::algoA, "Algorithm A", 50.0f));
    layout.add (pct (id::algoB, "Algorithm B", 50.0f));

    layout.add (std::make_unique<APB> (pid (id::freeze),   "Freeze", false));
    layout.add (std::make_unique<APB> (pid (id::infinite), "Infinite Universe", false));

    // ---- signature features ---------------------------------------------------
    layout.add (std::make_unique<APF> (pid (id::warp), "Time Warp", juce::NormalisableRange<float> (-100.0f, 100.0f, 0.01f), 0.0f,
                                       Attr().withStringFromValueFunction (fmtWarp).withValueFromStringFunction (parseWarp)));
    layout.add (pct (id::inertia, "Inertia", 25.0f));

    layout.add (pct (id::orbit, "Echo Orbit", 0.0f));
    layout.add (std::make_unique<APF> (pid (id::orbitRate), "Orbit Rate", skewed (0.01f, 10.0f, 0.5f, 0.001f), 0.25f,
                                       Attr().withStringFromValueFunction (fmtHz).withValueFromStringFunction (parseHz)));
    layout.add (std::make_unique<APC> (pid (id::orbitShape), "Orbit Shape", orbitShapeNames(), 0));
    layout.add (std::make_unique<APB> (pid (id::orbitSync), "Orbit Sync", false));
    layout.add (std::make_unique<APC> (pid (id::orbitDiv), "Orbit Division", divisionNames(), Div::bar1));

    layout.add (pct (id::ghost,      "Ghost Mode", 0.0f));
    layout.add (pct (id::collapse,   "Time Collapse", 0.0f, -100.0f, 100.0f));
    layout.add (pct (id::random,     "Random Universe", 0.0f));
    layout.add (pct (id::tapeMemory, "Tape Memory", 0.0f));

    // ---- output ---------------------------------------------------------------
    layout.add (std::make_unique<APF> (pid (id::outGain), "Output", juce::NormalisableRange<float> (-24.0f, 12.0f, 0.01f), 0.0f,
                                       Attr().withStringFromValueFunction (fmtDb)));
    layout.add (std::make_unique<APB> (pid (id::limiter), "Limiter", true));
    layout.add (std::make_unique<APB> (pid (id::bypass),  "Bypass", false));
    layout.add (std::make_unique<APC> (pid (id::quality), "Quality", juce::StringArray { "ECO 1x", "HIGH 2x", "ULTRA 4x" }, 1));

    // ---- MOTION lanes ---------------------------------------------------------
    for (int l = 0; l < kNumLanes; ++l)
    {
        const juce::String title = juce::String ("Motion ") + juce::String (kLanes[(size_t) l].name).toLowerCase().replaceSection (0, 1, juce::String (kLanes[(size_t) l].name).substring (0, 1));

        layout.add (std::make_unique<APF> (pid (id::lane (l, id::laneDepth)), title + " Depth",
                                           juce::NormalisableRange<float> (-100.0f, 100.0f, 0.01f), 0.0f,
                                           Attr().withStringFromValueFunction (fmtBipolar)));
        layout.add (std::make_unique<APC> (pid (id::lane (l, id::laneShape)), title + " Shape", lfoShapeNames(), 0));
        layout.add (std::make_unique<APF> (pid (id::lane (l, id::laneRate)), title + " Rate", skewed (0.01f, 20.0f, 1.0f, 0.001f), 0.5f,
                                           Attr().withStringFromValueFunction (fmtHz).withValueFromStringFunction (parseHz)));
        layout.add (std::make_unique<APB> (pid (id::lane (l, id::laneSync)), title + " Sync", true));
        layout.add (std::make_unique<APC> (pid (id::lane (l, id::laneDiv)), title + " Length", divisionNames(), Div::bar1));
        layout.add (std::make_unique<APF> (pid (id::lane (l, id::lanePhase)), title + " Phase",
                                           juce::NormalisableRange<float> (0.0f, 360.0f, 0.1f), 0.0f,
                                           Attr().withStringFromValueFunction (fmtDegrees)));
        layout.add (std::make_unique<APF> (pid (id::lane (l, id::laneStereo)), title + " Stereo",
                                           juce::NormalisableRange<float> (0.0f, 180.0f, 0.1f), 0.0f,
                                           Attr().withStringFromValueFunction (fmtDegrees)));
    }

    return layout;
}

// ----------------------------------------------------------------------------
void ParamCache::attach (juce::AudioProcessorValueTreeState& apvts)
{
    const auto raw = [&apvts] (const juce::String& paramId)
    {
        auto* p = apvts.getRawParameterValue (paramId);
        jassert (p != nullptr);
        return p;
    };

    mode = raw (id::mode);              time = raw (id::time);              sync = raw (id::sync);
    division = raw (id::division);      feedback = raw (id::feedback);      mix = raw (id::mix);
    stereoMode = raw (id::stereoMode);  pingPong = raw (id::pingPong);      width = raw (id::width);
    offset = raw (id::offset);          spread = raw (id::spread);

    saturation = raw (id::saturation);  drive = raw (id::drive);            satType = raw (id::satType);
    lowCut = raw (id::lowCut);          highCut = raw (id::highCut);        resonance = raw (id::resonance);
    diffusion = raw (id::diffusion);    modDepth = raw (id::modDepth);      modRate = raw (id::modRate);
    pitch = raw (id::pitch);            reverse = raw (id::reverse);        ducking = raw (id::ducking);
    algoA = raw (id::algoA);            algoB = raw (id::algoB);
    freeze = raw (id::freeze);          infinite = raw (id::infinite);

    warp = raw (id::warp);              inertia = raw (id::inertia);        orbit = raw (id::orbit);
    orbitRate = raw (id::orbitRate);    orbitShape = raw (id::orbitShape);  orbitSync = raw (id::orbitSync);
    orbitDiv = raw (id::orbitDiv);      ghost = raw (id::ghost);            collapse = raw (id::collapse);
    random = raw (id::random);          tapeMemory = raw (id::tapeMemory);

    outGain = raw (id::outGain);        limiter = raw (id::limiter);        bypass = raw (id::bypass);
    quality = raw (id::quality);

    for (int l = 0; l < kNumLanes; ++l)
        for (int k = 0; k < (int) id::numLaneParams; ++k)
            lanes[(size_t) l][(size_t) k] = raw (id::lane (l, (id::LaneParam) k));
}

EngineParams ParamCache::snapshot() const noexcept
{
    EngineParams p;

    p.mode        = juce::roundToInt (get (mode));
    p.timeMs      = get (time);
    p.sync        = get (sync) > 0.5f;
    p.division    = juce::roundToInt (get (division));

    p.feedback    = get (feedback) * 0.01f;
    p.mix         = get (mix) * 0.01f;
    p.stereoMode  = juce::roundToInt (get (stereoMode));
    p.pingPong    = get (pingPong) * 0.01f;
    p.width       = get (width) * 0.01f;
    p.offset      = get (offset) * 0.01f;
    p.spread      = get (spread) * 0.01f;

    p.saturation  = get (saturation) * 0.01f;
    p.driveDb     = get (drive);
    p.satType     = juce::roundToInt (get (satType));
    p.lowCutHz    = get (lowCut);
    p.highCutHz   = get (highCut);
    p.resonance   = get (resonance) * 0.01f;
    p.diffusion   = get (diffusion) * 0.01f;
    p.modDepth    = get (modDepth) * 0.01f;
    p.modRateHz   = get (modRate);
    p.pitchSt     = get (pitch);
    p.reverse     = get (reverse) * 0.01f;
    p.ducking     = get (ducking) * 0.01f;
    p.algoA       = get (algoA) * 0.01f;
    p.algoB       = get (algoB) * 0.01f;

    p.freeze      = get (freeze) > 0.5f;
    p.infinite    = get (infinite) > 0.5f;

    p.warp        = get (warp) * 0.01f;
    p.inertia     = get (inertia) * 0.01f;
    p.orbit       = get (orbit) * 0.01f;
    p.orbitRateHz = get (orbitRate);
    p.orbitShape  = juce::roundToInt (get (orbitShape));
    p.orbitSync   = get (orbitSync) > 0.5f;
    p.orbitDiv    = juce::roundToInt (get (orbitDiv));
    p.ghost       = get (ghost) * 0.01f;
    p.collapse    = get (collapse) * 0.01f;
    p.random      = get (random) * 0.01f;
    p.tapeMemory  = get (tapeMemory) * 0.01f;

    p.outGainDb   = get (outGain);
    p.limiter     = get (limiter) > 0.5f;
    p.bypass      = get (bypass) > 0.5f;
    p.quality     = juce::roundToInt (get (quality));

    for (int l = 0; l < kNumLanes; ++l)
    {
        const auto& src = lanes[(size_t) l];
        auto& dst = p.lanes[(size_t) l];

        dst.depth    = get (src[id::laneDepth]) * 0.01f;
        dst.shape    = juce::roundToInt (get (src[id::laneShape]));
        dst.rateHz   = get (src[id::laneRate]);
        dst.sync     = get (src[id::laneSync]) > 0.5f;
        dst.division = juce::roundToInt (get (src[id::laneDiv]));
        dst.phase    = get (src[id::lanePhase]) / 360.0f;
        dst.stereo   = get (src[id::laneStereo]) / 360.0f;
    }

    return p;
}

} // namespace dlay
