#pragma once

#include "EngineTypes.h"
#include "DelayLine.h"
#include "Filters.h"
#include "Oversampler.h"
#include "Saturation.h"
#include "Diffuser.h"
#include "PitchShifter.h"
#include "SpectralProcessor.h"

namespace dlay
{

/** The DLAY audio engine.

    One stereo "tape" (a pair of circular buffers) is shared by all 21 algorithms. Sound is
    processed on the way *onto* the tape (diffusion -> pitch -> spectral -> filters ->
    character -> saturation -> safety limiter), and each algorithm is a different way of
    reading it back: single taps, tap patterns, reverse heads, grain clouds, slicers...

    Because the character chain is on the write side, every reader - including the GHOST,
    TIME COLLAPSE and REVERSE layers - hears the same coloured tape, and FREEZE is simply a
    lossless integer-sample recirculation of what is already there.

    Real-time rules: no allocation, no locks, no system calls after prepare().
*/
class DlayEngine
{
public:
    static constexpr int    kTick             = 16;       // control-rate interval in samples
    static constexpr double kMaxDelaySeconds  = 10.0;
    static constexpr int    kMaxTaps          = 8;
    static constexpr int    kMaxGrains        = 24;

    DlayEngine();

    void prepare (double sampleRate);

    /** Hard reset. Call only when the audio callback is not running. */
    void reset() noexcept;

    /** Thread-safe: fades the wet signal out, clears all audio state, fades back in.
        Used when a preset is loaded so the old tape does not chirp into the new settings. */
    void requestSoftReset() noexcept              { softResetFlag.store (true, std::memory_order_release); }

    /** Thread-safe: erases all audio state at the start of the next block, without a fade.
        For a host "reset" (processing was stopped) and for waking up from bypass: the old
        tail must not be heard at all, and whatever arrives next is echoed from its first
        sample. The levels of the dry signal, the output and the bypass crossfade are not
        part of that state - they keep gliding, so nothing steps. */
    void requestHardReset() noexcept              { hardResetFlag.store (true, std::memory_order_release); }

    /** In-place stereo processing. */
    void process (float* left, float* right, int numSamples,
                  const EngineParams& params, const TransportInfo& transport) noexcept;

    /** True once the bypass crossfade has fully reached the dry signal. */
    bool isBypassIdle() const noexcept            { return bypassRamp.v >= 0.9999; }

    double getSampleRate() const noexcept         { return sr; }

    MotionEngine motion;
    VisualData   visual;

private:
    // ------------------------------------------------------------------ types
    struct Tap
    {
        double mult = 1.0, multTarget = 1.0;
        double g = 0.0, gTarget = 0.0;                      // fade-switch gain
        double gl = 0.0, gr = 0.0, fb = 0.0;                // smoothed output / feedback gains
        double glT = 0.0, grT = 0.0, fbT = 0.0;
        double pan = 0.0;                                   // for the visualiser only
        bool   active = false;
    };

    struct Grain
    {
        bool   active = false, useSinc = false;
        int    age = 0, len = 1, cut = 0;
        double invLen = 1.0, startDelay = 0.0, slope = 0.0, gainL = 0.0, gainR = 0.0, pan = 0.0;
    };

    struct ReverseHead
    {
        double pos = 0.0, len = 4800.0;
    };

    enum class SliceAction { normal = 0, repeat, ratchet, reverse, half, twice, tapeStop, gate, mute };
    enum class ColorKind   { none = 0, digital, tape, bbd, warm, degraded, loFi };
    enum class FadeState   { steady = 0, fadingOut, fadingIn };

    // ---------------------------------------------------------------- methods
    void controlTick (const EngineParams& p, const TransportInfo& t, int blockOffset) noexcept;
    void updateTaps (const EngineParams& p, double fbBase) noexcept;
    void updateCollapse() noexcept;
    void updateOrbitAndPan (const EngineParams& p, double bpm) noexcept;
    void publishVisuals (const EngineParams& p, double bpm, double baseMs) noexcept;

    void setTap (int index, double mult, double gl, double gr, double fbWeight, double pan = 0.0) noexcept;
    void clearTapsFrom (int index) noexcept;
    void resetModeState() noexcept;
    void clearAudioState() noexcept;

    void spawnGrain() noexcept;
    void startNextSlice() noexcept;

    inline double chain (int ch, double w) noexcept;
    inline double colour (int ch, double w) noexcept;

