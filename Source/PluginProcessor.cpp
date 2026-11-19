#include "PluginProcessor.h"
#include "PluginEditor.h"

DlayAudioProcessor::DlayAudioProcessor()
    : AudioProcessor (BusesProperties().withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undoManager, "DLAY", dlay::createParameterLayout())
{
    cache.attach (apvts);
    bypassParameter = apvts.getParameter (dlay::id::bypass);

    curves  = std::make_unique<dlay::MotionCurves> (apvts.state, &undoManager, engine.motion);
    presets = std::make_unique<dlay::PresetManager> (*this, apvts, *curves, undoManager);

    // a freshly loaded preset should not chirp through the previous preset's tape
    presets->onPresetApplied = [this] { engine.requestSoftReset(); };
    curves->onCurveChanged   = [this] (int) { presets->markModified(); };

    // sensible starting point for a new instance (bounds-checked: the library may be empty in tests)
    if (! presets->getEntries().empty())
        presets->load (0);

    undoManager.clearUndoHistory();
}

DlayAudioProcessor::~DlayAudioProcessor()
{
    cancelPendingUpdate();
    curves->onCurveChanged = nullptr;
    presets->onPresetApplied = nullptr;
}

// ============================================================================
void DlayAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate);

    scratchSize = juce::jmax (64, samplesPerBlock);
    scratchL.assign ((size_t) scratchSize, 0.0f);
    scratchR.assign ((size_t) scratchSize, 0.0f);
    sleeping = false;

    setLatencySamples (0);
}

void DlayAudioProcessor::releaseResources()
{
}

void DlayAudioProcessor::reset()
{
    // the host has stopped processing: nothing of the old tail may come back when it resumes
    engine.requestHardReset();
}

bool DlayAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    // mono -> mono, mono -> stereo, stereo -> stereo
    return in == out || (in == juce::AudioChannelSet::mono() && out == juce::AudioChannelSet::stereo());
}

// ============================================================================
void DlayAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn      = getTotalNumInputChannels();
    const int numOut     = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();

    if (numSamples <= 0 || numOut <= 0 || buffer.getNumChannels() <= 0 || scratchSize <= 0)
        return;

    const auto params = cache.snapshot();

    dlay::TransportInfo transport;

    if (auto* hostPlayHead = getPlayHead())
    {
        if (const auto position = hostPlayHead->getPosition())
        {
            if (const auto bpm = position->getBpm())            transport.bpm = *bpm;
            if (const auto ppq = position->getPpqPosition())    { transport.ppq = *ppq; transport.ppqValid = true; }
            transport.playing = position->getIsPlaying();
        }
    }

    // hosts have been seen to report a tempo of zero or a position that is not a number
    dlay::sanitise (transport);

    const bool stereoOut = numOut > 1 && buffer.getNumChannels() > 1;
    const bool stereoIn  = numIn > 1 && buffer.getNumChannels() > 1;

    // Fully bypassed: pass the dry signal through untouched and let the engine rest.
    if (params.bypass && engine.isBypassIdle())
    {
        if (numIn == 0)
            buffer.clear();
        else if (stereoOut && ! stereoIn)
            buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);

        for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
            buffer.clear (ch, 0, numSamples);

        sleeping = true;
        return;
    }

    if (sleeping)
    {
        // Waking up: echoes from before the bypass must not come back, and whatever changed
        // meanwhile applies at once. The bypass crossfade itself is untouched by this, so the
        // processed signal still fades in. A host reset() that arrived while asleep ends up
        // in the same request.
        engine.requestHardReset();
        sleeping = false;
    }

    const double ppqPerSample = transport.bpm / (60.0 * engine.getSampleRate());
    int offset = 0;

    while (offset < numSamples)
    {
        const int chunk = juce::jmin (numSamples - offset, scratchSize);

        if (numIn == 0)
        {
            juce::FloatVectorOperations::clear (scratchL.data(), chunk);
            juce::FloatVectorOperations::clear (scratchR.data(), chunk);
        }
        else
        {
            juce::FloatVectorOperations::copy (scratchL.data(), buffer.getReadPointer (0, offset), chunk);
            juce::FloatVectorOperations::copy (scratchR.data(), buffer.getReadPointer (stereoIn ? 1 : 0, offset), chunk);
        }

        engine.process (scratchL.data(), scratchR.data(), chunk, params, transport);
        transport.ppq += ppqPerSample * (double) chunk;

        if (stereoOut)
        {
            juce::FloatVectorOperations::copy (buffer.getWritePointer (0, offset), scratchL.data(), chunk);
            juce::FloatVectorOperations::copy (buffer.getWritePointer (1, offset), scratchR.data(), chunk);
        }
        else
        {
            auto* dst = buffer.getWritePointer (0, offset);
            for (int i = 0; i < chunk; ++i)
                dst[i] = 0.5f * (scratchL[(size_t) i] + scratchR[(size_t) i]);
        }

        offset += chunk;
    }

    for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);
}

