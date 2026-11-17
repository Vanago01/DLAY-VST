#include "PresetBrowser.h"

namespace dlay::ui
{

namespace
{
    void drawStar (juce::Graphics& g, juce::Point<float> c, float r, juce::Colour colour, bool filled)
    {
        juce::Path star;
        star.addStar (c, 5, r * 0.45f, r, -0.0f);
        g.setColour (colour);

        if (filled) g.fillPath (star);
        else        g.strokePath (star, juce::PathStrokeType (1.0f));
    }

    void styleEditor (juce::TextEditor& e, const juce::String& placeholder, float fontHeight)
    {
        e.setFont (font (FontKind::medium, fontHeight, 0.04f));
        e.setTextToShowWhenEmpty (placeholder, col::creamFaint);
        e.setIndents (8, 5);
        e.setSelectAllWhenFocused (true);
    }
}

// ============================================================================
class PresetBrowser::CategoryList : public juce::Component
{
public:
    explicit CategoryList (PresetBrowser& o) : owner (o) {}

    void rebuild()
    {
        rows.clear();
        const auto& entries = owner.presets.getEntries();

        int favourites = 0;
        for (int i = 0; i < (int) entries.size(); ++i)
            if (owner.presets.isFavourite (i)) ++favourites;

        rows.push_back ({ "ALL PRESETS", "", (int) entries.size() });
        rows.push_back ({ "FAVOURITES", "*", favourites });

        for (const auto& name : PresetManager::getCategories())
        {
            int count = 0;
            for (const auto& e : entries)
                if (e.category == name) ++count;

            rows.push_back ({ name.toUpperCase(), name, count });
        }

        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const float h = rowHeight();

        for (int i = 0; i < (int) rows.size(); ++i)
        {
            const auto r = juce::Rectangle<float> (0.0f, h * (float) i, (float) getWidth(), h);
            const bool selected = rows[(size_t) i].key == owner.category;
            const bool empty = rows[(size_t) i].count == 0;

            if (selected)
            {
                g.setColour (col::orange);
                g.fillRoundedRectangle (r.reduced (1.0f, 1.0f), 2.5f);
            }
            else if (i == hover)
            {
                g.setColour (juce::Colours::white.withAlpha (0.05f));
                g.fillRoundedRectangle (r.reduced (1.0f, 1.0f), 2.5f);
            }

            if (i == 1)
                drawStar (g, { r.getX() + 12.0f, r.getCentreY() }, 5.0f, selected ? col::ink : col::amber, true);

            g.setFont (font (FontKind::semiBold, juce::jmin (12.5f, h * 0.56f), 0.1f));
            g.setColour (selected ? col::ink : (empty ? col::creamFaint : col::cream));
            g.drawText (rows[(size_t) i].label, r.withTrimmedLeft (i == 1 ? 24.0f : 10.0f).withTrimmedRight (34.0f), juce::Justification::centredLeft, true);

            g.setFont (monoFont (juce::jmin (10.0f, h * 0.45f)));
            g.setColour (selected ? col::ink : col::creamFaint);
            g.drawText (juce::String (rows[(size_t) i].count), r.withTrimmedRight (9.0f), juce::Justification::centredRight, false);

            if (i == 1)
            {
                g.setColour (col::edge);
                g.drawHorizontalLine (juce::roundToInt (r.getBottom()), 6.0f, (float) getWidth() - 6.0f);
            }
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int row = (int) ((float) e.y / rowHeight());
        if (row != hover) { hover = row; repaint(); }
    }

    void mouseExit (const juce::MouseEvent&) override   { hover = -1; repaint(); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int row = (int) ((float) e.y / rowHeight());

        if (row >= 0 && row < (int) rows.size())
        {
            owner.category = rows[(size_t) row].key;
            owner.refresh();
        }
    }

private:
    struct Row { juce::String label, key; int count; };

    float rowHeight() const     { return juce::jmin (26.0f, (float) getHeight() / (float) juce::jmax (1, (int) rows.size())); }

    PresetBrowser& owner;
    std::vector<Row> rows;
    int hover = -1;
};

// ============================================================================
PresetBrowser::PresetBrowser (DlayAudioProcessor& p)
    : presets (*p.presets)
{
    setWantsKeyboardFocus (true);

    categories = std::make_unique<CategoryList> (*this);
    addAndMakeVisible (*categories);

    styleEditor (search, "Search presets...", 15.0f);
    search.onTextChange = [this] { refresh(); };
    search.onReturnKey  = [this] { if (list.isShowing()) list.grabKeyboardFocus(); };
    search.onEscapeKey  = [this] { if (onClose != nullptr) onClose(); };
    addAndMakeVisible (search);

    list.setRowHeight (25);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list.setOutlineThickness (0);
    addAndMakeVisible (list);

    styleEditor (nameEdit, "Preset name", 15.0f);
    nameEdit.setSelectAllWhenFocused (false);
    nameEdit.onReturnKey = [this] { save(); };
    addAndMakeVisible (nameEdit);

    styleEditor (descriptionEdit, "Description (optional)", 13.5f);
    descriptionEdit.setSelectAllWhenFocused (false);
    descriptionEdit.setMultiLine (true);
    descriptionEdit.setReturnKeyStartsNewLine (false);
    addAndMakeVisible (descriptionEdit);

    for (auto* b : { &saveButton, &deleteButton, &importButton, &folderButton, &initButton, &closeButton, &categoryButton })
    {
        b->setShowLed (false);
        b->setFontHeight (11.5f);
        addAndMakeVisible (*b);
    }

    saveButton.setTooltip ("Save the current sound as a user preset with this name, category and description.");
    saveButton.onClick = [this] { save(); };

    categoryButton.setTooltip ("Category the preset will be saved in.");
    categoryButton.onClick = [this] { chooseSaveCategory(); };

    deleteButton.setTooltip ("Delete the selected user preset. Factory presets can not be deleted.");
    deleteButton.onClick = [this]
    {
        const int index = presets.getCurrentIndex();
        if (index >= 0 && ! presets.getEntries()[(size_t) index].isFactory)
        {
            const auto name = presets.getCurrentName();
            setStatus (presets.deleteUserPreset (index) ? "Deleted \"" + name + "\"" : "Could not delete the preset file.", false);
        }
    };

    importButton.setTooltip ("Copy a .dlaypreset file into your library.");
    importButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Import a DLAY preset", juce::File(), juce::String ("*") + PresetManager::fileExtension);
        juce::Component::SafePointer<PresetBrowser> safe (this);

        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe] (const juce::FileChooser& fc)
                              {
                                  if (safe == nullptr) return;
                                  const auto file = fc.getResult();
                                  if (file == juce::File()) return;

                                  const auto result = safe->presets.importPresetFile (file);
                                  safe->setStatus (result.wasOk() ? "Imported \"" + file.getFileNameWithoutExtension() + "\"" : result.getErrorMessage(), result.failed());
                              });
    };

    folderButton.setTooltip ("Show the folder that holds your presets. They are plain files: copy them to share them.");
    folderButton.onClick = [this]
    {
        const auto dir = presets.getUserPresetDirectory();
        dir.createDirectory();
        dir.revealToUser();
    };

    initButton.setTooltip ("Reset every control to its default: a clean starting point.");
    initButton.onClick = [this] { presets.loadInit(); setStatus ("Initialised", false); };

    closeButton.onClick = [this] { if (onClose != nullptr) onClose(); };

    saveCategory = PresetManager::userCategory;
    presets.addChangeListener (this);
    refresh();
}

