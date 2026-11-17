#pragma once

#include "../Params/Parameters.h"
#include "../Motion/MotionCurves.h"

namespace dlay
{

enum class Cat
{
    analogTape = 0, vintageEchoes, modernDigital, rhythmic, pingPongStereo, ambient, experimental,
    glitch, pitchShimmer, vocal, guitar, cinematic, loFi, creativeMotion, extremeFeedback, numCategories
};

inline constexpr std::array<const char*, (size_t) Cat::numCategories> kCategoryNames
{
    "Analog & Tape", "Vintage Echoes", "Modern Digital", "Rhythmic & BPM", "Ping Pong & Stereo",
    "Atmospheric & Ambient", "Experimental", "Glitch & IDM", "Pitch & Shimmer", "Vocal Effects",
    "Guitar & Instrument", "Cinematic & Sound Design", "Lo-Fi & Degraded", "Creative Motion", "Extreme Feedback"
};

/** A factory preset: only the values that differ from the defaults, in real units. */
struct FactoryPreset
{
    juce::String name;
    Cat category = Cat::modernDigital;
    juce::String description;
    std::vector<std::pair<juce::String, float>> values;
    std::vector<std::pair<int, std::vector<CurvePoint>>> curves;
};

const std::vector<FactoryPreset>& getFactoryPresets();

} // namespace dlay
