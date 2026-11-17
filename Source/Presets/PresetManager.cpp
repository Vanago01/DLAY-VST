#include "PresetManager.h"

namespace dlay
{

const juce::String PresetManager::userCategory { "User" };

namespace
{
    const juce::Identifier presetTag      { "DLAYPRESET" };
    const juce::Identifier settingsTag    { "DLAYSETTINGS" };
    const juce::Identifier propPresetName { "presetName" };
    const juce::Identifier propPresetCat  { "presetCategory" };
    const juce::Identifier propPresetMod  { "presetModified" };
}

PresetManager::PresetManager (juce::AudioProcessor& p, juce::AudioProcessorValueTreeState& state,
                              MotionCurves& curves, juce::UndoManager& undo)
    : processor (p), apvts (state), motionCurves (curves), undoManager (undo)
{
    for (auto* param : processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
            apvts.addParameterListener (ranged->getParameterID(), this);

    loadSettings();
    rescan();
}

PresetManager::~PresetManager()
{
    for (auto* param : processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
            apvts.removeParameterListener (ranged->getParameterID(), this);
}

juce::StringArray PresetManager::getCategories()
{
    juce::StringArray names;
    for (const auto* n : kCategoryNames)
        names.add (n);
    names.add (userCategory);
    return names;
}

// ----------------------------------------------------------------------------
//  library
// ----------------------------------------------------------------------------
juce::File PresetManager::getUserPresetDirectory() const
{
    auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);

   #if JUCE_MAC
    base = base.getChildFile ("Application Support");
   #endif

    return base.getChildFile ("DLAY Audio").getChildFile ("DLAY").getChildFile ("Presets");
}

juce::File PresetManager::getSettingsFile() const
{
    return getUserPresetDirectory().getParentDirectory().getChildFile ("settings.xml");
}

void PresetManager::rescan()
{
    entries.clear();

    const auto& factory = getFactoryPresets();
    numFactory = (int) factory.size();
    entries.reserve (factory.size() + 32);

    for (int i = 0; i < numFactory; ++i)
    {
        Entry e;
        e.name         = factory[(size_t) i].name;
        e.category     = kCategoryNames[(size_t) factory[(size_t) i].category];
        e.description  = factory[(size_t) i].description;
        e.isFactory    = true;
        e.factoryIndex = i;
        entries.push_back (std::move (e));
    }

    std::vector<Entry> user;
    const auto dir = getUserPresetDirectory();

    if (dir.isDirectory())
    {
        for (const auto& f : dir.findChildFiles (juce::File::findFiles, true, juce::String ("*") + fileExtension))
        {
            if (auto xml = juce::XmlDocument::parse (f))
            {
                Entry e;
                if (readPresetXml (*xml, e, nullptr, nullptr))
                {
                    e.isFactory = false;
                    e.file = f;
                    if (e.name.isEmpty()) e.name = f.getFileNameWithoutExtension();
                    user.push_back (std::move (e));
                }
            }
        }
    }

    std::sort (user.begin(), user.end(), [] (const Entry& a, const Entry& b) { return a.name.compareIgnoreCase (b.name) < 0; });
    for (auto& e : user)
        entries.push_back (std::move (e));

    // re-resolve the current preset by name
    currentIndex = -1;
    for (int i = 0; i < (int) entries.size(); ++i)
    {
        if (entries[(size_t) i].name == currentName && entries[(size_t) i].category == currentCategory)
        {
            currentIndex = i;
            break;
        }
    }

    sendChangeMessage();
}

std::vector<int> PresetManager::filter (const juce::String& search, const juce::String& category, bool favouritesOnly) const
{
    std::vector<int> result;
    const auto tokens = juce::StringArray::fromTokens (search.trim().toLowerCase(), " ", "");

    for (int i = 0; i < (int) entries.size(); ++i)
    {
        const auto& e = entries[(size_t) i];

        if (category.isNotEmpty() && e.category != category)
            continue;

        if (favouritesOnly && ! isFavourite (i))
            continue;

        bool match = true;
        const auto haystack = (e.name + " " + e.category + " " + e.description).toLowerCase();

        for (const auto& t : tokens)
            if (t.isNotEmpty() && ! haystack.contains (t)) { match = false; break; }

        if (match)
            result.push_back (i);
    }

    return result;
}

// ----------------------------------------------------------------------------
//  applying
// ----------------------------------------------------------------------------
void PresetManager::applyValues (const std::vector<std::pair<juce::String, float>>& values, const juce::ValueTree& motionTree,
                                 const std::vector<std::pair<int, std::vector<CurvePoint>>>* factoryCurves)
{
    loading = true;
    undoManager.beginNewTransaction ("Load preset");

    std::map<juce::String, float> lookup;
    for (const auto& v : values)
        lookup[v.first] = v.second;

    for (auto* param : processor.getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param);
        if (ranged == nullptr)
            continue;

        const auto paramId = ranged->getParameterID();

        if (isSessionParameter (paramId) || (mixLocked && paramId == id::mix))
            continue;

        const auto it = lookup.find (paramId);
        const auto& range = ranged->getNormalisableRange();

        const float norm = it != lookup.end() ? range.convertTo0to1 (range.snapToLegalValue (juce::jlimit (range.start, range.end, it->second)))
                                              : ranged->getDefaultValue();

        if (std::abs (norm - ranged->getValue()) > 1.0e-7f)
        {
            ranged->beginChangeGesture();
            ranged->setValueNotifyingHost (norm);
            ranged->endChangeGesture();
        }
    }

    if (factoryCurves != nullptr)
    {
        juce::ValueTree tree (MotionCurves::motionType);

        for (const auto& c : *factoryCurves)
        {
            juce::ValueTree laneTree (MotionCurves::laneType);
            laneTree.setProperty (MotionCurves::propLane, c.first, nullptr);

            for (const auto& pt : c.second)
            {
                juce::ValueTree point (MotionCurves::pointType);
                point.setProperty (MotionCurves::propX, pt.x, nullptr);
                point.setProperty (MotionCurves::propY, pt.y, nullptr);
                point.setProperty (MotionCurves::propCurve, pt.curve, nullptr);
                laneTree.appendChild (point, nullptr);
            }

            tree.appendChild (laneTree, nullptr);
        }

        motionCurves.restoreFrom (tree, true);
    }
    else
    {
        motionCurves.restoreFrom (motionTree, true);
    }

    apvts.copyState();                          // flushes the new values into the tree within this undo transaction
    undoManager.beginNewTransaction();

    loading = false;
    modified = false;

    if (onPresetApplied != nullptr)
        onPresetApplied();
}

void PresetManager::setCurrentNames (const juce::String& name, const juce::String& category)
{
    const juce::ScopedLock lock (currentLock);
    currentName     = name;
    currentCategory = category;
}

void PresetManager::setCurrent (int index, const juce::String& name, const juce::String& category)
{
    currentIndex = index;
    setCurrentNames (name, category);
    modified     = false;
    sendChangeMessage();
}

void PresetManager::load (int index)
{
    if (index < 0 || index >= (int) entries.size())
        return;

    const auto entry = entries[(size_t) index];

    if (entry.isFactory)
    {
        const auto& fp = getFactoryPresets()[(size_t) entry.factoryIndex];
        applyValues (fp.values, {}, &fp.curves);
    }
    else
    {
        auto xml = juce::XmlDocument::parse (entry.file);
        Entry info;
        std::vector<std::pair<juce::String, float>> values;
        juce::ValueTree motionTree;

        if (xml == nullptr || ! readPresetXml (*xml, info, &values, &motionTree))
            return;

        applyValues (values, motionTree, nullptr);
    }

    setCurrent (index, entry.name, entry.category);
}

void PresetManager::loadRelative (int delta)
{
    const int n = (int) entries.size();
    if (n == 0)
        return;

    int next = currentIndex < 0 ? (delta > 0 ? 0 : n - 1) : (currentIndex + delta) % n;
    if (next < 0) next += n;
    load (next);
}

void PresetManager::loadInit()
{
    applyValues ({}, {}, nullptr);
    setCurrent (-1, "Init", {});
}

// ----------------------------------------------------------------------------
//  user presets
// ----------------------------------------------------------------------------
std::unique_ptr<juce::XmlElement> PresetManager::createPresetXml (const juce::String& name, const juce::String& category,
                                                                  const juce::String& description) const
{
    auto xml = std::make_unique<juce::XmlElement> (presetTag);
    xml->setAttribute ("version", 1);
    xml->setAttribute ("plugin", "DLAY");
    xml->setAttribute ("name", name);
    xml->setAttribute ("category", category);
    xml->setAttribute ("description", description);

    for (auto* param : processor.getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param);
        if (ranged == nullptr || isSessionParameter (ranged->getParameterID()))
            continue;

        auto* e = xml->createNewChildElement ("P");
        e->setAttribute ("id", ranged->getParameterID());
        e->setAttribute ("v", (double) ranged->convertFrom0to1 (ranged->getValue()));
    }

