#pragma once

#include "DspCommon.h"
#include "../Params/Modes.h"
#include "../Motion/MotionEngine.h"

namespace dlay
{

/** A plain snapshot of every parameter, in engine units. Built once per block. */
struct EngineParams
{
    int   mode        = 0;
    float timeMs      = 375.0f;
    bool  sync        = true;
    int   division    = Div::n4;

    float feedback    = 0.45f;      // 0 .. 1.25
    float mix         = 0.35f;      // 0 .. 1
    int   stereoMode  = 0;          // StereoMode
    float pingPong    = 0.0f;       // 0 .. 1
    float width       = 1.0f;       // 0 .. 2
    float offset      = 0.0f;       // -1 .. 1
    float spread      = 0.0f;       // 0 .. 1

    float saturation  = 0.0f;       // 0 .. 1
    float driveDb     = 0.0f;       // 0 .. 24
    int   satType     = 0;          // SatType
    float lowCutHz    = 20.0f;
    float highCutHz   = 20000.0f;
    float resonance   = 0.0f;       // 0 .. 1
    float diffusion   = 0.0f;       // 0 .. 1
    float modDepth    = 0.0f;       // 0 .. 1
    float modRateHz   = 0.5f;
    float pitchSt     = 0.0f;       // -24 .. 24
    float reverse     = 0.0f;       // 0 .. 1
    float ducking     = 0.0f;       // 0 .. 1
    float algoA       = 0.5f;       // 0 .. 1
    float algoB       = 0.5f;       // 0 .. 1

    bool  freeze      = false;
    bool  infinite    = false;

    float warp        = 0.0f;       // -1 .. 1   TIME WARP
    float inertia     = 0.25f;      // 0 .. 1
    float orbit       = 0.0f;       // 0 .. 1    ECHO ORBIT amount
    float orbitRateHz = 0.25f;
    int   orbitShape  = 0;          // OrbitShape
    bool  orbitSync   = false;
    int   orbitDiv    = Div::bar1;
    float ghost       = 0.0f;       // 0 .. 1    GHOST MODE
    float collapse    = 0.0f;       // -1 .. 1   TIME COLLAPSE
    float random      = 0.0f;       // 0 .. 1    RANDOM UNIVERSE
    float tapeMemory  = 0.0f;       // 0 .. 1    TAPE MEMORY

    float outGainDb   = 0.0f;
    bool  limiter     = true;
    bool  bypass      = false;
    int   quality     = 1;          // 0 = 1x, 1 = 2x, 2 = 4x oversampling

