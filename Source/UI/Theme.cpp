#include "Theme.h"
#include "DlayAssets.h"

namespace dlay::ui
{

// ============================================================================
//  shared assets
// ============================================================================
namespace
{
    struct SharedAssets
    {
        juce::Typeface::Ptr medium, semiBold, bold, heavy, mono;

        SharedAssets()
        {
            medium   = juce::Typeface::createSystemTypefaceFor (DlayAssets::GeistMedium_ttf,     (size_t) DlayAssets::GeistMedium_ttfSize);
            semiBold = juce::Typeface::createSystemTypefaceFor (DlayAssets::GeistSemiBold_ttf,   (size_t) DlayAssets::GeistSemiBold_ttfSize);
            bold     = juce::Typeface::createSystemTypefaceFor (DlayAssets::GeistBold_ttf,       (size_t) DlayAssets::GeistBold_ttfSize);
            heavy    = juce::Typeface::createSystemTypefaceFor (DlayAssets::GeistExtraBold_ttf,  (size_t) DlayAssets::GeistExtraBold_ttfSize);
            mono     = juce::Typeface::createSystemTypefaceFor (DlayAssets::GeistMonoMedium_ttf, (size_t) DlayAssets::GeistMonoMedium_ttfSize);
        }
    };

    using Assets = juce::SharedResourcePointer<SharedAssets>;