    /** Tick-rate smoother for everything inside the loop and on the wet side. Jumps to its
        target on the first tick after any erase. A target that is not a number is ignored. */
    double sm (int index, double target) noexcept
    {
        if (isSane (target))
            macro[(size_t) index] += macroCoef * (target - macro[(size_t) index]);

        return macro[(size_t) index];
    }

    /** The same for what the listener hears directly (dry level, output gain, bypass):
        these only ever jump when the engine is prepared, never because the tape was erased. */
    double smOut (int index, double target) noexcept
    {
        if (isSane (target))
            macro[(size_t) index] += outCoef * (target - macro[(size_t) index]);

        return macro[(size_t) index];
    }

    // ------------------------------------------------------------------ state
    double sr = 48000.0;
    int    tickPos = 0;
    bool   prepared = false;

    DelayLine lines[2];
    double maxDelay = 1000.0;

    // mode / fades
    int       mode = 0;
    FadeState fadeState = FadeState::steady;
    double    fadeLevel = 1.0;
    Ramp      fadeRamp;
    bool      snapNext = true;              // next tick: loop state jumps to its targets
    bool      snapOutput = true;            // next tick: the output stage does too (after prepare only)
    std::atomic<bool> softResetFlag { false }, hardResetFlag { false };
    bool      pendingReset = false;
    bool      healFlag = false;             // something produced a non-number: start over

    // tick-rate macro smoothing
    enum
    {
        mAlgoA = 0, mAlgoB, mSpread, mGhost, mCollapse, mRandom, mTape, mReverse, mDiffusion, mSat,
        mDrive, mWidth, mOffset, mWarp, mPingPong, mOrbit, mDucking, mRes, mModDepth, mFreeze,
        mPitchMix, mBypass, mMix, mOutGain, mCount
    };
    std::array<double, (size_t) mCount> macro {};
    double macroCoef = 1.0, outCoef = 1.0;

    // time
    double timeTarget[2] { 4800.0, 4800.0 };
    double timeS1[2] { 4800.0, 4800.0 }, timeS2[2] { 4800.0, 4800.0 };
    double inertiaCoef = 1.0;
    Ramp   fastMod[2];
    double prevD[2] { 4800.0, 4800.0 };
    double loopSamples[2] { 4800.0, 4800.0 };       // full loop period incl. chain latency
    double chainLatency[2] { 0.0, 0.0 };
    int    cutIdx = 0;
    double minMainDelay = 9.0;

    // modulation sources
    double modPhase = 0.0, wowPhase = 0.0, flutterPhase = 0.0;
    double wowNoise = 0.0, flutterNoise = 0.0;
    double driftValue[2] { 0.0, 0.0 }, driftTarget[2] { 0.0, 0.0 };
    int    driftCountdown = 0;
    double evolvePhase = 0.0;

    // RANDOM UNIVERSE
    double rndTime[2] { 0.0, 0.0 }, rndPan = 0.0, rndFb = 0.0, rndCut = 0.0;
    double rndCounter = 0.0;

    // feedback
    Ramp   fbRamp[2], ppRamp, freezeRamp;
    int    freezeDelay[2] { 4800, 4800 };
    bool   freezeLatched = false;
    double loopEnv = 0.0, loopEnvCoef = 0.0, agcGain = 1.0, agcInt = 0.0;

    // filters
    SvfCoeffs lpC[2], hpC[2];
    SvfState  lpS[2], hpS[2];
    double    lpLog[2] { 9.9, 9.9 }, hpLog[2] { 3.0, 3.0 };
    bool      lpOn[2] { false, false }, hpOn[2] { false, false };
    double    resNorm[2] { 1.0, 1.0 };
    OnePoleFilter dcBlock[2];
    bool      dcOn = false;

    // saturation
    SatParams   loopSat, modeSat, inSat;
    Oversampler osLoop[2], osIn[2];
    int         osFactor = 2;
    double      osLatency[3] { 0.0, 0.0, 0.0 };
    double      driveGain = 1.0, driveComp = 1.0;
    Ramp        wetMakeup;

    // diffusion
    Diffuser<4> diff[2];
    Diffuser<6> bigDiff[2];
    Ramp        diffG, bigG;
    bool        bigDiffOn = false;
    double      bigModPhase = 0.0;

    // pitch / spectral
    PitchShifter ps[2];
    Ramp         pitchMix, pitchDry, pitchWet;
    bool         pitchOn = false;
    SvfCoeffs    shimmerHpC;                // keeps the shifted path out of the sub-bass
    SvfState     shimmerHp[2];
    bool         shimmerOn = false;
    SpectralProcessor spec[2];
    bool         spectralOn = false;