    std::array<LaneParams, (size_t) kNumLanes> lanes {};
};

struct TransportInfo
{
    double bpm      = 120.0;
    double ppq      = 0.0;
    bool   ppqValid = false;
    bool   playing  = false;
};

// ----------------------------------------------------------------------------
//  Containment
//
//  Everything that reaches the engine from outside - parameter values, the host's
//  transport - passes through here first. A value that is not a number (a damaged
//  session, a host reporting NaN) would otherwise slip through every comparison, settle
//  in a smoother or a filter and stay there until the plug-in is reloaded.
// ----------------------------------------------------------------------------

inline void sanitise (TransportInfo& t) noexcept
{
    t.bpm = usableBpm (t.bpm);

    if (! (std::abs (t.ppq) < 1.0e12))
    {
        t.ppq = 0.0;
        t.ppqValid = false;
    }
}

/** Forces every field into its legal range; a field that is not a number takes its default. */
inline void sanitise (EngineParams& p) noexcept
{
    const EngineParams defaults;

    const auto real = [] (float& v, float lo, float hi, float fallback) noexcept
    {
        v = v >= lo ? (v <= hi ? v : hi) : (v < lo ? lo : fallback);       // NaN fails both tests
    };

    const auto whole = [] (int& v, int lo, int hi) noexcept
    {
        v = v < lo ? lo : (v > hi ? hi : v);
    };

    whole (p.mode, 0, kNumModes - 1);
    real  (p.timeMs, 1.0f, 10000.0f, defaults.timeMs);
    whole (p.division, 0, kNumDivisions - 1);

    real  (p.feedback, 0.0f, 1.25f, defaults.feedback);
    real  (p.mix, 0.0f, 1.0f, defaults.mix);
    whole (p.stereoMode, 0, (int) StereoMode::numModes - 1);
    real  (p.pingPong, 0.0f, 1.0f, 0.0f);
    real  (p.width, 0.0f, 2.0f, defaults.width);
    real  (p.offset, -1.0f, 1.0f, 0.0f);
    real  (p.spread, 0.0f, 1.0f, 0.0f);

    real  (p.saturation, 0.0f, 1.0f, 0.0f);
    real  (p.driveDb, 0.0f, 24.0f, 0.0f);
    whole (p.satType, 0, (int) SatType::numTypes - 1);
    real  (p.lowCutHz, 20.0f, 20000.0f, defaults.lowCutHz);
    real  (p.highCutHz, 20.0f, 22000.0f, defaults.highCutHz);
    real  (p.resonance, 0.0f, 1.0f, 0.0f);
    real  (p.diffusion, 0.0f, 1.0f, 0.0f);
    real  (p.modDepth, 0.0f, 1.0f, 0.0f);
    real  (p.modRateHz, 0.0f, 100.0f, defaults.modRateHz);
    real  (p.pitchSt, -24.0f, 24.0f, 0.0f);
    real  (p.reverse, 0.0f, 1.0f, 0.0f);
    real  (p.ducking, 0.0f, 1.0f, 0.0f);
    real  (p.algoA, 0.0f, 1.0f, defaults.algoA);
    real  (p.algoB, 0.0f, 1.0f, defaults.algoB);

    real  (p.warp, -1.0f, 1.0f, 0.0f);
    real  (p.inertia, 0.0f, 1.0f, defaults.inertia);
    real  (p.orbit, 0.0f, 1.0f, 0.0f);
    real  (p.orbitRateHz, 0.0f, 100.0f, defaults.orbitRateHz);
    whole (p.orbitShape, 0, (int) OrbitShape::numShapes - 1);
    whole (p.orbitDiv, 0, kNumDivisions - 1);
    real  (p.ghost, 0.0f, 1.0f, 0.0f);
    real  (p.collapse, -1.0f, 1.0f, 0.0f);
    real  (p.random, 0.0f, 1.0f, 0.0f);
    real  (p.tapeMemory, 0.0f, 1.0f, 0.0f);

    real  (p.outGainDb, -100.0f, 24.0f, 0.0f);
    whole (p.quality, 0, 2);

    for (auto& lane : p.lanes)
    {
        real  (lane.depth, -1.0f, 1.0f, 0.0f);
        whole (lane.shape, 0, (int) LfoShape::numShapes - 1);
        real  (lane.rateHz, 0.0f, 100.0f, 0.5f);
        whole (lane.division, 0, kNumDivisions - 1);
        real  (lane.phase, -8.0f, 8.0f, 0.0f);
        real  (lane.stereo, -8.0f, 8.0f, 0.0f);
    }
}

/** Lock-free telemetry from the audio thread to the editor.
    Everything is a relaxed atomic: single writer (audio), any number of readers (UI). */
struct VisualData
{
    static constexpr int kRingSize = 8192;
    static constexpr int kMaxHeads = 48;

    enum HeadKind { headMain = 0, headTap, headCollapse, headGhost, headReverse, headGrain, headSlice };

    struct Head
    {
        std::atomic<float> delayMs { 0.0f };
        std::atomic<float> pan     { 0.0f };    // -1 .. 1
        std::atomic<float> gain    { 0.0f };
        std::atomic<int>   kind    { 0 };
    };

    // Peak envelope of everything written onto the "tape" (input + feedback), newest last.
    std::array<std::atomic<float>, (size_t) kRingSize> ringL, ringR, ringIn;
    std::atomic<uint32_t> ringWrite    { 0 };
    std::atomic<uint32_t> ringErasedAt { 0 };       // value of ringWrite when the tape was last erased:
                                                    // anything older than that is no longer on the tape
    std::atomic<float>    ringHopMs    { 1.333f };

    std::array<Head, (size_t) kMaxHeads> heads;
    std::atomic<int> numHeads { 0 };

    std::atomic<int>   mode        { 0 };
    std::atomic<float> bpm         { 120.0f };
    std::atomic<float> baseTimeMs  { 375.0f };
    std::atomic<float> timeMsL     { 375.0f }, timeMsR { 375.0f };
    std::atomic<float> chainMsL    { 0.0f }, chainMsR { 0.0f };      // delay spent inside the write-side chain
    std::atomic<float> feedback    { 0.0f };
    std::atomic<float> pitchSt     { 0.0f };
    std::atomic<float> lpHz        { 20000.0f }, hpHz { 20.0f };
    std::atomic<float> orbitX      { 0.0f }, orbitY { 0.0f };
    std::atomic<float> panL        { 0.0f }, panR { 0.0f };
    std::atomic<float> freeze      { 0.0f };
    std::atomic<float> duckGain    { 1.0f };
    std::atomic<float> limiterGr   { 1.0f };
    std::atomic<float> loopLevel   { 0.0f };

    std::atomic<float> inPeakL  { 0.0f }, inPeakR  { 0.0f };
    std::atomic<float> wetPeakL { 0.0f }, wetPeakR { 0.0f };
    std::atomic<float> outPeakL { 0.0f }, outPeakR { 0.0f };

    VisualData()
    {
        for (auto& v : ringL)  v.store (0.0f, std::memory_order_relaxed);
        for (auto& v : ringR)  v.store (0.0f, std::memory_order_relaxed);
        for (auto& v : ringIn) v.store (0.0f, std::memory_order_relaxed);
    }
};

} // namespace dlay