    if (auto motionXml = motionCurves.createCopy().createXml())
        xml->addChildElement (motionXml.release());

    return xml;
}

bool PresetManager::readPresetXml (const juce::XmlElement& xml, Entry& info,
                                   std::vector<std::pair<juce::String, float>>* values, juce::ValueTree* motionTree)
{
    if (! xml.hasTagName (presetTag))
        return false;

    info.name        = xml.getStringAttribute ("name");
    info.category    = xml.getStringAttribute ("category", userCategory);
    info.description = xml.getStringAttribute ("description");

    if (info.category.isEmpty())
        info.category = userCategory;

    if (values != nullptr)
    {
        for (auto* e : xml.getChildWithTagNameIterator ("P"))
            values->emplace_back (e->getStringAttribute ("id"), (float) e->getDoubleAttribute ("v"));
    }

    if (motionTree != nullptr)
    {
        if (auto* m = xml.getChildByName (MotionCurves::motionType.toString()))
            *motionTree = juce::ValueTree::fromXml (*m);
    }

    return true;
}

juce::Result PresetManager::saveUserPreset (const juce::String& name, const juce::String& category, const juce::String& description)
{
    const auto cleanName = name.trim();
    if (cleanName.isEmpty())
        return juce::Result::fail ("Please give the preset a name.");

    const auto dir = getUserPresetDirectory();
    if (! dir.isDirectory() && ! dir.createDirectory().wasOk())
        return juce::Result::fail ("Could not create the preset folder:\n" + dir.getFullPathName());

    const auto cat  = category.isEmpty() ? userCategory : category;
    const auto file = dir.getChildFile (juce::File::createLegalFileName (cleanName) + fileExtension);
    const auto xml  = createPresetXml (cleanName, cat, description);

    if (! xml->writeTo (file))
        return juce::Result::fail ("Could not write the preset file:\n" + file.getFullPathName());

    setCurrentNames (cleanName, cat);
    modified = false;
    rescan();
    return juce::Result::ok();
}

