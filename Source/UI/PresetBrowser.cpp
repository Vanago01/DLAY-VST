#include "PresetBrowser.h"

namespace dlay::ui
{

namespace
{
    void drawHeart (juce::Graphics& g, juce::Point<float> c, float s, bool filled, juce::Colour outline)
    {
        juce::Path heart;
        heart.startNewSubPath (c.x, c.y + s * 0.92f);
        heart.cubicTo (c.x - s * 1.45f, c.y - s * 0.05f, c.x - s * 0.80f, c.y - s * 1.25f, c.x, c.y - s * 0.42f);
        heart.cubicTo (c.x + s * 0.80f, c.y - s * 1.25f, c.x + s * 1.45f, c.y - s * 0.05f, c.x, c.y + s * 0.92f);
        heart.closeSubPath();

        if (filled)
        {
            g.setGradientFill (accentGradient ({ c.x - s, c.y }, { c.x + s, c.y }));
            g.fillPath (heart);
        }
        else
        {
            g.setColour (outline);
            g.strokePath (heart, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    void styleEditor (juce::TextEditor& e, const juce::String& placeholder, float px, bool singleLine)
    {
        e.setFont (font (FontKind::medium, px, 0.0f));
        e.setTextToShowWhenEmpty (placeholder, col::inkFaint);
        e.setSelectAllWhenFocused (true);

        if (singleLine)
        {
            e.setJustification (juce::Justification::centredLeft);
            e.setIndents (12, 0);
        }
        else
        {
            e.setIndents (12, 9);
        }
    }

    constexpr int kFavouriteZone = 34;          // click this far into a row to toggle its heart
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

        rows.push_back ({ "All presets", "", (int) entries.size() });
        rows.push_back ({ "Favourites", "*", favourites });

        for (const auto& name : PresetManager::getCategories())
        {
            int count = 0;
            for (const auto& e : entries)
                if (e.category == name) ++count;

            rows.push_back ({ name, name, count });
        }

        repaint();
    }

    int getNumRows() const noexcept             { return (int) rows.size(); }

    /** Rows keep a readable height; the owner gives the list the height that needs. */
    void setRowHeight (int pixels)              { rowPixels = pixels; repaint(); }

    void paint (juce::Graphics& g) override
    {
        const float h = rowHeight();

        for (int i = 0; i < (int) rows.size(); ++i)
        {
            const auto r = juce::Rectangle<float> (0.0f, h * (float) i, (float) getWidth(), h);
            const bool selected = rows[(size_t) i].key == owner.category;
            const bool empty = rows[(size_t) i].count == 0;
            const auto pill = r.reduced (2.0f, 1.5f);
            const float corner = juce::jmin (10.0f, pill.getHeight() * 0.5f);

            if (selected)
            {
                g.setColour (col::dark);
                g.fillRoundedRectangle (pill, corner);
            }
            else if (i == hover)
            {
                g.setColour (juce::Colours::white);
                g.fillRoundedRectangle (pill, corner);
            }

            if (i == 1)
                drawHeart (g, { r.getX() + 19.0f, r.getCentreY() }, 5.6f, true, col::ink);

            g.setFont (font (FontKind::semiBold, 13.0f, 0.0f));
            g.setColour (selected ? juce::Colours::white : (empty ? col::inkFaint : col::ink));
            g.drawText (rows[(size_t) i].label, r.withTrimmedLeft (i == 1 ? 34.0f : 14.0f).withTrimmedRight (40.0f), juce::Justification::centredLeft, true);

            g.setFont (monoFont (11.0f));
            g.setColour (selected ? juce::Colours::white.withAlpha (0.7f) : col::inkFaint);
            g.drawText (juce::String (rows[(size_t) i].count), r.withTrimmedRight (14.0f), juce::Justification::centredRight, false);
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

    float rowHeight() const     { return (float) rowPixels; }

    int rowPixels = 30;

    PresetBrowser& owner;
    std::vector<Row> rows;
    int hover = -1;
};

// ============================================================================
PresetBrowser::PresetBrowser (DlayAudioProcessor& p)
    : presets (*p.presets)
{
    setWantsKeyboardFocus (true);
    setOpaque (true);

    categories = std::make_unique<CategoryList> (*this);
    categoryView.setViewedComponent (categories.get(), false);
    categoryView.setScrollBarsShown (true, false);
    categoryView.setScrollBarThickness (9);
    addAndMakeVisible (categoryView);

    styleEditor (search, "Search presets...", 14.0f, true);
    search.onTextChange = [this] { refresh(); };
    search.onReturnKey  = [this] { if (list.isShowing()) list.grabKeyboardFocus(); };
    search.onEscapeKey  = [this] { if (onClose != nullptr) onClose(); };
    addAndMakeVisible (search);

    list.setRowHeight (32);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list.setOutlineThickness (0);
    addAndMakeVisible (list);

    styleEditor (nameEdit, "Preset name", 14.0f, true);
    nameEdit.setSelectAllWhenFocused (false);
    nameEdit.onReturnKey = [this] { save(); };
    addAndMakeVisible (nameEdit);

    styleEditor (descriptionEdit, "Description (optional)", 13.0f, false);
    descriptionEdit.setSelectAllWhenFocused (false);
    descriptionEdit.setMultiLine (true);
    descriptionEdit.setReturnKeyStartsNewLine (false);
    addAndMakeVisible (descriptionEdit);

    for (auto* b : { &saveButton, &deleteButton, &importButton, &folderButton, &initButton, &closeButton, &categoryButton })
    {
        b->setFontSize (11.5f);
        addAndMakeVisible (*b);
    }

    // the one filled key on the page: saving is what this column is for
    saveButton.setClickingTogglesState (false);
    saveButton.setToggleState (true, juce::dontSendNotification);
    saveButton.setOnFill (KeyButton::OnFill::accent);
    saveButton.setFontSize (12.5f);
    categoryButton.setFontSize (12.5f);

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
    categoryButton.setButtonText ("In: " + saveCategory);

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
    const auto pill = r.reduced (3.0f, 1.5f);

    if (current)
    {
        g.setColour (col::dark);
        g.fillRoundedRectangle (pill, 10.0f);
    }
    else if (selected)
    {
        g.setColour (juce::Colours::white);
        g.fillRoundedRectangle (pill, 10.0f);
    }

    const bool fav = presets.isFavourite (index);
    drawHeart (g, { 19.0f, r.getCentreY() }, 5.6f, fav, current ? juce::Colours::white.withAlpha (0.5f) : col::inkFaint.withAlpha (0.8f));

    g.setFont (font (FontKind::semiBold, 14.0f, 0.0f));
    g.setColour (current ? juce::Colours::white : col::ink);
    g.drawText (entry.name, r.withTrimmedLeft ((float) kFavouriteZone + 2.0f).withTrimmedRight (176.0f), juce::Justification::centredLeft, true);

    g.setFont (capsFont (10.0f));
    g.setColour (current ? juce::Colours::white.withAlpha (0.7f) : col::inkFaint);
    g.drawText ((entry.isFactory ? juce::String() : juce::String ("USER / ")) + entry.category.toUpperCase(),
                r.withTrimmedRight (16.0f), juce::Justification::centredRight, true);
}

void PresetBrowser::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (row < 0 || row >= (int) filtered.size())
        return;

    const int index = filtered[(size_t) row];

    if (e.x < kFavouriteZone)
    {
        presets.setFavourite (index, ! presets.isFavourite (index));
        return;
    }

    if (index != presets.getCurrentIndex() || presets.isModified())
        presets.load (index);
}

void PresetBrowser::listBoxItemDoubleClicked (int row, const juce::MouseEvent& e)
{
    if (e.x >= kFavouriteZone && row >= 0 && onClose != nullptr)
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
            safe->categoryButton.setButtonText ("In: " + safe->saveCategory);
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

    // the library takes over everything below the top bar: the same page, one large card
    {
        juce::Graphics::ScopedSaveState state (g);
        g.setOrigin (-getX(), -getY());
        drawPage (g, getParentComponent() != nullptr ? getParentComponent()->getLocalBounds().toFloat()
                                                     : getBounds().toFloat());
    }

    const auto panel = b.reduced (12.0f, 4.0f).withTrimmedBottom (8.0f);
    drawCard (g, panel, 18.0f, false);
    g.setColour (col::line);
    g.drawRoundedRectangle (panel, 18.0f, 1.0f);

    drawSectionHeader (g, juce::Rectangle<float> (panel.getX() + 22.0f, panel.getY() + 20.0f, 300.0f, 16.0f), "00", "PRESET LIBRARY");

    drawField (g, leftArea.toFloat().expanded (4.0f), 14.0f);
    drawField (g, listArea.toFloat().expanded (4.0f), 14.0f);

    // ---- info column
    const int current = presets.getCurrentIndex();
    auto info = infoArea.toFloat().withBottom ((float) nameEdit.getY() - 26.0f);

    g.setFont (capsFont (10.5f));
    g.setColour (col::inkFaint);
    g.drawText ("NOW PLAYING", info.removeFromTop (14.0f), juce::Justification::centredLeft, false);

    g.setFont (font (FontKind::heavy, 23.0f, -0.01f));
    g.setColour (col::ink);
    g.drawText (presets.getCurrentName() + (presets.isModified() ? " *" : ""), info.removeFromTop (34.0f), juce::Justification::centredLeft, true);

    juce::String tag = presets.getCurrentCategory().toUpperCase();
    if (current >= 0)
        tag = (presets.getEntries()[(size_t) current].isFactory ? "FACTORY / " : "USER / ") + tag;
    drawAccentText (g, tag, info.removeFromTop (18.0f), juce::Justification::centredLeft, capsFont (11.0f));

    info.removeFromTop (8.0f);

    if (current >= 0 && info.getHeight() >= 16.0f)
    {
        juce::AttributedString text;
        text.setWordWrap (juce::AttributedString::byWord);
        text.setLineSpacing (2.0f);
        text.append (presets.getEntries()[(size_t) current].description, font (FontKind::medium, 13.5f, 0.0f), col::inkDim);
        juce::TextLayout layout;
        layout.createLayout (text, info.getWidth(), info.getHeight());
        layout.draw (g, info);
    }

    g.setFont (capsFont (10.5f));
    g.setColour (col::inkFaint);
    g.drawText ("SAVE AS", juce::Rectangle<float> ((float) nameEdit.getX() + 2.0f, (float) nameEdit.getY() - 19.0f, 160.0f, 14.0f), juce::Justification::centredLeft, false);

    if (status.isNotEmpty())
    {
        g.setFont (font (FontKind::medium, 12.5f, 0.0f));
        g.setColour (statusIsError ? col::hot : col::violet);
        g.drawFittedText (status, statusArea, juce::Justification::topLeft, 2);
    }

    if (filtered.empty())
    {
        g.setFont (font (FontKind::medium, 14.0f, 0.0f));
        g.setColour (col::inkFaint);
        g.drawText (category == "*" ? "No favourites yet - click the heart next to a preset." : "No presets match.",
                    listArea, juce::Justification::centred, false);
    }
}

void PresetBrowser::resized()
{
    auto area = getLocalBounds().reduced (12, 4);
    area.removeFromBottom (8);
    area = area.reduced (22, 16);

    auto top = area.removeFromTop (34);
    closeButton.setBounds (top.removeFromRight (88).withSizeKeepingCentre (88, 32));
    area.removeFromTop (14);

    leftArea = area.removeFromLeft (220).reduced (0, 4);
    area.removeFromLeft (22);
    infoArea = area.removeFromRight (318);
    area.removeFromRight (24);

    {
        // every category at a readable height; when the window is too short for that
        // (compact view) the list scrolls instead of shrinking its type
        const int rows = juce::jmax (1, categories->getNumRows());
        const int rowH = juce::jlimit (27, 31, leftArea.getHeight() / rows);
        const bool scrolls = rowH * rows > leftArea.getHeight();

        categoryView.setBounds (leftArea);
        categories->setRowHeight (rowH);
        categories->setSize (leftArea.getWidth() - (scrolls ? categoryView.getScrollBarThickness() + 2 : 0), rowH * rows);
    }

    search.setBounds (area.removeFromTop (36));
    area.removeFromTop (14);
    listArea = area.reduced (0, 4);
    list.setBounds (listArea);

    // The save section is anchored to the bottom of the info column, above the tools; what
    // is left between it and the name of the playing preset shows that preset's description.
    auto column = infoArea;

    auto tools = column.removeFromBottom (32);
    const int w = (tools.getWidth() - 18) / 4;
    deleteButton.setBounds (tools.removeFromLeft (w));
    tools.removeFromLeft (6);
    importButton.setBounds (tools.removeFromLeft (w));
    tools.removeFromLeft (6);
    folderButton.setBounds (tools.removeFromLeft (w));
    tools.removeFromLeft (6);
    initButton.setBounds (tools);

    column.removeFromBottom (8);
    statusArea = column.removeFromBottom (34);
    column.removeFromBottom (4);

    auto saveRow = column.removeFromBottom (36);
    saveButton.setBounds (saveRow.removeFromRight (132));
    saveRow.removeFromRight (8);
    categoryButton.setBounds (saveRow);
    column.removeFromBottom (8);

    // the description field gives way first when the window is short (compact view)
    const int headerNeeds = 14 + 34 + 18 + 8 + 34;              // now playing, name, tag, two lines of description
    const int room = column.getHeight() - headerNeeds - 26 - 36 - 8;
    const int descriptionH = room >= 40 ? juce::jmin (66, room) : 0;

    descriptionEdit.setVisible (descriptionH > 0);

    if (descriptionH > 0)
    {
        descriptionEdit.setBounds (column.removeFromBottom (descriptionH));
        column.removeFromBottom (8);
    }

    nameEdit.setBounds (column.removeFromBottom (36));
}

} // namespace dlay::ui
