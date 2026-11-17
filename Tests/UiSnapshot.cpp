// Renders the real DLAY editor to PNG files while audio is running through the plugin.
//
//   DLAY_UiSnapshot <output-dir> [--preset "Name"] [--scale 1.0] [--seconds 3.5] [--lanes] [--interact]
//
// Writes advanced.png, simple.png and browser.png, plus one image per MOTION lane with --lanes.
// With --interact it then drives the interface with simulated mouse gestures (dragging the
// tape and the orbit radar, drawing MOTION curves, typing a value, browsing presets, undo,
// resizing, every algorithm) and checks that each one does what it should. The exit code is
// the number of failed checks.

#include "TestSupport.h"
#include "PluginEditor.h"
#include "UI/PresetBrowser.h"

using namespace dlaytest;

namespace
{
    struct Runner : private juce::Timer
    {
        Runner (const juce::File& dir, const juce::String& presetName, float scale, double seconds, bool allLanes, bool interact)
            : outDir (dir), snapshotScale (scale), warmupFrames ((int) (seconds * 30.0)), lanes (allLanes), interactive (interact)
        {
            outDir.createDirectory();

            proc.setPlayHead (&playHead);
            proc.setRateAndBufferSizeDetails (sampleRate, blockSize);
            proc.prepareToPlay (sampleRate, blockSize);

            if (presetName.isNotEmpty())
            {
                const auto& entries = proc.presets->getEntries();
                for (int i = 0; i < (int) entries.size(); ++i)
                    if (entries[(size_t) i].name.equalsIgnoreCase (presetName))
                        proc.presets->load (i);
            }

            input = makeTestSignal (sampleRate, 60.0, 60.0, 3);

            editor.reset (dynamic_cast<DlayAudioProcessorEditor*> (proc.createEditorAndMakeActive()));
            jassert (editor != nullptr);
            editor->setVisible (true);

            startTimerHz (30);
        }

        ~Runner() override
        {
            stopTimer();
            if (editor != nullptr)
                proc.editorBeingDeleted (editor.get());
            editor.reset();
            proc.setPlayHead (nullptr);
        }

    private:
        void pumpAudio()
        {
            juce::AudioBuffer<float> block (2, blockSize);
            juce::MidiBuffer midi;

            for (int k = 0; k < 3; ++k)         // ~33 ms of audio per frame
            {
                for (int i = 0; i < blockSize; ++i)
                {
                    const auto idx = (size_t) ((position + i) % input.size());
                    block.setSample (0, i, input.l[idx]);
                    block.setSample (1, i, input.r[idx]);
                }

                proc.processBlock (block, midi);
                position += blockSize;
                playHead.ppq += (double) blockSize * playHead.bpm / (60.0 * sampleRate);
            }
        }

        void save (const juce::String& name)
        {
            const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, snapshotScale);
            const auto file = outDir.getChildFile (name + ".png");
            file.deleteFile();
            juce::FileOutputStream stream (file);
            juce::PNGImageFormat png;

            if (stream.openedOk() && png.writeImageToStream (image, stream))
                std::printf ("wrote %s (%d x %d)\n", file.getFullPathName().toRawUTF8(), image.getWidth(), image.getHeight());
        }

        static juce::Component* findById (juce::Component& root, const juce::String& componentId)
        {
            if (root.getComponentID() == componentId)
                return &root;

            for (auto* child : root.getChildren())
                if (auto* hit = findById (*child, componentId))
                    return hit;

            return nullptr;
        }

        /** How long the animated parts take to draw (they repaint 30 times a second). */
        void reportPaintCost()
        {
            const auto time = [] (juce::Component& c, int runs)
            {
                const auto start = juce::Time::getHighResolutionTicks();
                for (int i = 0; i < runs; ++i)
                    c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
                return 1000.0 * juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - start) / runs;
            };