PresetBrowser::~PresetBrowser()
{
    presets.removeChangeListener (this);
}

// ============================================================================
void PresetBrowser::refresh()
{
    updating = true;

    filtered = presets.filter (search.getText(), category == "*" ? juce::String() : category, category == "*");
    categories->rebuild();
    list.updateContent();

    const int current = presets.getCurrentIndex();
    const auto it = std::find (filtered.begin(), filtered.end(), current);

    if (it != filtered.end())
    {
        const int row = (int) (it - filtered.begin());
        list.selectRow (row, false, true);
        list.scrollToEnsureRowIsOnscreen (row);     // selectRow does not scroll when the row was already selected
    }
    else
    {
        list.deselectAllRows();
    }

    deleteButton.setEnabled (current >= 0 && ! presets.getEntries()[(size_t) current].isFactory);
    categoryButton.setButtonText (saveCategory);

    updating = false;
    list.repaint();
    repaint();
}

void PresetBrowser::prepareToShow (bool focusSaveField)
{
    status.clear();
    nameEdit.setText (presets.getCurrentName() == "Init" ? juce::String() : presets.getCurrentName(), false);

    const int current = presets.getCurrentIndex();
    descriptionEdit.setText (current >= 0 ? presets.getEntries()[(size_t) current].description : juce::String(), false);

    refresh();

    if (! isShowing())
        return;                                 // focus can only go to something that is on screen

    if (focusSaveField)
    {
        nameEdit.grabKeyboardFocus();
        nameEdit.selectAll();
    }
    else
    {
        search.grabKeyboardFocus();
    }
}

