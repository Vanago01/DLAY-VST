#pragma once

#include "Widgets.h"
#include "../PluginProcessor.h"

namespace dlay::ui
{

/** Preset library overlay: categories, search, favourites, instant auditioning, and saving,
    importing and deleting user presets. Arrow keys step through the list and load as you go. */
class PresetBrowser : public juce::Component,
                      private juce::ListBoxModel,
                      private juce::ChangeListener
{
public:
    explicit PresetBrowser (DlayAudioProcessor&);
    ~PresetBrowser() override;

    std::function<void()> onClose;

    void refresh();
    void prepareToShow (bool focusSaveField);

    /** Loads the previous / next preset within the current filter. */
    void step (int delta);

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    class CategoryList;

    // ListBoxModel
    int getNumRows() override                                  { return (int) filtered.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void selectedRowsChanged (int lastRowSelected) override;
    void returnKeyPressed (int) override                       { if (onClose != nullptr) onClose(); }
    juce::String getTooltipForRow (int row) override;

    void changeListenerCallback (juce::ChangeBroadcaster*) override   { refresh(); }

    void save();
    void setStatus (const juce::String& text, bool isError);
    void chooseSaveCategory();

    PresetManager& presets;

    std::unique_ptr<CategoryList> categories;
    juce::TextEditor search, nameEdit, descriptionEdit;
    juce::ListBox list { "presets", this };
    KeyButton saveButton { "SAVE PRESET", col::amber }, deleteButton { "DELETE" }, importButton { "IMPORT" },
              folderButton { "FOLDER" }, initButton { "INIT" }, closeButton { "CLOSE" }, categoryButton { "User" };

    std::vector<int> filtered;
    juce::String category, saveCategory, status;       // category: "" = all, "*" = favourites
    bool statusIsError = false, updating = false;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Rectangle<int> leftArea, listArea, infoArea;
};

} // namespace dlay::ui