    constexpr float kPiF = juce::MathConstants<float>::pi;
}

juce::Font font (FontKind kind, float px, float tracking)
{
    Assets assets;
    juce::Typeface::Ptr tf = assets->medium;

    switch (kind)
    {
        case FontKind::semiBold: tf = assets->semiBold; break;
        case FontKind::bold:     tf = assets->bold;     break;
        case FontKind::heavy:    tf = assets->heavy;    break;
        case FontKind::mono:     tf = assets->mono;     break;
        case FontKind::medium:
        default:                 break;
    }

    return juce::Font (juce::FontOptions (tf).withPointHeight (px).withKerningFactor (tracking));
}

float textWidth (const juce::Font& f, const juce::String& text)
{
    return juce::GlyphArrangement::getStringWidth (f, text);
}

juce::String titleCase (const juce::String& text)
{
    juce::String out;
    out.preallocateBytes ((size_t) text.length() + 1);

    bool wordStart = true;

    for (auto p = text.getCharPointer(); ! p.isEmpty();)
    {
        const auto c = p.getAndAdvance();

        if (juce::CharacterFunctions::isLetter (c))
        {
            out << juce::String::charToString (wordStart ? juce::CharacterFunctions::toUpperCase (c)
                                                         : juce::CharacterFunctions::toLowerCase (c));
            wordStart = false;
        }
        else
        {
            out << juce::String::charToString (c);
            wordStart = true;
        }
    }

    return out;
}

struct AssetLifetime::Holder
{
    Assets assets;
};

AssetLifetime::AssetLifetime() : holder (std::make_unique<Holder>()) {}
AssetLifetime::~AssetLifetime() = default;

// ============================================================================
//  drawing helpers
// ============================================================================
juce::ColourGradient accentGradient (juce::Point<float> from, juce::Point<float> to, float alpha)
{
    juce::ColourGradient grad (col::pink.withAlpha (alpha), from.x, from.y, col::purple.withAlpha (alpha), to.x, to.y, false);
    grad.addColour (0.5, col::magenta.withAlpha (alpha));
    return grad;
}

juce::ColourGradient accentGradient (juce::Rectangle<float> across, float alpha)
{
    return accentGradient ({ across.getX(), across.getCentreY() }, { across.getRight(), across.getCentreY() }, alpha);
}

void drawPage (juce::Graphics& g, juce::Rectangle<float> full)
{
    g.setColour (col::page);
    g.fillRect (full);

    g.setGradientFill (juce::ColourGradient (col::pink.withAlpha (0.17f), full.getX(), full.getY(),
                                             col::pink.withAlpha (0.0f), full.getX() + 560.0f, full.getY(), true));
    g.fillRect (full);
    g.setGradientFill (juce::ColourGradient (col::purple.withAlpha (0.17f), full.getRight(), full.getBottom(),
                                             col::purple.withAlpha (0.0f), full.getRight() - 640.0f, full.getBottom(), true));
    g.fillRect (full);
}

namespace
{
    /** The shadow a rounded rectangle would cast if it were blurred by `reach` and moved down
        by `drop` - drawn as concentric outlines whose opacity follows the blur's falloff.

        A real blur of this size takes about a tenth of a second for the whole faceplate,
        and the faceplate is redrawn at every step while the window is being resized. This
        looks the same and takes a few milliseconds. Only the part that shows outside the
        rectangle is drawn. */
    void softShadow (juce::Graphics& g, juce::Rectangle<float> b, float corner, float drop, float reach,
                     juce::Colour colour, float strength)
    {
        const auto cast = b.translated (0.0f, drop);
        constexpr float step = 0.8f, width = 1.15f;
        const float gain = strength * step / width;             // neighbouring outlines overlap

        for (float x = reach; x >= -drop - 1.0f; x -= step)     // x: distance outside the cast outline
        {
            const float t = juce::jlimit (0.0f, 1.0f, (reach - x) / (2.0f * reach));
            const float alpha = gain * t * t * (3.0f - 2.0f * t);

            if (alpha < 0.002f)
                continue;

            g.setColour (colour.withAlpha (alpha));
            g.drawRoundedRectangle (cast.expanded (x), juce::jmax (1.0f, corner + x), width);
        }
    }
}

void drawCard (juce::Graphics& g, juce::Rectangle<float> b, float corner, bool withShadow)
{
    juce::Path shape;
    shape.addRoundedRectangle (b, corner);

    if (withShadow)
    {
        // a wide soft shadow and a tight one where the card meets the page
        softShadow (g, b, corner, 8.0f, 20.0f, juce::Colour (0xff3c3760), 0.15f);
        softShadow (g, b, corner, 1.0f, 4.0f, juce::Colour (0xff231f3a), 0.10f);
    }

    g.setGradientFill (juce::ColourGradient (juce::Colours::white, 0.0f, b.getY(), col::card, 0.0f, b.getY() + juce::jmin (120.0f, b.getHeight()), false));
    g.fillPath (shape);

    g.setColour (juce::Colours::white);
    g.drawRoundedRectangle (b.reduced (0.6f), corner - 0.6f, 1.0f);
    g.setColour (juce::Colour (0x12141020));
    g.drawRoundedRectangle (b, corner, 0.8f);
}

void drawSectionHeader (juce::Graphics& g, juce::Rectangle<float> b, const juce::String& index, const juce::String& title)
{
    const auto f = capsFont (11.5f);
    float x = b.getX();

    if (index.isNotEmpty())
    {
        const float iw = textWidth (f, index);
        drawAccentText (g, index, juce::Rectangle<float> (x, b.getY(), iw + 2.0f, b.getHeight()), juce::Justification::centredLeft, f);
        x += iw + 9.0f;
    }

    g.setFont (f);
    g.setColour (col::inkDim);
    g.drawText (title, juce::Rectangle<float> (x, b.getY(), b.getRight() - x, b.getHeight()), juce::Justification::centredLeft, false);
}

void drawLed (juce::Graphics& g, juce::Point<float> c, float r, juce::Colour colour, float brightness)
{
    brightness = juce::jlimit (0.0f, 1.0f, brightness);

    if (brightness > 0.02f)
    {
        const float halo = r * 3.0f;
        g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.38f * brightness), c.x, c.y,
                                                 colour.withAlpha (0.0f), c.x + halo, c.y, true));
        g.fillEllipse (c.x - halo, c.y - halo, 2.0f * halo, 2.0f * halo);
    }

    g.setColour (col::track.interpolatedWith (colour, brightness));
    g.fillEllipse (c.x - r, c.y - r, 2.0f * r, 2.0f * r);

    if (brightness > 0.3f)
    {
        g.setColour (juce::Colours::white.withAlpha (0.55f * brightness));
        g.fillEllipse (c.x - r * 0.45f, c.y - r * 0.55f, r * 0.6f, r * 0.45f);
    }
}