    // character
    ColorKind     colorKind = ColorKind::none;
    OnePoleFilter colorLp[2], colorLp2[2], colorHp[2], tiltLp[2], memLp[2];
    Biquad        bump;
    BiquadState   bumpS[2];
    bool          bumpOn = false, memLpOn = false, softOn = false;
    SvfCoeffs     bbdC;
    SvfState      bbdPre[2], bbdPost[2];
    double        shInc = 1.0, shPhase[2] { 0.0, 0.0 }, shHold[2] { 0.0, 0.0 }, shMix = 0.0;
    double        crushLevels = 0.0;
    double        tiltAmount = 0.0;

    // wear: dropouts, noise, crackle
    bool   wearOn = false;
    double noiseAmt = 0.0, crackleProb = 0.0, crackleAmt = 0.0;
    double noiseEnv[2] { 0.0, 0.0 }, noiseEnvRel = 0.999;
    double dropTarget = 1.0, dropSmooth = 1.0;
    int    dropCountdown = 2000, dropHold = 0;
    Ramp   dropRamp;
    Rng    rng, rngCtl;

    // taps
    Tap    taps[kMaxTaps];
    Tap    ctaps[kMaxTaps];             // TIME COLLAPSE
    int    numCollapse = 0;
    double tapCoef = 1.0, multCoef = 1.0;
    bool   monoLoop = false;
    double randomModeCounter = 0.0;
    double randomMult[kMaxTaps] {}, randomPan[kMaxTaps] {}, randomGain[kMaxTaps] {};

    // reverse
    ReverseHead rev[2];
    Ramp   revAmtR, revFbR;
    double revFade = 0.1, revHead2 = 0.0, revSwell = 0.0;
    double revChunk = 4800.0;

    // granular
    Grain  grains[kMaxGrains];
    int    grainCountdown = 0;
    double grainLen = 2400.0, grainSpray = 0.3, grainRatio = 1.0, grainRevProb = 0.0;
    double grainSpread = 0.0, grainOctProb = 0.0, grainNorm = 0.6;
    std::array<float, 513> hann {};

    // glitch
    SliceAction sliceAction = SliceAction::normal;
    double slicePos = 0.0, sliceLen = 4800.0, sliceOffset = 0.0, slicePanL = 1.0, slicePanR = 1.0;
    int    sliceIndex = 0, sliceRepeats = 0, sliceSubs = 1, sliceCut = 0;
    int    sliceDivs = 4;
    double sliceChaos = 0.0, sliceFade = 96.0;

    // ghost
    double        ghostPhase[2] { 0.0, 0.37 };
    Ramp          ghostMod[2];
    bool          ghostRunning = false;
    SvfCoeffs     ghostC[2];
    SvfState      ghostS[2];
    Diffuser<2>   ghostDiff[2];
    double        ghostFast[2] { 0.0, 0.0 }, ghostSlow[2] { 0.0, 0.0 };
    double        ghostPanL[2] { 0.7, 0.3 }, ghostPanR[2] { 0.3, 0.7 };
    double        ghostFastA = 0.0, ghostFastR = 0.0, ghostSlowA = 0.0, ghostSlowR = 0.0;
    Ramp          ghostGain;

    // slapback
    OnePoleFilter snapHp[2];
    double        snapAmt = 0.0;

    // orbit / pan / depth
    double orbitPhase = 0.0;
    int64_t orbitCycle = 0;
    double orbitXs = 0.0, orbitYs = 0.0, orbitSmooth = 1.0;
    Ramp   panMidL, panMidR, panSide, depthGain;
    OnePoleFilter depthLp[2];
    bool   depthOn = false;
    OnePoleFilter subsonic[2];
    bool   subsonicOn = false;

    // output
    Ramp   dryG[2], wetG[2], widthRamp, outGain, bypassRamp, duckRamp;
    double duckEnv = 0.0, duckAtk = 1.0, duckRel = 1.0;
    double limGain = 1.0, limRel = 0.0;
    bool   limiterOn = true;
    int    stereoModeCur = 0;

    // visual ring
    double ringPkL = 0.0, ringPkR = 0.0, ringPkIn = 0.0;
    int    ringHop = 64, ringCount = 0;
    uint32_t ringErased = 0;                // ring index at the last erase (mirrored to the editor)
    int    visualDivider = 0;
    double pitchForUi = 0.0;
};

} // namespace dlay
