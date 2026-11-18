#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "DSP/DlayEngine.h"
#include "Params/Parameters.h"
#include "Motion/MotionCurves.h"
#include "Presets/PresetManager.h"

/** DLAY — Bend Time. Shape Space. Create Echoes. */
class DlayAudioProcessor : public juce::AudioProcessor,
                           private juce::AsyncUpdater
{
public:
    DlayAudioProcessor();
    ~DlayAudioProcessor() override;

    // ---- audio --------------------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    juce::AudioProcessorParameter* getBypassParameter() const override   { return bypassParameter; }

    // ---- editor -------------------------------------------------------------
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                                      { return true; }

    // ---- info ---------------------------------------------------------------
    const juce::String getName() const override                          { return "DLAY"; }
    bool acceptsMidi() const override                                    { return false; }
    bool producesMidi() const override                                   { return false; }
    bool isMidiEffect() const override                                   { return false; }
    double getTailLengthSeconds() const override                         { return 10.0; }

    int getNumPrograms() override                                        { return 1; }
    int getCurrentProgram() override                                     { return 0; }
    void setCurrentProgram (int) override                                {}
    const juce::String getProgramName (int) override                     { return "Default"; }
    void changeProgramName (int, const juce::String&) override           {}

    // ---- state --------------------------------------------------------------
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    /** Message thread only. Applies, right now, a session state that a host handed over on
        another thread and that is still waiting for the message loop. The loop does this by
        itself; this is for code that cannot wait for it. */
    void applyPendingState()                                             { handleUpdateNowIfNeeded(); }

    // ---- shared with the editor ---------------------------------------------
    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState apvts;
    dlay::DlayEngine engine;
    std::unique_ptr<dlay::MotionCurves> curves;
    std::unique_ptr<dlay::PresetManager> presets;

    /** Editor settings that travel with the session. */
    bool   getUiAdvanced() const        { return (bool) apvts.state.getProperty ("uiAdvanced", true); }
    void   setUiAdvanced (bool b)       { apvts.state.setProperty ("uiAdvanced", b, nullptr); }
    // (Stored as "uiZoom": the "uiScale" of sessions saved with the first interface belonged
    // to a larger layout and would open this one too small. 0 = never chosen.)
    double getUiScale() const           { return (double) apvts.state.getProperty ("uiZoom", 0.0); }
    void   setUiScale (double s)        { apvts.state.setProperty ("uiZoom", s, nullptr); }
    int    getUiPage() const            { return (int) apvts.state.getProperty ("uiPage", 0); }
    void   setUiPage (int p)            { apvts.state.setProperty ("uiPage", p, nullptr); }
    int    getUiLane() const            { return (int) apvts.state.getProperty ("uiLane", 0); }
    void   setUiLane (int l)            { apvts.state.setProperty ("uiLane", l, nullptr); }

private:
    /** Makes a restored session state the current one. Message thread only (or the
        calling thread, in a process that has no message thread). */
    void applyState (const juce::ValueTree& tree);
    void handleAsyncUpdate() override;

    dlay::ParamCache cache;
    juce::AudioProcessorParameter* bypassParameter = nullptr;

    std::vector<float> scratchL, scratchR;
    int  scratchSize = 0;
    bool sleeping = false;

    // A state handed over by the host on another thread, waiting for the message thread.
    juce::CriticalSection pendingStateLock;
    juce::ValueTree pendingState;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DlayAudioProcessor)
};