void drawScreen (juce::Graphics& g, juce::Rectangle<float> b, float corner, float haze)
{
    juce::Path shape;
    shape.addRoundedRectangle (b, corner);

    // lip of the cut-out in the card
    g.setColour (juce::Colours::white.withAlpha (0.9f));
    g.drawRoundedRectangle (b.expanded (0.9f), corner + 0.9f, 1.4f);

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0d0916), 0.0f, b.getY(), col::space, 0.0f, b.getBottom(), false));
    g.fillPath (shape);

    if (haze > 0.01f)
    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (shape);

        const float reach = juce::jmax (b.getWidth(), b.getHeight() * 1.6f);

        // magenta from above, violet from below right: the glow of the nebula behind the glass
        {
            const juce::Point<float> c (b.getX() + b.getWidth() * 0.42f, b.getY() - b.getHeight() * 0.35f);
            g.setGradientFill (juce::ColourGradient (col::haze.withAlpha (0.95f * haze), c.x, c.y,
                                                     col::haze.withAlpha (0.0f), c.x, c.y + reach * 0.78f, true));
            g.fillRect (b);
        }
        {
            const juce::Point<float> c (b.getRight() + b.getWidth() * 0.05f, b.getBottom() + b.getHeight() * 0.3f);
            g.setGradientFill (juce::ColourGradient (col::hazeViolet.withAlpha (0.85f * haze), c.x, c.y,
                                                     col::hazeViolet.withAlpha (0.0f), c.x - reach * 0.62f, c.y, true));
            g.fillRect (b);
        }
    }

    g.setColour (juce::Colours::white.withAlpha (0.09f));
    g.drawRoundedRectangle (b.reduced (0.6f), corner - 0.6f, 1.0f);
}

void drawStars (juce::Graphics& g, juce::Rectangle<float> b, juce::uint32 seed, int count, float brightness)
{
    juce::Random random ((juce::int64) seed);

    for (int i = 0; i < count; ++i)
    {
        const float x = b.getX() + random.nextFloat() * b.getWidth();
        const float y = b.getY() + random.nextFloat() * b.getHeight();
        const float depth = random.nextFloat();                         // most stars are far away
        const float size = 0.45f + 1.0f * depth * depth;
        const float alpha = (0.10f + 0.6f * depth * depth) * brightness;

        if ((i % 11) == 0)
        {
            const auto tint = (i % 22) == 0 ? col::pink : col::purple;
            const float halo = 3.0f + 4.0f * depth;
            g.setGradientFill (juce::ColourGradient (tint.withAlpha (0.30f * brightness), x, y, tint.withAlpha (0.0f), x + halo, y, true));
            g.fillEllipse (x - halo, y - halo, 2.0f * halo, 2.0f * halo);
        }

        g.setColour (col::star.withAlpha (juce::jlimit (0.0f, 1.0f, alpha)));
        g.fillEllipse (x - size, y - size, 2.0f * size, 2.0f * size);
    }
}

void drawGlowText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> b,
                   juce::Justification just, const juce::Font& f, juce::Colour colour)
{
    g.setFont (f);
    g.setColour (colour.withAlpha (0.14f));
    g.drawText (text, b.translated (0.0f, 1.0f), just, false);
    g.drawText (text, b.translated (1.0f, 0.0f), just, false);
    g.drawText (text, b.translated (-1.0f, 0.0f), just, false);
    g.setColour (colour);
    g.drawText (text, b, just, false);
}

void drawAccentText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> b,
                     juce::Justification just, const juce::Font& f)
{
    const float w = juce::jmin (b.getWidth(), textWidth (f, text));
    float x = b.getX();

    if (just.testFlags (juce::Justification::horizontallyCentred))  x = b.getCentreX() - 0.5f * w;
    else if (just.testFlags (juce::Justification::right))           x = b.getRight() - w;

    g.setFont (f);
    g.setGradientFill (accentGradient ({ x, b.getCentreY() }, { x + juce::jmax (1.0f, w), b.getCentreY() }));
    g.drawText (text, b, just, false);
}

void drawField (juce::Graphics& g, juce::Rectangle<float> b, float corner)
{
    g.setColour (col::cardLow);
    g.fillRoundedRectangle (b, corner);

    {
        // pressed in: a little shade under the upper edge
        juce::Graphics::ScopedSaveState state (g);
        juce::Path clip;
        clip.addRoundedRectangle (b, corner);
        g.reduceClipRegion (clip);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0x16201c38), 0.0f, b.getY(), juce::Colours::transparentBlack, 0.0f, b.getY() + 5.0f, false));
        g.fillRect (b.withHeight (6.0f));
    }

    g.setColour (col::line);
    g.drawRoundedRectangle (b.reduced (0.5f), corner - 0.5f, 1.0f);
}