void PresetBrowser::step (int delta)
{
    // walk the visible (filtered) list when the current preset is part of it
    const int current = presets.getCurrentIndex();
    const auto it = std::find (filtered.begin(), filtered.end(), current);

    if (filtered.empty() || it == filtered.end())
    {
        presets.loadRelative (delta);
        return;
    }

    const int n = (int) filtered.size();
    int row = ((int) (it - filtered.begin()) + delta) % n;
    if (row < 0) row += n;
    presets.load (filtered[(size_t) row]);
}

// ============================================================================
void PresetBrowser::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (row < 0 || row >= (int) filtered.size())
        return;

    const int index = filtered[(size_t) row];
    const auto& entry = presets.getEntries()[(size_t) index];
    const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);
    const bool current = index == presets.getCurrentIndex();

    if (selected || current)
    {
        g.setColour (current ? col::orange : juce::Colours::white.withAlpha (0.08f));
        g.fillRoundedRectangle (r.reduced (1.0f, 1.0f), 2.5f);
    }
    else if ((row & 1) != 0)
    {
        g.setColour (juce::Colours::white.withAlpha (0.018f));
        g.fillRect (r);
    }

    const bool fav = presets.isFavourite (index);
    drawStar (g, { 14.0f, r.getCentreY() }, 5.5f, current ? col::ink : (fav ? col::amber : col::creamFaint.withAlpha (0.7f)), fav);

    g.setFont (font (FontKind::semiBold, 14.5f, 0.05f));
    g.setColour (current ? col::ink : col::cream);
    g.drawText (entry.name, r.withTrimmedLeft (30.0f).withTrimmedRight (170.0f), juce::Justification::centredLeft, true);

    g.setFont (font (FontKind::medium, 11.5f, 0.08f));
    g.setColour (current ? col::ink.withAlpha (0.8f) : col::creamFaint);
    g.drawText ((entry.isFactory ? juce::String() : juce::String ("USER  /  ")) + entry.category.toUpperCase(),
                r.withTrimmedRight (10.0f), juce::Justification::centredRight, true);
}

void PresetBrowser::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (row < 0 || row >= (int) filtered.size())
        return;

    const int index = filtered[(size_t) row];

    if (e.x < 28)
    {
        presets.setFavourite (index, ! presets.isFavourite (index));
        return;
    }

    if (index != presets.getCurrentIndex() || presets.isModified())
        presets.load (index);
}

void PresetBrowser::listBoxItemDoubleClicked (int row, const juce::MouseEvent& e)
{
    if (e.x >= 28 && row >= 0 && onClose != nullptr)
        onClose();
}

void PresetBrowser::selectedRowsChanged (int row)
{
    // keyboard navigation auditions as it goes
    if (updating || row < 0 || row >= (int) filtered.size())
        return;

    const int index = filtered[(size_t) row];
    if (index != presets.getCurrentIndex())
        presets.load (index);
}

juce::String PresetBrowser::getTooltipForRow (int row)
{
    if (row < 0 || row >= (int) filtered.size())
        return {};

    return presets.getEntries()[(size_t) filtered[(size_t) row]].description;
}

// ============================================================================
void PresetBrowser::save()
{
    const auto name = nameEdit.getText().trim();
    const auto result = presets.saveUserPreset (name, saveCategory, descriptionEdit.getText().trim());

    setStatus (result.wasOk() ? "Saved \"" + name + "\" to " + saveCategory : result.getErrorMessage(), result.failed());

    if (result.wasOk())
    {
        category = saveCategory;
        refresh();
    }
}

void PresetBrowser::setStatus (const juce::String& text, bool isError)
{
    status = text;
    statusIsError = isError;
    repaint();
}

void PresetBrowser::chooseSaveCategory()
{
    juce::PopupMenu menu;
    const auto names = PresetManager::getCategories();

    for (int i = 0; i < names.size(); ++i)
        menu.addItem (i + 1, names[i], true, names[i] == saveCategory);

    juce::Component::SafePointer<PresetBrowser> safe (this);
    showPopupMenu (menu, categoryButton, [safe, names] (int result)
    {
        if (safe != nullptr && result > 0 && result <= names.size())
        {
            safe->saveCategory = names[result - 1];
            safe->categoryButton.setButtonText (safe->saveCategory);
        }
    });
}

bool PresetBrowser::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        if (onClose != nullptr) onClose();
        return true;
    }

    return false;
}