bool PresetManager::deleteUserPreset (int index)
{
    if (index < 0 || index >= (int) entries.size() || entries[(size_t) index].isFactory)
        return false;

    const auto entry = entries[(size_t) index];
    favourites.removeString (favouriteKey (entry));

    if (! entry.file.deleteFile())
        return false;

    saveSettings();
    rescan();
    return true;
}

juce::Result PresetManager::importPresetFile (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    Entry info;

    if (xml == nullptr || ! readPresetXml (*xml, info, nullptr, nullptr))
        return juce::Result::fail ("This is not a DLAY preset file.");

    const auto dir = getUserPresetDirectory();
    if (! dir.isDirectory() && ! dir.createDirectory().wasOk())
        return juce::Result::fail ("Could not create the preset folder.");

    const auto target = dir.getNonexistentChildFile (file.getFileNameWithoutExtension(), fileExtension, false);

    if (! file.copyFileTo (target))
        return juce::Result::fail ("Could not copy the preset into the preset folder.");

    rescan();
    return juce::Result::ok();
}

// ----------------------------------------------------------------------------
//  favourites + settings
// ----------------------------------------------------------------------------
juce::String PresetManager::favouriteKey (const Entry& e) const
{
    return (e.isFactory ? "F:" : "U:") + e.name;
}

bool PresetManager::isFavourite (int index) const
{
    if (index < 0 || index >= (int) entries.size())
        return false;

    return favourites.contains (favouriteKey (entries[(size_t) index]));
}

void PresetManager::setFavourite (int index, bool shouldBeFavourite)
{
    if (index < 0 || index >= (int) entries.size())
        return;

    const auto key = favouriteKey (entries[(size_t) index]);

    if (shouldBeFavourite)  favourites.addIfNotAlreadyThere (key);
    else                    favourites.removeString (key);

    saveSettings();
    sendChangeMessage();
}

void PresetManager::setMixLocked (bool locked)
{
    mixLocked = locked;
    saveSettings();
}

void PresetManager::loadSettings()
{
    favourites.clear();

    if (auto xml = juce::XmlDocument::parse (getSettingsFile()))
    {
        if (xml->hasTagName (settingsTag))
        {
            mixLocked = xml->getBoolAttribute ("mixLock", false);

            for (auto* e : xml->getChildWithTagNameIterator ("FAV"))
                favourites.addIfNotAlreadyThere (e->getStringAttribute ("key"));
        }
    }
}

void PresetManager::saveSettings() const
{
    juce::XmlElement xml (settingsTag);
    xml.setAttribute ("mixLock", mixLocked);

    for (const auto& key : favourites)
        xml.createNewChildElement ("FAV")->setAttribute ("key", key);

    const auto file = getSettingsFile();
    file.getParentDirectory().createDirectory();
    xml.writeTo (file);
}

// ----------------------------------------------------------------------------
//  session state
// ----------------------------------------------------------------------------
void PresetManager::saveToState (juce::ValueTree& state) const
{
    state.setProperty (propPresetName, getCurrentName(), nullptr);
    state.setProperty (propPresetCat, getCurrentCategory(), nullptr);
    state.setProperty (propPresetMod, modified.load(), nullptr);
}

void PresetManager::restoreFromState (const juce::ValueTree& state)
{
    setCurrentNames (state.getProperty (propPresetName, "Init").toString(),
                     state.getProperty (propPresetCat, "").toString());
    currentIndex = -1;

    for (int i = 0; i < (int) entries.size(); ++i)
    {
        if (entries[(size_t) i].name == currentName
             && (currentCategory.isEmpty() || entries[(size_t) i].category == currentCategory))
        {
            currentIndex = i;
            break;
        }
    }

    modified = (bool) state.getProperty (propPresetMod, false);
    sendChangeMessage();
}

void PresetManager::parameterChanged (const juce::String& paramId, float)
{
    if (! loading.load() && ! isSessionParameter (paramId))
        modified = true;
}

} // namespace dlay