void drawKnob (juce::Graphics& g, juce::Rectangle<float> bounds, float value, float centre,
               bool darkBody, bool highlighted, bool enabled)
{
    const float size = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto  c    = bounds.getCentre();
    const float R    = size * 0.5f;

    constexpr float startAngle = -0.75f * kPiF, endAngle = 0.75f * kPiF;
    const float angle = startAngle + juce::jlimit (0.0f, 1.0f, value) * (endAngle - startAngle);
    const float alpha = enabled ? 1.0f : 0.4f;

    // ---- the value arc around the dial
    {
        const float th = juce::jlimit (2.6f, 5.0f, R * 0.105f);
        const float rt = R - th * 0.5f - 1.0f;

        juce::Path track;
        track.addCentredArc (c.x, c.y, rt, rt, 0.0f, startAngle, endAngle, true);
        g.setColour (col::track.withMultipliedAlpha (alpha));
        g.strokePath (track, juce::PathStrokeType (th, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const float from = centre >= 0.0f ? startAngle + centre * (endAngle - startAngle) : startAngle;

        if (std::abs (angle - from) > 0.01f)
        {
            juce::Path arc;
            arc.addCentredArc (c.x, c.y, rt, rt, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);

            if (highlighted && enabled)
            {
                g.setGradientFill (accentGradient ({ c.x - R, c.y }, { c.x + R, c.y }, 0.28f));
                g.strokePath (arc, juce::PathStrokeType (th + 5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }

            g.setGradientFill (accentGradient ({ c.x - R, c.y }, { c.x + R, c.y }, alpha));
            g.strokePath (arc, juce::PathStrokeType (th, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        else if (centre >= 0.0f)
        {
            // a bipolar dial at rest: mark the middle
            const float sx = std::sin (from), cy = -std::cos (from);
            g.setColour (col::inkFaint.withMultipliedAlpha (alpha));
            g.fillEllipse (c.x + sx * rt - th * 0.5f, c.y + cy * rt - th * 0.5f, th, th);
        }
    }

    // ---- the dial
    const float rb = R * 0.70f;

    for (int i = 3; i >= 1; --i)        // soft shadow on the card
    {
        const float grow = (float) i * R * 0.045f;
        g.setColour (juce::Colour (0xff2a2548).withAlpha (darkBody ? 0.10f : 0.075f));
        g.fillEllipse (c.x - rb - grow, c.y - rb - grow + R * 0.13f, 2.0f * (rb + grow), 2.0f * (rb + grow));
    }

    if (darkBody)
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3d3a49), c.x, c.y - rb, juce::Colour (0xff15131c), c.x, c.y + rb, false));
    else
        g.setGradientFill (juce::ColourGradient (juce::Colours::white, c.x, c.y - rb, juce::Colour (0xffe1e1ea), c.x, c.y + rb, false));

    g.fillEllipse (c.x - rb, c.y - rb, 2.0f * rb, 2.0f * rb);

    // a flatter top face inside a bevelled edge
    {
        const float rf = rb * 0.86f;

        if (darkBody)
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2b2935), c.x, c.y - rf, juce::Colour (0xff211f2a), c.x, c.y + rf, false));
        else
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff4f4f8), c.x, c.y - rf, juce::Colours::white, c.x, c.y + rf, false));

        g.fillEllipse (c.x - rf, c.y - rf, 2.0f * rf, 2.0f * rf);
    }

    g.setColour (darkBody ? juce::Colours::black.withAlpha (0.55f) : juce::Colour (0xffc9c9d6).withAlpha (0.9f));
    g.drawEllipse (c.x - rb, c.y - rb, 2.0f * rb, 2.0f * rb, 0.8f);

    {
        juce::Path rim;
        rim.addCentredArc (c.x, c.y, rb - 0.8f, rb - 0.8f, 0.0f, -1.0f, 1.0f, true);
        g.setColour (juce::Colours::white.withAlpha (darkBody ? 0.22f : 0.95f));
        g.strokePath (rim, juce::PathStrokeType (1.0f));
    }

    // ---- the pointer: a single dot near the edge
    {
        const float sx = std::sin (angle), cy = -std::cos (angle);
        const float rd = juce::jmax (1.7f, rb * 0.095f);
        const juce::Point<float> p (c.x + sx * rb * 0.66f, c.y + cy * rb * 0.66f);

        g.setColour ((darkBody ? juce::Colours::white : col::ink).withAlpha (alpha));
        g.fillEllipse (p.x - rd, p.y - rd, 2.0f * rd, 2.0f * rd);
    }
}