// ============================================================================
juce::AudioProcessorEditor* DlayAudioProcessor::createEditor()
{
    return new DlayAudioProcessorEditor (*this);
}

// ============================================================================
namespace
{
    const juce::Identifier stateType { "DLAY" };        // the tag the parameter tree was created with
}

void DlayAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree state;

    {
        // a state the host has just handed over, but which the message thread has not got
        // round to yet, is already "the" state as far as the host is concerned
        const juce::ScopedLock lock (pendingStateLock);
        if (pendingState.isValid())
            state = pendingState.createCopy();
    }

    if (! state.isValid())
    {
        state = apvts.copyState();
        presets->saveToState (state);
    }

    state.setProperty ("pluginVersion", DLAY_VERSION_STRING, nullptr);

    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

/*  Hosts restore state from whichever thread suits them. Everything a restore touches -
    the parameter tree the editor reads, the MOTION curves, the preset name the top bar
    paints, the undo history - belongs to the message thread, so that is the only place
    where it is applied. Called on any other thread, this only parses the data and hands
    it over. */
void DlayAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // A session of DLAY is a few kilobytes of text behind the eight bytes copyXmlToBinary()
    // puts in front. What is far bigger, or is not text, is not one, whatever it says.
    if (data == nullptr || sizeInBytes <= 8 || sizeInBytes > dlay::foreign::kMaxBytes)
        return;

    const auto* bytes = static_cast<const char*> (data);
    const int statedLength = (int) juce::ByteOrder::littleEndianInt (bytes + 4);
    const int textLength   = juce::jmin (sizeInBytes - 8, statedLength);

    if (textLength <= 0 || ! juce::CharPointer_UTF8::isValidString (bytes + 8, textLength))
        return;

    const auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (stateType))
        return;

    auto tree = cleanedState (*xml);
    if (! tree.isValid())
        return;

    // No message thread at all (a bare command-line host): nothing can be reading over
    // there, and nothing would ever deliver the hand-over, so apply it here.
    const auto* messageManager = juce::MessageManager::getInstanceWithoutCreating();

    if (messageManager == nullptr || messageManager->isThisTheMessageThread())
    {
        {
            const juce::ScopedLock lock (pendingStateLock);
            pendingState = {};                          // superseded
        }

        cancelPendingUpdate();
        applyState (tree);
    }
    else
    {
        {
            const juce::ScopedLock lock (pendingStateLock);
            pendingState = std::move (tree);            // the latest one wins
        }

        triggerAsyncUpdate();
    }
}

void DlayAudioProcessor::handleAsyncUpdate()
{
    juce::ValueTree tree;

    {
        const juce::ScopedLock lock (pendingStateLock);
        std::swap (tree, pendingState);
    }

    if (tree.isValid())
        applyState (tree);
}