            if (auto* v = findById (*editor, "visualizer"))  std::printf ("paint cost: visualizer   %.2f ms\n", time (*v, 30));
            if (auto* m = findById (*editor, "motion"))      std::printf ("paint cost: motion panel %.2f ms\n", time (*m, 30));
            std::printf ("paint cost: whole editor %.2f ms\n", time (*editor, 10));
        }

        // ---------------------------------------------------------------------------------
        //  scripted interaction
        // ---------------------------------------------------------------------------------
        void expect (bool ok, const juce::String& what)
        {
            std::printf ("  %s  %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8());
            if (! ok) ++failures;
        }

        float value (const char* paramId)            { return getParam (proc, paramId); }

        static juce::MouseEvent mouse (juce::Component& c, juce::Point<float> pos, juce::Point<float> downPos,
                                       bool dragged = false, int clicks = 1,
                                       juce::ModifierKeys mods = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier))
        {
            const auto now = juce::Time::getCurrentTime();
            return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos, mods, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f,
                                     &c, &c, now, downPos, now, clicks, dragged);
        }

        static void drag (juce::Component& c, juce::Point<float> from, juce::Point<float> to, int moves = 6)
        {
            c.mouseDown (mouse (c, from, from));
            for (int i = 1; i <= moves; ++i)
                c.mouseDrag (mouse (c, from + (to - from) * ((float) i / (float) moves), from, true));
            c.mouseUp (mouse (c, to, from, true));
        }

        template <typename Type>
        static Type* findChild (juce::Component& root, const std::function<bool (Type&)>& match)
        {
            if (auto* typed = dynamic_cast<Type*> (&root))
                if (match (*typed))
                    return typed;

            for (auto* child : root.getChildren())
                if (auto* hit = findChild<Type> (*child, match))
                    return hit;

            return nullptr;
        }

        void wait (int frames)
        {
            for (int i = 0; i < frames; ++i)
                steps.push_back ([] {});
        }

        void buildInteractionScript()
        {
            using namespace dlay;
            std::printf ("interaction script:\n");

            // ---- the tape: sideways = time, up = feedback -----------------------------------
            steps.push_back ([this]
            {
                proc.presets->loadInit();
                editor->setAdvancedView (true);
            });
            wait (8);
            steps.push_back ([this]
            {
                auto* vis = findById (*editor, "visualizer");
                expect (vis != nullptr, "visualizer found");
                if (vis == nullptr) return;

                const float before = value (id::feedback);
                drag (*vis, { 180.0f, 220.0f }, { 250.0f, 160.0f });
                expect (juce::roundToInt (value (id::division)) == (int) Div::n4 + 2, "dragging the tape right steps the note value (1/4 -> 1/4D)");
                expect (std::abs (value (id::feedback) - (before + 27.0f)) < 0.5f, "dragging the tape up raises the feedback");
            });
            wait (8);
            steps.push_back ([this]
            {
                expect (proc.undoManager.canUndo(), "the gesture is undoable");
                proc.undoManager.undo();
            });
            wait (6);
            steps.push_back ([this]
            {
                expect (juce::roundToInt (value (id::division)) == (int) Div::n4 && std::abs (value (id::feedback) - 45.0f) < 0.5f,
                        "undo restores time and feedback together");
                proc.undoManager.redo();
            });
            wait (6);
            steps.push_back ([this]
            {
                expect (juce::roundToInt (value (id::division)) == (int) Div::n4 + 2, "redo re-applies the gesture");

                // free-running time: the tape drag is continuous
                setParam (proc, id::sync, 0.0f);
                setParam (proc, id::time, 300.0f);
            });
            wait (4);
            steps.push_back ([this]
            {
                auto* vis = findById (*editor, "visualizer");
                drag (*vis, { 180.0f, 220.0f }, { 350.0f, 220.0f });
                expect (std::abs (value (id::time) - 600.0f) < 4.0f, "170 px to the right doubles a free-running time (300 -> 600 ms)");
            });

            // ---- the orbit radar ------------------------------------------------------------------
            steps.push_back ([this]
            {
                auto* vis = findById (*editor, "visualizer");
                const auto w = (float) vis->getWidth();

                // the radar is the right-hand third of the screen
                const juce::Point<float> inside (w - 120.0f, 200.0f);
                vis->mouseDown (mouse (*vis, inside, inside));
                vis->mouseDrag (mouse (*vis, inside + juce::Point<float> (40.0f, 0.0f), inside, true));
                vis->mouseUp (mouse (*vis, inside + juce::Point<float> (40.0f, 0.0f), inside, true));
                expect (value (id::orbit) > 5.0f, "dragging in the radar engages ECHO ORBIT (" + juce::String (value (id::orbit), 1) + " %)");

                const int shape = juce::roundToInt (value (id::orbitShape));
                vis->mouseDoubleClick (mouse (*vis, inside, inside, false, 2));
                expect (juce::roundToInt (value (id::orbitShape)) == (shape + 1) % 5, "double-clicking the radar steps to the next orbit shape");
            });

            // ---- MOTION: click-edit, freehand, reset, undo --------------------------------------------
            steps.push_back ([this] { editor->showMotionLane ((int) Lane::speed); });
            wait (3);
            steps.push_back ([this]
            {
                auto* curve = findById (*editor, "motionCurve");
                expect (curve != nullptr, "MOTION curve view found");
                if (curve == nullptr) return;

                const auto w = (float) curve->getWidth(), h = (float) curve->getHeight();
                drag (*curve, { w * 0.37f, h * 0.5f }, { w * 0.37f, h * 0.2f });

                expect (juce::roundToInt (value (id::lane ((int) Lane::speed, id::laneShape).toRawUTF8())) == (int) LfoShape::custom,
                        "editing a sine lane turns it into a custom curve");
                expect (proc.curves->getPoints ((int) Lane::speed).size() == 18, "the curve starts as that sine (17 points) plus the new point");

                bool reachesEngine = false;
                for (int i = 80; i < 110; ++i)
                    reachesEngine = reachesEngine || proc.engine.motion.tableValue ((int) Lane::speed, i) > 0.55f;
                expect (reachesEngine, "the dragged point reaches the audio engine's curve table");

                // Alt-drag bends the segment under the pointer
                const juce::Point<float> onSegment (w * 0.7f, h * 0.6f);
                const juce::ModifierKeys alt (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier);
                curve->mouseDown (mouse (*curve, onSegment, onSegment, false, 1, alt));
                curve->mouseDrag (mouse (*curve, onSegment + juce::Point<float> (0.0f, 40.0f), onSegment, true, 1, alt));
                curve->mouseUp (mouse (*curve, onSegment + juce::Point<float> (0.0f, 40.0f), onSegment, true, 1, alt));

                bool bent = false;
                for (const auto& pt : proc.curves->getPoints ((int) Lane::speed))
                    bent = bent || std::abs (pt.curve) > 0.2f;
                expect (bent, "Alt-dragging a segment bends it");

                // double-click removes a point
                const juce::Point<float> onPoint (w * 0.37f, h * 0.2f);
                curve->mouseDoubleClick (mouse (*curve, onPoint, onPoint, false, 2));
                expect (proc.curves->getPoints ((int) Lane::speed).size() == 17, "double-clicking a point removes it");
            });
            wait (3);
            steps.push_back ([this]
            {
                auto* drawKey = findChild<juce::Button> (*editor, [] (juce::Button& b) { return b.getButtonText() == "DRAW"; });
                expect (drawKey != nullptr, "DRAW key found");
                if (drawKey != nullptr)
                    drawKey->setToggleState (true, juce::sendNotificationSync);

                auto* curve = findById (*editor, "motionCurve");
                const auto w = (float) curve->getWidth(), h = (float) curve->getHeight();

                // one stroke: a ramp up over the first half, then a plateau
                curve->mouseDown (mouse (*curve, { w * 0.05f, h * 0.85f }, { w * 0.05f, h * 0.85f }));
                for (int i = 1; i <= 24; ++i)
                {
                    const float t = (float) i / 24.0f;
                    const float y = t < 0.5f ? 0.85f - 1.4f * t : 0.15f;
                    curve->mouseDrag (mouse (*curve, { w * (0.05f + 0.9f * t), h * y }, { w * 0.05f, h * 0.85f }, true));
                }
                curve->mouseUp (mouse (*curve, { w * 0.95f, h * 0.15f }, { w * 0.05f, h * 0.85f }, true));

                const auto pts = proc.curves->getPoints ((int) Lane::speed);
                expect (pts.size() == 33, "freehand drawing produces a 33-step curve");
                expect (pts.size() == 33 && pts[4].y < 0.35f && pts[24].y > 0.75f, "the stroke is what ended up in the curve");

                if (drawKey != nullptr)
                    drawKey->setToggleState (false, juce::sendNotificationSync);

                setParam (proc, id::lane ((int) Lane::speed, id::laneDepth).toRawUTF8(), 35.0f);
            });
            wait (20);
            steps.push_back ([this] { save ("interact-drawn-curve"); });
            steps.push_back ([this]
            {
                auto* resetKey = findChild<juce::Button> (*editor, [] (juce::Button& b) { return b.getButtonText() == "RESET"; });
                if (resetKey != nullptr) resetKey->onClick();
                expect (proc.curves->getPoints ((int) Lane::speed).size() == MotionCurves::defaultCurve ((int) Lane::speed).size(),
                        "RESET restores the lane's starting curve");
                proc.undoManager.undo();
            });
            wait (4);
            steps.push_back ([this]
            {
                expect (proc.curves->getPoints ((int) Lane::speed).size() == 33, "undo brings the drawn curve back");
            });

            // ---- typing a value under a knob ----------------------------------------------------------------
            steps.push_back ([this]
            {
                auto* knob = findById (*editor, id::feedback);
                expect (knob != nullptr, "FEEDBACK knob found");
                if (knob == nullptr) return;

                const juce::Point<float> legend ((float) knob->getWidth() * 0.5f, (float) knob->getHeight() - 4.0f);
                knob->mouseDoubleClick (mouse (*knob, legend, legend, false, 2));

                auto* entry = findChild<juce::TextEditor> (*knob, [] (juce::TextEditor&) { return true; });
                expect (entry != nullptr, "double-clicking a knob's name opens a text field");

                if (entry != nullptr)
                {
                    entry->setText ("80", false);
                    entry->onReturnKey();
                }
            });
            wait (3);
            steps.push_back ([this]
            {
                expect (std::abs (value (id::feedback) - 80.0f) < 0.01f, "the typed value is applied (feedback = 80 %)");

                auto* knob = findById (*editor, id::time);
                if (knob != nullptr)
                {
                    const juce::Point<float> legend ((float) knob->getWidth() * 0.5f, (float) knob->getHeight() - 4.0f);
                    knob->mouseDoubleClick (mouse (*knob, legend, legend, false, 2));
                    if (auto* entry = findChild<juce::TextEditor> (*knob, [] (juce::TextEditor&) { return true; }))
                    {
                        entry->setText ("1.5 s", false);
                        entry->onReturnKey();
                    }
                }
            });
            wait (3);
            steps.push_back ([this] { expect (std::abs (value (id::time) - 1500.0f) < 0.5f, "units are understood (\"1.5 s\" = 1500 ms)"); });

            // ---- preset browser -----------------------------------------------------------------------------------
            steps.push_back ([this] { editor->showPresetBrowser (true); });
            wait (3);
            steps.push_back ([this]
            {
                auto* browser = dynamic_cast<dlay::ui::PresetBrowser*> (findById (*editor, "browser"));
                expect (browser != nullptr && browser->isVisible(), "preset library opens");
                if (browser == nullptr) return;

                proc.presets->load (0);
                browser->refresh();
                browser->step (1);
                expect (proc.presets->getCurrentIndex() == 1, "next preset");
                browser->step (-1);
                expect (proc.presets->getCurrentIndex() == 0, "previous preset");

                auto* list = findChild<juce::ListBox> (*browser, [] (juce::ListBox&) { return true; });
                expect (list != nullptr, "preset list found");
                if (list != nullptr)
                {
                    list->selectRow (7);
                    expect (proc.presets->getCurrentIndex() == 7, "selecting a row (or arrowing to it) loads that preset");
                }

                auto* search = findChild<juce::TextEditor> (*browser, [] (juce::TextEditor& t) { return t.getTextToShowWhenEmpty().containsIgnoreCase ("search"); });
                expect (search != nullptr, "search field found");
                if (search != nullptr)
                    search->setText ("ghost machine", true);        // the text editor notifies asynchronously, like typing does
            });
            wait (4);
            steps.push_back ([this]
            {
                auto* browser = dynamic_cast<dlay::ui::PresetBrowser*> (findById (*editor, "browser"));
                if (browser == nullptr) return;

                auto* list = findChild<juce::ListBox> (*browser, [] (juce::ListBox&) { return true; });
                if (list == nullptr) return;

                expect (list->getListBoxModel() != nullptr && list->getListBoxModel()->getNumRows() == 1, "search narrows the list to the matching preset");
                list->selectRow (0);
                expect (proc.presets->getCurrentName() == "Ghost Machine", "the found preset loads");

                if (auto* search = findChild<juce::TextEditor> (*browser, [] (juce::TextEditor& t) { return t.getTextToShowWhenEmpty().containsIgnoreCase ("search"); }))
                    search->setText ({}, true);
            });
            wait (25);
            steps.push_back ([this] { save ("interact-ghost-machine-browser"); editor->showPresetBrowser (false); });
            wait (40);
            steps.push_back ([this] { save ("interact-ghost-machine"); });

            // ---- views and sizes -----------------------------------------------------------------------------------------
            steps.push_back ([this]
            {
                // the editor opens no larger than the screen allows, so compare at its own scale
                const double scale = (double) editor->getWidth() / (double) DlayAudioProcessorEditor::kBaseWidth;
                editor->setAdvancedView (false);
                expect (editor->getHeight() == juce::roundToInt (DlayAudioProcessorEditor::kSimpleHeight * scale), "compact view");
                editor->setWindowScale (0.75);
                expect (editor->getWidth() == 930, "75 % window");
            });
            wait (3);
            steps.push_back ([this]
            {
                editor->setWindowScale (1.5);
                editor->setAdvancedView (true);
                expect (editor->getWidth() == 1860 && editor->getHeight() == 1332, "150 % advanced window");
            });
            wait (3);
            steps.push_back ([this] { editor->setWindowScale (1.0); });
            wait (3);

            // ---- every algorithm, with the extra readers switched on -----------------------------------------------------------
            steps.push_back ([this]
            {
                proc.presets->loadInit();
                setParam (proc, id::collapse, 60.0f);
                setParam (proc, id::ghost, 70.0f);
                setParam (proc, id::orbit, 80.0f);
                setParam (proc, id::feedback, 60.0f);
                setParam (proc, id::reverse, 30.0f);
            });

            for (int m = 0; m < kNumModes; ++m)
            {
                steps.push_back ([this, m] { setParam (proc, id::mode, (float) m); });
                wait (9);

                if (m == (int) Mode::multiTap || m == (int) Mode::granular || m == (int) Mode::glitch || m == (int) Mode::fractal)
                    steps.push_back ([this, m] { save ("interact-mode-" + juce::String (modeInfo (m).name).toLowerCase().replaceCharacter (' ', '-')); });
            }

            steps.push_back ([this]
            {
                bool finite = true;
                juce::AudioBuffer<float> block (2, blockSize);
                juce::MidiBuffer midi;
                block.clear();
                proc.processBlock (block, midi);
                for (int i = 0; i < blockSize; ++i)
                    finite = finite && std::isfinite (block.getSample (0, i)) && std::isfinite (block.getSample (1, i));
                expect (finite, "audio is still healthy after stepping through all 21 algorithms");
            });
        }

        void timerCallback() override
        {
            pumpAudio();
            ++frame;

            // a small script: warm up, then take the shots a few frames apart
            const int f = frame - warmupFrames;

            if (f == 0)         { editor->setAdvancedView (true); }
            else if (f == 4)    { save ("advanced"); }
            else if (f == 5)    { editor->setAdvancedView (false); }
            else if (f == 9)    { save ("simple"); }
            else if (f == 10)   { editor->setAdvancedView (true); editor->showPresetBrowser (true); }
            else if (f == 14)   { save ("browser"); editor->showPresetBrowser (false); }
            else if (f > 14)
            {
                const int laneStep = f - 15;

                if (! lanes || laneStep >= dlay::kNumLanes * 12)
                {
                    if (interactive)
                    {
                        if (steps.empty())
                            buildInteractionScript();

                        if (nextStep < steps.size())
                        {
                            steps[nextStep++]();
                            return;
                        }
                    }

                    reportPaintCost();
                    stopTimer();
                    juce::MessageManager::getInstance()->stopDispatchLoop();
                    return;
                }

                const int lane = laneStep / 12;
                if (laneStep % 12 == 0)        editor->showMotionLane (lane);
                else if (laneStep % 12 == 11)  save ("motion-" + juce::String (dlay::kLanes[(size_t) lane].name).toLowerCase());
            }
        }

        DlayAudioProcessor proc;
        TestPlayHead playHead;
        std::unique_ptr<DlayAudioProcessorEditor> editor;
        StereoBuffer input;

        juce::File outDir;
        float snapshotScale;
        int warmupFrames, frame = 0, position = 0;
        bool lanes, interactive;
        std::vector<std::function<void()>> steps;
        size_t nextStep = 0;

    public:
        int failures = 0;

    private:
        static constexpr double sampleRate = 48000.0;
        static constexpr int blockSize = 512;
    };
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const juce::StringArray args (argv + 1, argc - 1);

    if (args.isEmpty())
    {
        std::printf ("usage: DLAY_UiSnapshot <output-dir> [--preset \"Name\"] [--scale 1.0] [--seconds 3.5] [--lanes] [--interact]\n");
        return 1;
    }

    int failures = 0;

    const auto value = [&args] (const char* flag, const juce::String& fallback)
    {
        const int i = args.indexOf (flag);
        return i >= 0 && i + 1 < args.size() ? args[i + 1] : fallback;
    };

    {
        Runner runner (juce::File::getCurrentWorkingDirectory().getChildFile (args[0]),
                       value ("--preset", {}), value ("--scale", "1.0").getFloatValue(),
                       value ("--seconds", "3.5").getDoubleValue(), args.contains ("--lanes"), args.contains ("--interact"));

        juce::MessageManager::getInstance()->runDispatchLoop();
        failures = runner.failures;
    }

    if (args.contains ("--interact"))
        std::printf ("%d interaction failures\n", failures);

    return failures;
}