// ============================================================================
//  look and feel
// ============================================================================
DlayLookAndFeel::DlayLookAndFeel()
{
    setColour (juce::PopupMenu::backgroundColourId, juce::Colours::white);
    setColour (juce::PopupMenu::textColourId, col::ink);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, col::cardLow);
    setColour (juce::PopupMenu::highlightedTextColourId, col::ink);
    setColour (juce::PopupMenu::headerTextColourId, col::inkFaint);

    setColour (juce::TextEditor::backgroundColourId, col::cardLow);
    setColour (juce::TextEditor::textColourId, col::ink);
    setColour (juce::TextEditor::highlightColourId, col::purple.withAlpha (0.28f));
    setColour (juce::TextEditor::highlightedTextColourId, col::ink);
    setColour (juce::TextEditor::outlineColourId, col::line);
    setColour (juce::TextEditor::focusedOutlineColourId, col::violet);
    setColour (juce::CaretComponent::caretColourId, col::violet);

    setColour (juce::TextButton::buttonColourId, juce::Colours::white);
    setColour (juce::TextButton::buttonOnColourId, col::dark);
    setColour (juce::TextButton::textColourOffId, col::ink);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);

    setColour (juce::ScrollBar::thumbColourId, col::track);
    setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::TooltipWindow::backgroundColourId, col::dark);
    setColour (juce::TooltipWindow::textColourId, col::star);
    setColour (juce::Label::textColourId, col::ink);
}

// ---- popup menus -------------------------------------------------------------
juce::Font DlayLookAndFeel::getPopupMenuFont()
{
    return font (FontKind::medium, 14.5f * uiScale, 0.0f);
}

void DlayLookAndFeel::getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int,
                                                 int& idealWidth, int& idealHeight)
{
    if (isSeparator)
    {
        idealWidth = 50;
        idealHeight = juce::roundToInt (11.0f * uiScale);
        return;
    }

    const auto f = getPopupMenuFont();
    idealHeight = juce::roundToInt (32.0f * uiScale);
    idealWidth  = juce::roundToInt (textWidth (f, text) + 70.0f * uiScale);
}

void DlayLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.fillAll (juce::Colours::white);
    g.setColour (col::line);
    g.drawRect (0, 0, width, height, 1);
    g.setGradientFill (accentGradient (juce::Rectangle<float> (0.0f, 0.0f, (float) width, 2.0f)));
    g.fillRect (0, 0, width, juce::jmax (2, juce::roundToInt (2.5f * uiScale)));
}

void DlayLookAndFeel::drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& sectionName)
{
    g.setFont (capsFont (11.0f * uiScale));
    g.setColour (col::inkFaint);
    g.drawText (sectionName.toUpperCase(), area.reduced (juce::roundToInt (14.0f * uiScale), 0), juce::Justification::centredLeft, true);
}

void DlayLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                                         bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                                         const juce::String& shortcutKeyText, const juce::Drawable*, const juce::Colour*)
{
    if (isSeparator)
    {
        g.setColour (col::line);
        g.fillRect (area.reduced (juce::roundToInt (12.0f * uiScale), 0).withSizeKeepingCentre (area.getWidth() - 24, 1));
        return;
    }

    auto r = area.toFloat();
    const bool lit = isHighlighted && isActive;

    if (lit)
    {
        g.setColour (col::cardLow);
        g.fillRoundedRectangle (r.reduced (5.0f * uiScale, 1.5f * uiScale), 7.0f * uiScale);
    }

    const float pad = 14.0f * uiScale;

    if (isTicked)
    {
        const float rd = 4.0f * uiScale;
        const juce::Point<float> c (r.getX() + pad + rd, r.getCentreY());
        g.setGradientFill (accentGradient ({ c.x - rd, c.y }, { c.x + rd, c.y }));
        g.fillEllipse (c.x - rd, c.y - rd, 2.0f * rd, 2.0f * rd);
    }

    g.setFont (isTicked ? font (FontKind::semiBold, 14.5f * uiScale, 0.0f) : getPopupMenuFont());
    g.setColour (isActive ? col::ink : col::inkFaint);
    g.drawText (text, r.withTrimmedLeft (pad + 18.0f * uiScale).withTrimmedRight (pad), juce::Justification::centredLeft, true);

    if (shortcutKeyText.isNotEmpty())
    {
        g.setFont (monoFont (12.0f * uiScale));
        g.setColour (col::inkFaint);
        g.drawText (shortcutKeyText, r.withTrimmedRight (pad), juce::Justification::centredRight, true);
    }

    if (hasSubMenu)
    {
        juce::Path arrow;
        const float h = 4.5f * uiScale;
        const float x = r.getRight() - pad;
        arrow.startNewSubPath (x - h, r.getCentreY() - h);
        arrow.lineTo (x, r.getCentreY());
        arrow.lineTo (x - h, r.getCentreY() + h);
        g.setColour (col::inkDim);
        g.strokePath (arrow, juce::PathStrokeType (1.6f * uiScale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

// ---- tooltips ------------------------------------------------------------------
juce::TextLayout DlayLookAndFeel::layoutTooltip (const juce::String& text) const
{
    juce::AttributedString s;
    s.setJustification (juce::Justification::centredLeft);
    s.append (text, font (FontKind::medium, 13.5f * uiScale, 0.0f), col::star);

    juce::TextLayout layout;
    layout.createLayoutWithBalancedLineLengths (s, 360.0f * uiScale);
    return layout;
}

juce::Rectangle<int> DlayLookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                                        juce::Rectangle<int> parentArea)
{
    const auto layout = layoutTooltip (tipText);
    const int w = (int) std::ceil (layout.getWidth() + 28.0f * uiScale);
    const int h = (int) std::ceil (layout.getHeight() + 18.0f * uiScale);

    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6,
                                 w, h)
             .constrainedWithin (parentArea);
}

void DlayLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    const auto b = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);
    const float corner = 9.0f * uiScale;

    g.setColour (col::dark.withAlpha (0.97f));
    g.fillRoundedRectangle (b, corner);
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawRoundedRectangle (b.reduced (0.5f), corner, 1.0f);

    layoutTooltip (text).draw (g, b.reduced (14.0f * uiScale, 9.0f * uiScale));
}