// ============================================================================
void PresetBrowser::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();

    g.setColour (col::bg.withAlpha (0.93f));
    g.fillRect (b);

    const auto panel = b.reduced (6.0f, 4.0f);
    drawPanel (g, panel);
    drawSectionHeader (g, juce::Rectangle<float> (panel.getX() + 20.0f, panel.getY() + 9.0f, panel.getWidth() - 130.0f, 14.0f), "00", "PRESET LIBRARY");

    drawScreen (g, listArea.toFloat().expanded (3.0f), 4.0f);
    drawScreen (g, leftArea.toFloat().expanded (3.0f), 4.0f);

    // ---- info column
    auto info = infoArea.toFloat();
    const int current = presets.getCurrentIndex();

    g.setFont (font (FontKind::semiBold, 9.5f, 0.2f));
    g.setColour (col::creamFaint);
    g.drawText ("NOW PLAYING", info.removeFromTop (12.0f), juce::Justification::centredLeft, false);

    g.setFont (font (FontKind::bold, 24.0f, 0.03f));
    g.setColour (col::cream);
    g.drawText (presets.getCurrentName() + (presets.isModified() ? " *" : ""), info.removeFromTop (30.0f), juce::Justification::centredLeft, true);

    g.setFont (font (FontKind::semiBold, 11.5f, 0.14f));
    g.setColour (col::orange);
    juce::String tag = presets.getCurrentCategory().toUpperCase();
    if (current >= 0)
        tag = (presets.getEntries()[(size_t) current].isFactory ? "FACTORY  /  " : "USER  /  ") + tag;
    g.drawText (tag, info.removeFromTop (16.0f), juce::Justification::centredLeft, true);

    info.removeFromTop (6.0f);

    if (current >= 0)
    {
        juce::AttributedString text;
        text.append (presets.getEntries()[(size_t) current].description, font (FontKind::medium, 14.0f, 0.03f), col::creamDim);
        juce::TextLayout layout;
        layout.createLayout (text, info.getWidth());
        layout.draw (g, info.removeFromTop (50.0f));
    }

    g.setFont (font (FontKind::semiBold, 9.5f, 0.2f));
    g.setColour (col::creamFaint);
    g.drawText ("SAVE AS", juce::Rectangle<float> ((float) nameEdit.getX(), (float) nameEdit.getY() - 15.0f, 120.0f, 11.0f), juce::Justification::centredLeft, false);
    g.drawText ("CATEGORY", juce::Rectangle<float> ((float) categoryButton.getX(), (float) categoryButton.getY() - 15.0f, 120.0f, 11.0f), juce::Justification::centredLeft, false);

    if (status.isNotEmpty())
    {
        g.setFont (font (FontKind::medium, 12.5f, 0.04f));
        g.setColour (statusIsError ? col::red : col::amber);
        g.drawFittedText (status, juce::Rectangle<int> (infoArea.getX(), saveButton.getBottom() + 6, infoArea.getWidth(), 30),
                          juce::Justification::topLeft, 2);
    }

    if (filtered.empty())
    {
        g.setFont (font (FontKind::medium, 14.0f, 0.06f));
        g.setColour (col::creamFaint);
        g.drawText (category == "*" ? "No favourites yet - click the star next to a preset." : "No presets match.",
                    listArea, juce::Justification::centred, false);
    }
}

void PresetBrowser::resized()
{
    auto area = getLocalBounds().reduced (24, 12);
    auto top = area.removeFromTop (22);
    closeButton.setBounds (top.removeFromRight (70).withHeight (20));
    area.removeFromTop (10);

    leftArea = area.removeFromLeft (214);
    area.removeFromLeft (16);
    infoArea = area.removeFromRight (300);
    area.removeFromRight (18);

    categories->setBounds (leftArea);

    search.setBounds (area.removeFromTop (30));
    area.removeFromTop (10);
    listArea = area;
    list.setBounds (listArea);

    // save section sits at the bottom of the info column
    auto save = infoArea;
    save.removeFromTop (136);
    save.removeFromTop (15);
    nameEdit.setBounds (save.removeFromTop (30));
    save.removeFromTop (22);
    categoryButton.setBounds (save.removeFromTop (26));
    save.removeFromTop (8);
    descriptionEdit.setBounds (save.removeFromTop (juce::jlimit (30, 62, save.getHeight() - 130)));
    save.removeFromTop (8);
    saveButton.setBounds (save.removeFromTop (30));

    auto tools = infoArea.removeFromBottom (26);
    const int w = (tools.getWidth() - 12) / 4;
    deleteButton.setBounds (tools.removeFromLeft (w));
    tools.removeFromLeft (4);
    importButton.setBounds (tools.removeFromLeft (w));
    tools.removeFromLeft (4);
    folderButton.setBounds (tools.removeFromLeft (w));
    tools.removeFromLeft (4);
    initButton.setBounds (tools);
}

} // namespace dlay::ui
