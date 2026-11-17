#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "FactoryPresets.h"

namespace dlay
{

/** Factory + user preset library: browsing, search, favourites, save, import.

    A preset is the complete sound: every parameter except the session settings (bypass,
    quality) plus the six MOTION curves. User presets are small XML files, so they can be
    copied between machines or shared as they are.
*/
class PresetManager : public juce::ChangeBroadcaster,
                      private juce::AudioProcessorValueTreeState::Listener
{
public:
    struct Entry
    {
        juce::String name, category, description;
        bool isFactory = true;
        int factoryIndex = -1;
        juce::File file;
    };

    PresetManager (juce::AudioProcessor& processor, juce::AudioProcessorValueTreeState& state,
                   MotionCurves& curves, juce::UndoManager& undo);
    ~PresetManager() override;

    // ---- library ------------------------------------------------------------
    const std::vector<Entry>& getEntries() const noexcept   { return entries; }
    int getNumFactoryPresets() const noexcept               { return numFactory; }
    static juce::StringArray getCategories();
    static const juce::String userCategory;

    /** Indices of the entries matching the search text, category ("" = all) and favourites filter. */
    std::vector<int> filter (const juce::String& search, const juce::String& category, bool favouritesOnly) const;

    // ---- current preset -----------------------------------------------------
    int getCurrentIndex() const noexcept                    { return currentIndex; }
    juce::String getCurrentName() const                     { const juce::ScopedLock l (currentLock); return currentName; }
    juce::String getCurrentCategory() const                 { const juce::ScopedLock l (currentLock); return currentCategory; }
    bool isModified() const noexcept                        { return modified.load(); }
    void markModified() noexcept                            { if (! loading.load()) modified = true; }

    void load (int index);
    void loadRelative (int delta);                          // previous / next, wraps around
    void loadInit();                                        // every parameter back to its default

    // ---- user presets -------------------------------------------------------
    juce::Result saveUserPreset (const juce::String& name, const juce::String& category, const juce::String& description);
    bool deleteUserPreset (int index);
    juce::Result importPresetFile (const juce::File& file);
    juce::File getUserPresetDirectory() const;
    void rescan();

    // ---- favourites ---------------------------------------------------------
    bool isFavourite (int index) const;
    void setFavourite (int index, bool shouldBeFavourite);

    // ---- settings -----------------------------------------------------------
    bool isMixLocked() const noexcept                       { return mixLocked; }
    void setMixLocked (bool locked);

    // ---- session state ------------------------------------------------------
    void saveToState (juce::ValueTree& state) const;
    void restoreFromState (const juce::ValueTree& state);

    /** Called after any preset has been applied (the processor clears the delay memory). */
    std::function<void()> onPresetApplied;

    static constexpr const char* fileExtension = ".dlaypreset";

private:
    void applyValues (const std::vector<std::pair<juce::String, float>>& values, const juce::ValueTree& motionTree,
                      const std::vector<std::pair<int, std::vector<CurvePoint>>>* factoryCurves);
    std::unique_ptr<juce::XmlElement> createPresetXml (const juce::String& name, const juce::String& category,
                                                       const juce::String& description) const;
    static bool readPresetXml (const juce::XmlElement& xml, Entry& info,
                               std::vector<std::pair<juce::String, float>>* values, juce::ValueTree* motionTree);
    void setCurrent (int index, const juce::String& name, const juce::String& category);
    void setCurrentNames (const juce::String& name, const juce::String& category);
    juce::String favouriteKey (const Entry&) const;
    juce::File getSettingsFile() const;
    void loadSettings();
    void saveSettings() const;

    void parameterChanged (const juce::String&, float) override;

    juce::AudioProcessor& processor;
    juce::AudioProcessorValueTreeState& apvts;
    MotionCurves& motionCurves;
    juce::UndoManager& undoManager;

    std::vector<Entry> entries;
    int numFactory = 0;

    // The library belongs to the message thread, but a host may ask for the session state
    // (which carries the preset's name) from any thread: the two strings have their own lock.
    juce::CriticalSection currentLock;
    int currentIndex = -1;
    juce::String currentName { "Init" }, currentCategory;
    std::atomic<bool> modified { false };
    std::atomic<bool> loading { false };

    juce::StringArray favourites;
    bool mixLocked = false;
};

} // namespace dlay