// ---- misc ------------------------------------------------------------------------
void DlayLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height,
                                     bool isScrollbarVertical, int thumbStartPosition, int thumbSize, bool isMouseOver, bool isMouseDown)
{
    juce::ignoreUnused (height);

    if (thumbSize > 0)
    {
        const auto thumb = isScrollbarVertical ? juce::Rectangle<float> ((float) x + 2.5f, (float) thumbStartPosition, (float) width - 5.0f, (float) thumbSize)
                                               : juce::Rectangle<float> ((float) thumbStartPosition, (float) y + 2.5f, (float) thumbSize, (float) height - 5.0f);
        g.setColour ((isMouseOver || isMouseDown) ? col::inkFaint : col::track);
        g.fillRoundedRectangle (thumb, juce::jmin (thumb.getWidth(), thumb.getHeight()) * 0.5f);
    }
}

void DlayLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor&)
{
    drawField (g, juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (1.0f), 9.0f);
}

void DlayLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    if (editor.hasKeyboardFocus (true))
    {
        const auto b = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (1.0f);
        g.setGradientFill (accentGradient (b));
        g.drawRoundedRectangle (b, 9.0f, 1.6f);
    }
}

juce::Font DlayLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return font (FontKind::semiBold, juce::jmin (13.5f, (float) buttonHeight * 0.42f), 0.02f);
}

void DlayLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                            bool highlighted, bool down)
{
    const auto b = button.getLocalBounds().toFloat().reduced (1.5f);
    const float corner = b.getHeight() * 0.5f;
    const bool on = button.getToggleState();

    g.setColour (juce::Colour (0x18201c38));
    g.fillRoundedRectangle (b.translated (0.0f, 1.5f), corner);

    g.setColour (on || down ? col::dark : (highlighted ? col::cardLow : juce::Colours::white));
    g.fillRoundedRectangle (b, corner);
    g.setColour (on || down ? col::dark : col::line);
    g.drawRoundedRectangle (b.reduced (0.5f), corner, 1.0f);
}

void DlayLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool down)
{
    const bool on = button.getToggleState();
    g.setFont (getTextButtonFont (button, button.getHeight()));
    g.setColour ((on || down ? juce::Colours::white : col::ink).withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.4f));
    g.drawText (button.getButtonText(), button.getLocalBounds(), juce::Justification::centred, false);
}

void DlayLookAndFeel::drawCornerResizer (juce::Graphics& g, int w, int h, bool isMouseOver, bool isMouseDragging)
{
    // a small triangle of dots in the corner
    const float step = (float) juce::jmin (w, h) * 0.26f, r = step * 0.24f;
    g.setColour ((isMouseOver || isMouseDragging) ? col::inkDim : col::inkFaint.withAlpha (0.75f));

    for (int row = 0; row < 3; ++row)
        for (int column = 2 - row; column < 3; ++column)
            g.fillEllipse ((float) w - step * (float) (3 - column) - r + step * 0.3f,
                           (float) h - step * (float) (3 - row) - r + step * 0.3f, 2.0f * r, 2.0f * r);
}

} // namespace dlay::ui