/*  The host got this state out of a project file, and a project file can be damaged, or
    come from someone else. Nothing of it is used as it is. What goes on:

      - the parameters DLAY has, once each. A value that is not a number is left out, and
        the parameter then takes its default. (The text "nan" parses as a float. It would
        pass every range check, sit in the parameter, and be written into the next save.)
      - the MOTION curves, as MotionCurves::cleaned() rebuilds them
      - the settings of the editor, inside their ranges
      - other small properties, a limited number: a newer DLAY may have written them

    Everything else is dropped here, so it is not carried into the next save either. */
juce::ValueTree DlayAudioProcessor::cleanedState (const juce::XmlElement& fromHost) const
{
    namespace foreign = dlay::foreign;

    if (! fromHost.hasTagName (stateType))
        return {};

    juce::ValueTree out (stateType);

    // ---- settings beside the parameters
    const auto number = [&fromHost] (const char* name, double lowest, double highest, double& result)
    {
        if (! fromHost.hasAttribute (name))
            return false;

        const auto text = fromHost.getStringAttribute (name).trim();
        const double v = text.getDoubleValue();

        if (text.isEmpty() || text.length() > 32 || ! std::isfinite (v) || v < lowest || v > highest)
            return false;

        result = v;
        return true;
    };

    double v = 0.0;
    if (number ("uiZoom", 0.0, 4.0, v))                             out.setProperty ("uiZoom", v, nullptr);
    if (number ("uiPage", 0.0, 15.0, v))                            out.setProperty ("uiPage", juce::roundToInt (v), nullptr);
    if (number ("uiLane", 0.0, (double) dlay::kNumLanes - 1.0, v))  out.setProperty ("uiLane", juce::roundToInt (v), nullptr);
    if (number ("uiAdvanced", 0.0, 1.0, v))                         out.setProperty ("uiAdvanced", v >= 0.5, nullptr);

    for (int i = 0; i < fromHost.getNumAttributes() && out.getNumProperties() < foreign::kMaxProperties; ++i)
    {
        const auto& name = fromHost.getAttributeName (i);

        if (name.startsWith ("ui") || name.length() > 64 || ! juce::XmlElement::isValidXmlName (name) || out.hasProperty (name))
            continue;                                           // (handled above, or left out there)

        const auto& text = fromHost.getAttributeValue (i);

        if (text.length() <= foreign::kMaxPropertyLength)
            out.setProperty (name, foreign::text (text, foreign::kMaxPropertyLength), nullptr);
    }

    // ---- parameters and curves (the names are the ones AudioProcessorValueTreeState uses)
    juce::StringArray seen;

    for (auto* child : fromHost.getChildIterator())
    {
        if (child->hasTagName ("PARAM"))
        {
            const auto paramId = child->getStringAttribute ("id");

            if (paramId.length() > 64 || apvts.getParameter (paramId) == nullptr || seen.contains (paramId))
                continue;

            seen.add (paramId);

            juce::ValueTree param ("PARAM");
            param.setProperty ("id", paramId, nullptr);

            const double value = child->getStringAttribute ("value").getDoubleValue();

            if (child->hasAttribute ("value") && foreign::usableFloat (value))
                param.setProperty ("value", value, nullptr);

            out.appendChild (param, nullptr);
        }
        else if (child->hasTagName (dlay::MotionCurves::motionType.toString())
                  && ! out.getChildWithName (dlay::MotionCurves::motionType).isValid())
        {
            out.appendChild (dlay::MotionCurves::cleaned (*child), nullptr);
        }
    }

    return out;
}

void DlayAudioProcessor::applyState (const juce::ValueTree& tree)
{
    apvts.replaceState (tree);                          // also clears the undo history

    // (cleanedState() lets no such value through; this is the second lock on the door)
    for (auto* param : getParameters())
        if (! std::isfinite (param->getValue()))
            param->setValueNotifyingHost (param->getDefaultValue());

    curves->stateReplaced();
    presets->restoreFromState (apvts.state);
    undoManager.clearUndoHistory();
    engine.requestSoftReset();
}

// ============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DlayAudioProcessor();
}
