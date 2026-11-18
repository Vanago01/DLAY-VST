// DLAY host-side validator.
//
// Loads the built DLAY.vst3 through JUCE's VST3 hosting code - the same path a DAW takes -
// and checks what a host relies on: scanning, bus layouts, parameters, processing at
// several sample rates and block sizes, state save / restore through the VST3 state
// stream, the bypass parameter, and opening the editor while audio runs on another thread.
//
//   DLAY_HostCheck /path/to/DLAY.vst3

#include <juce_audio_utils/juce_audio_utils.h>
#include <atomic>
#include <cstdio>
#include <thread>

#if JUCE_MAC
 // A command-line tool has no application object on macOS, and without one no window can
 // be opened. JUCE creates it in its own main(); this tool has to ask for it.
 namespace juce { extern void initialiseNSApplication(); }
#endif

namespace
{
    int failures = 0, checks = 0;

    void check (bool ok, const juce::String& what)
    {
        ++checks;
        if (! ok) { ++failures; std::printf ("  FAIL  %s\n", what.toRawUTF8()); }
    }

    void fillTestSignal (juce::AudioBuffer<float>& buffer, int64_t position, double sampleRate)
    {
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const double t = (double) (position + i) / sampleRate;
            const double beat = std::fmod (t, 0.5);
            const float v = t < 2.0 ? (float) (0.5 * std::exp (-beat * 22.0) * std::sin (6.2831853 * 330.0 * t)) : 0.0f;

            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.setSample (ch, i, v);
        }
    }

    struct Stats { bool finite = true; float peak = 0.0f; double tailEnergy = 0.0; };

    Stats run (juce::AudioPluginInstance& plugin, double sampleRate, int blockSize, double seconds)
    {
        plugin.setRateAndBufferSizeDetails (sampleRate, blockSize);
        plugin.prepareToPlay (sampleRate, blockSize);

        const int channels = juce::jmax (plugin.getTotalNumInputChannels(), plugin.getTotalNumOutputChannels());
        juce::AudioBuffer<float> buffer (channels, blockSize);
        juce::MidiBuffer midi;
        Stats stats;

        const auto total = (int64_t) (seconds * sampleRate);
        for (int64_t pos = 0; pos < total; pos += blockSize)
        {
            fillTestSignal (buffer, pos, sampleRate);
            plugin.processBlock (buffer, midi);

            for (int ch = 0; ch < plugin.getTotalNumOutputChannels(); ++ch)
            {
                for (int i = 0; i < blockSize; ++i)
                {
                    const float v = buffer.getSample (ch, i);
                    stats.finite = stats.finite && std::isfinite (v);
                    stats.peak = juce::jmax (stats.peak, std::abs (v));
                    if ((double) pos / sampleRate > 2.2)
                        stats.tailEnergy += (double) v * v;
                }
            }
        }

        plugin.releaseResources();
        return stats;
    }

    /** Opens the editor on the message thread while a second thread keeps processing and
        the message thread keeps moving parameters - the situation in every DAW. */
    struct EditorSession : private juce::Timer
    {
        EditorSession (juce::AudioPluginInstance& p) : plugin (p)
        {
            plugin.setRateAndBufferSizeDetails (48000.0, 256);
            plugin.prepareToPlay (48000.0, 256);

            audio = std::thread ([this]
            {
                juce::AudioBuffer<float> buffer (2, 256);
                juce::MidiBuffer midi;
                int64_t pos = 0;

                while (! stop.load())
                {
                    fillTestSignal (buffer, pos % 96000, 48000.0);
                    plugin.processBlock (buffer, midi);
                    pos += 256;

                    for (int i = 0; i < 256; ++i)
                        if (! std::isfinite (buffer.getSample (0, i)) || ! std::isfinite (buffer.getSample (1, i)))
                            finite = false;

                    std::this_thread::sleep_for (std::chrono::microseconds (1500));
                }
            });

            startTimerHz (40);
        }

        ~EditorSession() override
        {
            stopTimer();
            stop = true;
            if (audio.joinable()) audio.join();
            editor.reset();
            plugin.releaseResources();
        }

        std::atomic<bool> finite { true };
        bool editorOpened = false, finished = false;
        int editorWidth = 0, editorHeight = 0;

    private:
        void timerCallback() override
        {
            ++tick;

            if (tick == 4)
            {
                editor.reset (plugin.createEditorAndMakeActive());
                if (editor != nullptr)
                {
                    editor->setOpaque (true);
                    editor->addToDesktop (0);
                    editor->setVisible (true);
                    editorOpened = true;
                    editorWidth = editor->getWidth();
                    editorHeight = editor->getHeight();
                }
            }

            // keep twisting knobs from the message thread
            const auto& params = plugin.getParameters();
            for (int k = 0; k < 6; ++k)
            {
                auto* p = params[random.nextInt (params.size())];
                p->setValueNotifyingHost (random.nextFloat());
            }

            if (tick == 70)                     // close and reopen the editor once
                editor.reset();
            if (tick == 76)
            {
                editor.reset (plugin.createEditorAndMakeActive());
                if (editor != nullptr) { editor->addToDesktop (0); editor->setVisible (true); }
            }

            if (tick >= 130)
            {
                stopTimer();
                finished = true;
            }
        }

        juce::AudioPluginInstance& plugin;
        std::unique_ptr<juce::AudioProcessorEditor> editor;
        std::thread audio;
        std::atomic<bool> stop { false };
        juce::Random random { 42 };
        int tick = 0;
    };
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

   #if JUCE_MAC
    juce::initialiseNSApplication();
   #endif

    if (argc < 2)
    {
        std::printf ("usage: DLAY_HostCheck /path/to/DLAY.vst3\n");
        return 2;
    }

    const juce::String path (juce::File::getCurrentWorkingDirectory().getChildFile (juce::String::fromUTF8 (argv[1])).getFullPathName());
    std::printf ("Hosting %s\n", path.toRawUTF8());

    juce::AudioPluginFormatManager formats;
    juce::addDefaultFormatsToManager (formats);

    // ---- scan --------------------------------------------------------------------
    juce::OwnedArray<juce::PluginDescription> found;
    for (auto* format : formats.getFormats())
        format->findAllTypesForFile (found, path);

    check (found.size() == 1, "the bundle exposes exactly one plugin (found " + juce::String (found.size()) + ")");
    if (found.isEmpty())
        return 1;

    const auto description = *found[0];
    std::printf ("  name '%s'  vendor '%s'  category '%s'  format %s\n",
                 description.name.toRawUTF8(), description.manufacturerName.toRawUTF8(), description.category.toRawUTF8(),
                 description.pluginFormatName.toRawUTF8());

    check (description.name == "DLAY", "plugin name is DLAY");
    check (description.manufacturerName == "DLAY Audio", "vendor is DLAY Audio");
    check (! description.isInstrument, "registered as an effect");
    check (description.category.containsIgnoreCase ("Delay"), "categorised as a delay");

    juce::String error;
    auto plugin = formats.createPluginInstance (description, 48000.0, 512, error);
    check (plugin != nullptr, "instantiates (" + error + ")");
    if (plugin == nullptr)
        return 1;

    // ---- parameters ----------------------------------------------------------------
    const auto& params = plugin->getParameters();
    std::printf ("  %d parameters, latency %d samples, tail %.1f s\n", params.size(), plugin->getLatencySamples(), plugin->getTailLengthSeconds());

    check (plugin->getTotalNumInputChannels() == 2 && plugin->getTotalNumOutputChannels() == 2, "stereo in / stereo out by default");
    check (params.size() >= 84, "all parameters are exposed to the host");
    check (plugin->getLatencySamples() == 0, "reports zero latency");

    {
        bool named = true, automatable = true;
        for (auto* p : params)
        {
            named = named && p->getName (64).isNotEmpty();
            automatable = automatable && p->isAutomatable();
        }
        check (named, "every parameter has a name");
        check (automatable, "every parameter is automatable");
        check (plugin->getBypassParameter() != nullptr, "exposes a bypass parameter to the host");
    }

    // ---- processing ----------------------------------------------------------------
    static const std::pair<double, int> configurations[] = { { 44100.0, 64 }, { 48000.0, 512 }, { 88200.0, 333 }, { 96000.0, 1024 }, { 192000.0, 2048 } };

    for (const auto& cfg : configurations)
    {
        const auto stats = run (*plugin, cfg.first, cfg.second, 4.0);
        std::printf ("  %6.0f Hz / %4d : peak %.3f  tail energy %.4f\n", cfg.first, cfg.second, stats.peak, stats.tailEnergy);
        check (stats.finite, "finite output at " + juce::String (cfg.first) + " / " + juce::String (cfg.second));
        check (stats.peak <= 1.0f, "no clipping at " + juce::String (cfg.first));
        check (stats.tailEnergy > 1.0e-5, "echo tail present at " + juce::String (cfg.first));
    }

    // every parameter at random, repeatedly, while processing
    {
        juce::Random random (7);
        bool finite = true;
        float peak = 0.0f;

        for (int round = 0; round < 40; ++round)
        {
            for (auto* p : params)
                if (p != plugin->getBypassParameter())
                    p->setValueNotifyingHost (random.nextFloat());

            const auto stats = run (*plugin, 48000.0, 256, 0.6);
            finite = finite && stats.finite;
            peak = juce::jmax (peak, stats.peak);
        }

        std::printf ("  40 random parameter sets: peak %.3f\n", peak);
        check (finite, "finite output for 40 random parameter sets");
        check (peak <= 4.0f, "bounded output for 40 random parameter sets (the limiter may be switched off by the fuzzing)");
    }

    // ---- state through the VST3 stream ---------------------------------------------------
    {
        // A host may hand the plug-in any normalised value; the plug-in snaps it to its own
        // legal steps. So: A (random values) -> state -> B, then B -> state -> C. B and C must
        // agree exactly, and B must be close to what was asked for.
        juce::Random random (11);
        for (auto* p : params)
            p->setValueNotifyingHost (random.nextFloat());

        juce::MessageManager::getInstance()->runDispatchLoopUntil (150);

        juce::Array<float> asked;
        for (auto* p : params)
            asked.add (p->getValue());

        juce::MemoryBlock stateA;
        plugin->getStateInformation (stateA);
        check (stateA.getSize() > 64, "state blob is produced (" + juce::String ((int) stateA.getSize()) + " bytes)");

        auto b = formats.createPluginInstance (description, 48000.0, 512, error);
        auto c = formats.createPluginInstance (description, 48000.0, 512, error);
        check (b != nullptr && c != nullptr, "further instances");

        if (b != nullptr && c != nullptr)
        {
            b->setStateInformation (stateA.getData(), (int) stateA.getSize());
            juce::MessageManager::getInstance()->runDispatchLoopUntil (150);

            juce::MemoryBlock stateB;
            b->getStateInformation (stateB);
            c->setStateInformation (stateB.getData(), (int) stateB.getSize());
            juce::MessageManager::getInstance()->runDispatchLoopUntil (150);

            int mismatches = 0, farOff = 0;
            const auto& pb = b->getParameters();
            const auto& pc = c->getParameters();

            for (int i = 0; i < juce::jmin (pb.size(), pc.size()); ++i)
            {
                if (std::abs (pb[i]->getValue() - pc[i]->getValue()) > 1.0e-5f)
                {
                    ++mismatches;
                    std::printf ("    %s: %f vs %f\n", pb[i]->getName (40).toRawUTF8(), (double) pb[i]->getValue(), (double) pc[i]->getValue());
                }

                // continuous parameters come back within a step; switches and lists snap to a position
                if (pb[i]->getNumSteps() > 100 && std::abs (pb[i]->getValue() - asked[i]) > 0.02f)
                    ++farOff;
            }

            check (mismatches == 0, "state survives save -> restore -> save -> restore exactly");
            check (farOff == 0, "restored values match what the host set");

            const auto stats = run (*c, 48000.0, 512, 2.0);
            check (stats.finite, "restored instance processes cleanly");
        }
    }

    // ---- editor + concurrent audio -----------------------------------------------------------
    {
        auto third = formats.createPluginInstance (description, 48000.0, 256, error);
        check (third != nullptr, "third instance");

        if (third != nullptr)
        {
            check (third->hasEditor(), "has an editor");

            {
                EditorSession session (*third);

                // Pumped in slices rather than with runDispatchLoop(): that one needs a running
                // application, which a command-line tool is not on every system.
                const auto deadline = juce::Time::getMillisecondCounter() + 60000u;
                while (! session.finished && juce::Time::getMillisecondCounter() < deadline)
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);

                check (session.finished, "the editor session ran to its end");

                std::printf ("  editor %d x %d\n", session.editorWidth, session.editorHeight);
                check (session.editorOpened, "editor opens");
                check (session.editorWidth >= 600 && session.editorHeight >= 300, "editor has a sensible size");
                check (session.finite.load(), "audio thread stays healthy while the editor is open and parameters move");
            }
        }
    }

    plugin.reset();
    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
