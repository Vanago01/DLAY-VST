#pragma once

#include <array>

namespace dlay
{

// ----------------------------------------------------------------------------
//  Delay algorithms
// ----------------------------------------------------------------------------
enum class Mode
{
    // CLASSIC
    digital = 0, stereo, pingPong, dual, multiTap, slapback,
    // VINTAGE
    tape, bbd, tapeEcho, warmAnalog, degraded, loFi,
    // CREATIVE
    reverse, granular, pitch, spectral, diffused, glitch, fractal, random, shimmer,
    numModes
};

constexpr int kNumModes = (int) Mode::numModes;

enum class ModeFamily { classic = 0, vintage, creative };

struct ModeInfo
{
    const char* name;
    ModeFamily  family;
    const char* macroA;      // label of the algorithm's first dedicated control
    const char* macroB;      // label of the second
    const char* tipA;
    const char* tipB;
    const char* description;
};

inline constexpr std::array<ModeInfo, (size_t) kNumModes> kModes { {
    { "DIGITAL",     ModeFamily::classic,  "COLOR",    "TILT",    "Converter character: centre is pristine, left rolls off the top like an early digital unit, right adds low-resolution grit",
                                                                   "Tone of the repeats: darker to the left, thinner and brighter to the right",
      "Pristine digital delay. Sample-accurate, flat, endless headroom." },
    { "STEREO",      ModeFamily::classic,  "R RATIO",  "TILT",    "Right-channel time as a musical ratio of the left",
                                                                   "Tone of the repeats: darker to the left, brighter to the right",
      "True stereo delay with independent left and right times." },
    { "PING-PONG",   ModeFamily::classic,  "PATTERN",  "ANGLE",   "Bounce pattern: L-R, L-C-R-C, L-R-C, L-L-R-R, L-R-R-L, C-L-C-R",
                                                                   "How far apart the bounces are thrown",
      "Pattern bouncer: echoes hop across the stereo field in selectable figures." },
    { "DUAL",        ModeFamily::classic,  "B RATIO",  "BALANCE", "Time of delay B relative to delay A",
                                                                   "Level balance between delay A (left) and delay B (right)",
      "Two independent delays, A on the left and B on the right." },
    { "MULTI-TAP",   ModeFamily::classic,  "TAPS",     "SHAPE",   "Number of taps spread across the delay time (2 to 8)",
                                                                   "Tap levels: fading out, flat, or swelling in",
      "Up to eight evenly spaced taps; the pattern repeats through the feedback." },
    { "SLAPBACK",    ModeFamily::classic,  "SNAP",     "DOUBLER", "Transient emphasis of the slap",
                                                                   "Adds a second, wider slap for an automatic double-track",
      "Short, punchy single repeat. Rockabilly to room." },

    { "TAPE",        ModeFamily::vintage,  "WOW/FLT",  "AGE",     "Wow and flutter of the tape transport",
                                                                   "Tape wear: high-frequency loss, dropouts and noise",
      "Studio tape delay: head bump, soft saturation, speed-dependent tone." },
    { "BBD",         ModeFamily::vintage,  "STAGES",   "CLOCK",   "Bucket-brigade chip size: fewer stages = darker, grainier long delays",
                                                                   "Clock bleed and sample-and-hold grit",
      "Bucket-brigade analog delay. Gets darker the longer it is." },
    { "TAPE ECHO",   ModeFamily::vintage,  "HEADS",    "WOW/FLT", "Playback head combination: 1, 2, 3, 1+2, 2+3, 1+3, all",
                                                                   "Wow and flutter of the tape loop",
      "Three-head tape echo unit with a worn loop." },
    { "WARM ANALOG", ModeFamily::vintage,  "WARMTH",   "DRIFT",   "Darkens and fattens every pass through the loop",
                                                                   "Slow, random wander of the delay time",
      "Smooth analog repeats that melt into each other and self-oscillate gracefully." },
    { "DEGRADED",    ModeFamily::vintage,  "LOSS",     "DUST",    "How much bandwidth each repeat loses",
                                                                   "Crackle, dropouts and noise that grow with the signal",
      "Every repeat is a worse copy of the last: narrower, dustier, further away." },
    { "LO-FI",       ModeFamily::vintage,  "BITS",     "RATE",    "Bit depth reduction",
                                                                   "Sample-rate reduction",
      "Early-sampler echo: crushed bits, low sample rate, narrow band." },

    { "REVERSE",     ModeFamily::creative, "SMOOTH",   "SWELL",   "Crossfade between choppy reversed chunks and a seamless reverse wash",
                                                                   "Shape of each reversed chunk: flat or swelling in",
      "Plays the past backwards. REVERSE sets how much of the feedback is re-reversed." },
    { "GRANULAR",    ModeFamily::creative, "GRAIN",    "SPRAY",   "Grain length",
                                                                   "Scatter of grain positions and pitch around the delay time",
      "A cloud of grains read from around the delay time." },
    { "PITCH",       ModeFamily::creative, "DETUNE",   "WINDOW",  "Micro-detune between left and right",
                                                                   "Shifter window: short is tight and grainy, long is smooth",
      "Pitch-shifter in the feedback loop: every repeat climbs or falls." },
    { "SPECTRAL",    ModeFamily::creative, "TILT",     "SMEAR",   "Disperses the spectrum: lows first (left) or highs first (right)",
                                                                   "Sustains each frequency band after it passes",
      "Each frequency band has its own delay time." },
    { "DIFFUSED",    ModeFamily::creative, "SIZE",     "DENSITY", "Size of the diffusion network",
                                                                   "How quickly echoes dissolve into a wash",
      "Echoes that bloom into reverb the longer they repeat." },
    { "GLITCH",      ModeFamily::creative, "SLICE",    "CHAOS",   "Slices per delay time",
                                                                   "Probability of stutters, reverses, tape-stops and gates",
      "Rhythmic buffer slicer: stutter, reverse, speed jumps and gates on the grid." },
    { "FRACTAL",     ModeFamily::creative, "DEPTH",    "RATIO",   "Number of nested echo scales (2 to 6)",
                                                                   "Size ratio between scales, around the golden section",
      "Self-similar echoes inside echoes that grow denser with every pass." },
    { "RANDOM",      ModeFamily::creative, "CHAOS",    "QUANTIZE","How far and how often the taps are re-rolled",
                                                                   "Locks the random taps to a rhythmic grid",
      "Taps that land somewhere new on every cycle." },
    { "SHIMMER",     ModeFamily::creative, "SHIMMER",  "SIZE",    "Amount of pitched signal fed back",
                                                                   "Size of the diffusion network",
      "Pitch-shifted, diffused feedback: rising halos of harmonics." },
} };

inline const ModeInfo& modeInfo (int index) noexcept
{
    return kModes[(size_t) (index < 0 ? 0 : (index >= kNumModes ? kNumModes - 1 : index))];
}

// ----------------------------------------------------------------------------
//  Note values shared by the delay time, MOTION lanes and ECHO ORBIT
// ----------------------------------------------------------------------------
struct Division
{
    const char* name;
    double beats;        // length in quarter notes
};

inline constexpr std::array<Division, 23> kDivisions { {
    { "1/64",  0.0625 },      { "1/32T", 1.0 / 12.0 },  { "1/32",  0.125 },
    { "1/16T", 1.0 / 6.0 },   { "1/32D", 0.1875 },      { "1/16",  0.25 },
    { "1/8T",  1.0 / 3.0 },   { "1/16D", 0.375 },       { "1/8",   0.5 },
    { "1/4T",  2.0 / 3.0 },   { "1/8D",  0.75 },        { "1/4",   1.0 },
    { "1/2T",  4.0 / 3.0 },   { "1/4D",  1.5 },         { "1/2",   2.0 },
    { "1/1T",  8.0 / 3.0 },   { "1/2D",  3.0 },         { "1 BAR", 4.0 },
    { "1.5 BAR", 6.0 },       { "2 BARS", 8.0 },        { "4 BARS", 16.0 },
    { "8 BARS", 32.0 },       { "16 BARS", 64.0 }
} };

constexpr int kNumDivisions = (int) kDivisions.size();

/** Symbolic indices into kDivisions (used by the factory presets). */
namespace Div
{
    enum : int
    {
        n64 = 0, n32t, n32, n16t, n32d, n16, n8t, n16d, n8, n4t, n8d, n4, n2t, n4d, n2, n1t, n2d,
        bar1, bar1d, bar2, bar4, bar8, bar16
    };
}

// ----------------------------------------------------------------------------
enum class StereoMode { stereo = 0, mono, midSide, numModes };

enum class SatType { tape = 0, tube, diode, fold, crush, numTypes };

enum class OrbitShape { circle = 0, figure8, pendulum, spiral, scatter, numShapes };

enum class LfoShape { sine = 0, triangle, square, saw, random, sampleHold, custom, numShapes };

enum class Lane { speed = 0, pan, feedback, filter, pitch, mix, numLanes };

constexpr int kNumLanes = (int) Lane::numLanes;

struct LaneInfo
{
    const char* id;          // parameter-id prefix
    const char* name;
    const char* unitHint;    // what +100 % depth means
};

inline constexpr std::array<LaneInfo, (size_t) kNumLanes> kLanes { {
    { "spd", "SPEED",    "echoes twice as fast / half as fast" },
    { "pan", "PAN",      "full left to full right" },
    { "fbk", "FEEDBACK", "+/- 50 % feedback" },
    { "flt", "FILTER",   "down: low-pass closes, up: high-pass opens" },
    { "pch", "PITCH",    "+/- 12 semitones" },
    { "mix", "MIX",      "echo level across the whole MIX range (the dry signal is not touched)" },
} };

} // namespace dlay
