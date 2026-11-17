#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../DSP/EngineTypes.h"

namespace dlay
{

/** Parameter identifiers. Every one of these is automatable and stored in presets. */
namespace id
{
   #define DLAY_ID(name) inline constexpr const char* name = #name;

    DLAY_ID (mode)        DLAY_ID (time)        DLAY_ID (sync)        DLAY_ID (division)
    DLAY_ID (feedback)    DLAY_ID (mix)         DLAY_ID (stereoMode)  DLAY_ID (pingPong)
    DLAY_ID (width)       DLAY_ID (offset)      DLAY_ID (spread)

    DLAY_ID (saturation)  DLAY_ID (drive)       DLAY_ID (satType)     DLAY_ID (lowCut)
    DLAY_ID (highCut)     DLAY_ID (resonance)   DLAY_ID (diffusion)   DLAY_ID (modDepth)
    DLAY_ID (modRate)     DLAY_ID (pitch)       DLAY_ID (reverse)     DLAY_ID (ducking)
    DLAY_ID (algoA)       DLAY_ID (algoB)

    DLAY_ID (freeze)      DLAY_ID (infinite)

    DLAY_ID (warp)        DLAY_ID (inertia)     DLAY_ID (orbit)       DLAY_ID (orbitRate)
    DLAY_ID (orbitShape)  DLAY_ID (orbitSync)   DLAY_ID (orbitDiv)    DLAY_ID (ghost)
    DLAY_ID (collapse)    DLAY_ID (random)      DLAY_ID (tapeMemory)

    DLAY_ID (outGain)     DLAY_ID (limiter)     DLAY_ID (bypass)      DLAY_ID (quality)

   #undef DLAY_ID

    /** Per-lane MOTION parameter suffixes. */
    inline constexpr std::array<const char*, 7> laneSuffix { "depth", "shape", "rate", "sync", "div", "phase", "stereo" };

    enum LaneParam { laneDepth = 0, laneShape, laneRate, laneSync, laneDiv, lanePhase, laneStereo, numLaneParams };

    inline juce::String lane (int laneIndex, LaneParam which)
    {
        return juce::String ("m_") + kLanes[(size_t) laneIndex].id + "_" + laneSuffix[(size_t) which];
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

juce::StringArray modeNames();
juce::StringArray divisionNames();
juce::StringArray lfoShapeNames();
juce::StringArray orbitShapeNames();
juce::StringArray satTypeNames();
juce::StringArray stereoModeNames();

/** Parameters that a preset never touches (session-level settings). */
bool isSessionParameter (const juce::String& paramId);

/** Lock-free view of the parameter tree for the audio thread. */
class ParamCache
{
public:
    void attach (juce::AudioProcessorValueTreeState& apvts);

    /** Reads every parameter once and converts to engine units. Audio-thread safe. */
    EngineParams snapshot() const noexcept;

private:
    static float get (const std::atomic<float>* p) noexcept { return p->load (std::memory_order_relaxed); }

    std::atomic<float>* mode = nullptr, *time = nullptr, *sync = nullptr, *division = nullptr;
    std::atomic<float>* feedback = nullptr, *mix = nullptr, *stereoMode = nullptr, *pingPong = nullptr;
    std::atomic<float>* width = nullptr, *offset = nullptr, *spread = nullptr;
    std::atomic<float>* saturation = nullptr, *drive = nullptr, *satType = nullptr, *lowCut = nullptr;
    std::atomic<float>* highCut = nullptr, *resonance = nullptr, *diffusion = nullptr, *modDepth = nullptr;
    std::atomic<float>* modRate = nullptr, *pitch = nullptr, *reverse = nullptr, *ducking = nullptr;
    std::atomic<float>* algoA = nullptr, *algoB = nullptr, *freeze = nullptr, *infinite = nullptr;
    std::atomic<float>* warp = nullptr, *inertia = nullptr, *orbit = nullptr, *orbitRate = nullptr;
    std::atomic<float>* orbitShape = nullptr, *orbitSync = nullptr, *orbitDiv = nullptr, *ghost = nullptr;
    std::atomic<float>* collapse = nullptr, *random = nullptr, *tapeMemory = nullptr;
    std::atomic<float>* outGain = nullptr, *limiter = nullptr, *bypass = nullptr, *quality = nullptr;

    std::array<std::array<std::atomic<float>*, (size_t) id::numLaneParams>, (size_t) kNumLanes> lanes {};
};

} // namespace dlay
