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
        juce::Typeface::Ptr medium, semiBold, bold, mono;
        juce::Image brushed;

        SharedAssets()
        {
            medium   = juce::Typeface::createSystemTypefaceFor (DlayAssets::BarlowCondensedMedium_ttf,   (size_t) DlayAssets::BarlowCondensedMedium_ttfSize);
            semiBold = juce::Typeface::createSystemTypefaceFor (DlayAssets::BarlowCondensedSemiBold_ttf, (size_t) DlayAssets::BarlowCondensedSemiBold_ttfSize);
            bold     = juce::Typeface::createSystemTypefaceFor (DlayAssets::BarlowCondensedBold_ttf,     (size_t) DlayAssets::BarlowCondensedBold_ttfSize);
            mono     = juce::Typeface::createSystemTypefaceFor (DlayAssets::ShareTechMonoRegular_ttf,    (size_t) DlayAssets::ShareTechMonoRegular_ttfSize);

            // brushed metal: horizontal streaks of slightly lighter / darker grain
            constexpr int size = 256;
            brushed = juce::Image (juce::Image::ARGB, size, size, true);
            juce::Image::BitmapData data (brushed, juce::Image::BitmapData::writeOnly);
            juce::Random random (0xD1A7);

            for (int y = 0; y < size; ++y)
            {
                const float row = random.nextFloat() * 2.0f - 1.0f;
                float streak = 0.0f;

                for (int x = 0; x < size; ++x)
                {
                    streak += 0.08f * ((random.nextFloat() * 2.0f - 1.0f) - streak);
                    const float v = juce::jlimit (-1.0f, 1.0f, 0.55f * row + 2.2f * streak);
                    const auto alpha = (juce::uint8) juce::jlimit (0.0f, 255.0f, std::abs (v) * 20.0f);
                    data.setPixelColour (x, y, (v > 0.0f ? juce::Colours::white : juce::Colours::black).withAlpha (alpha));
                }
            }
        }
    };

    using Assets = juce::SharedResourcePointer<SharedAssets>;

    constexpr float kPiF = juce::MathConstants<float>::pi;
}

juce::Font font (FontKind kind, float height, float tracking)
{
    Assets assets;
    juce::Typeface::Ptr tf = assets->medium;

    switch (kind)
    {
        case FontKind::semiBold: tf = assets->semiBold; break;
        case FontKind::bold:     tf = assets->bold;     break;
        case FontKind::mono:     tf = assets->mono;     break;
        case FontKind::medium:
        default:                 break;
    }

    return juce::Font (juce::FontOptions (tf).withHeight (height).withKerningFactor (tracking));
}

float textWidth (const juce::Font& f, const juce::String& text)
{
    return juce::GlyphArrangement::getStringWidth (f, text);
}

juce::Image brushedTexture()
{
    Assets assets;
    return assets->brushed;         // images are reference counted: this is a cheap handle copy
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
void drawScrew (juce::Graphics& g, juce::Point<float> c, float r, float angle)
{
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillEllipse (c.x - r - 0.6f, c.y - r + 0.2f, 2.0f * r + 1.2f, 2.0f * r + 1.2f);

    juce::ColourGradient grad (juce::Colour (0xff5b6066), c.x, c.y - r, juce::Colour (0xff23262a), c.x, c.y + r, false);
    g.setGradientFill (grad);
    g.fillEllipse (c.x - r, c.y - r, 2.0f * r, 2.0f * r);

    g.setColour (juce::Colours::black.withAlpha (0.75f));
    const float dx = std::cos (angle) * r * 0.72f, dy = std::sin (angle) * r * 0.72f;
    g.drawLine (c.x - dx, c.y - dy, c.x + dx, c.y + dy, juce::jmax (1.0f, r * 0.34f));

    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawEllipse (c.x - r, c.y - r, 2.0f * r, 2.0f * r, 0.6f);
}

void drawPanel (juce::Graphics& g, juce::Rectangle<float> b, bool screws)
{
    constexpr float corner = 5.0f;

    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (b.translated (0.0f, 1.5f), corner);

    g.setColour (col::panel);
    g.fillRoundedRectangle (b, corner);

    {
        juce::Graphics::ScopedSaveState state (g);
        juce::Path clip;
        clip.addRoundedRectangle (b, corner);
        g.reduceClipRegion (clip);
        g.setTiledImageFill (brushedTexture(), 0, 0, 0.5f);
        g.fillRect (b);

        // a very light vertical sheen, as if lit from above
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.035f), b.getX(), b.getY(),
                                                 juce::Colours::transparentWhite, b.getX(), b.getY() + b.getHeight() * 0.45f, false));
        g.fillRect (b);
    }

    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.drawRoundedRectangle (b.reduced (0.6f), corner, 0.8f);
    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.drawRoundedRectangle (b, corner, 1.0f);

    if (screws)
    {
        constexpr float inset = 8.5f, r = 2.6f;
        drawScrew (g, { b.getX() + inset, b.getY() + inset }, r, 0.6f);
        drawScrew (g, { b.getRight() - inset, b.getY() + inset }, r, 2.1f);
        drawScrew (g, { b.getX() + inset, b.getBottom() - inset }, r, 1.3f);
        drawScrew (g, { b.getRight() - inset, b.getBottom() - inset }, r, 2.8f);
    }
}

void drawSectionHeader (juce::Graphics& g, juce::Rectangle<float> b, const juce::String& index, const juce::String& title)
{
    const auto idxFont   = monoFont (9.5f);
    const auto titleFont = font (FontKind::semiBold, 12.0f, 0.16f);

    float x = b.getX();

    g.setColour (col::orange);
    g.setFont (idxFont);
    g.drawText (index, juce::Rectangle<float> (x, b.getY(), 20.0f, b.getHeight()), juce::Justification::centredLeft, false);
    x += textWidth (idxFont, index) + 6.0f;

    g.setColour (col::cream);
    g.setFont (titleFont);
    const float tw = textWidth (titleFont, title);
    g.drawText (title, juce::Rectangle<float> (x, b.getY(), tw + 4.0f, b.getHeight()), juce::Justification::centredLeft, false);
    x += tw + 9.0f;

    if (x < b.getRight() - 4.0f)
    {
        const float y = b.getCentreY() + 0.5f;
        g.setColour (col::creamFaint.withAlpha (0.55f));
        g.drawLine (x, y, b.getRight(), y, 1.0f);
    }
}

void drawLed (juce::Graphics& g, juce::Point<float> c, float r, juce::Colour colour, float brightness)
{
    brightness = juce::jlimit (0.0f, 1.0f, brightness);

    if (brightness > 0.02f)
    {
        const float halo = r * 3.4f;
        g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.42f * brightness), c.x, c.y,
                                                 colour.withAlpha (0.0f), c.x + halo, c.y, true));
        g.fillEllipse (c.x - halo, c.y - halo, 2.0f * halo, 2.0f * halo);
    }

    g.setColour (juce::Colours::black);
    g.fillEllipse (c.x - r - 1.0f, c.y - r - 1.0f, 2.0f * r + 2.0f, 2.0f * r + 2.0f);

    const auto lit = colour.interpolatedWith (juce::Colours::white, 0.25f * brightness);
    g.setColour (colour.withMultipliedBrightness (0.22f).interpolatedWith (lit, brightness));
    g.fillEllipse (c.x - r, c.y - r, 2.0f * r, 2.0f * r);

    g.setColour (juce::Colours::white.withAlpha (0.18f + 0.35f * brightness));
    g.fillEllipse (c.x - r * 0.5f, c.y - r * 0.62f, r * 0.7f, r * 0.5f);
}

void drawScreen (juce::Graphics& g, juce::Rectangle<float> b, float corner)
{
    g.setColour (col::screen);
    g.fillRoundedRectangle (b, corner);

    {
        juce::Graphics::ScopedSaveState state (g);
        juce::Path clip;
        clip.addRoundedRectangle (b, corner);
        g.reduceClipRegion (clip);

        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.75f), b.getX(), b.getY(),
                                                 juce::Colours::transparentBlack, b.getX(), b.getY() + juce::jmin (10.0f, b.getHeight() * 0.4f), false));
        g.fillRect (b);
    }

    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (b, corner, 1.2f);
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.drawRoundedRectangle (b.expanded (0.9f), corner + 0.9f, 0.8f);
}

void drawGlowText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> b,
                   juce::Justification just, const juce::Font& f, juce::Colour colour)
{
    g.setFont (f);
    g.setColour (colour.withAlpha (0.16f));
    g.drawText (text, b.translated (0.0f, 0.8f), just, false);
    g.drawText (text, b.translated (0.8f, 0.0f), just, false);
    g.drawText (text, b.translated (-0.8f, 0.0f), just, false);
    g.setColour (colour);
    g.drawText (text, b, just, false);
}

void drawKnob (juce::Graphics& g, juce::Rectangle<float> bounds, float value, float centre,
               juce::Colour accent, bool highlighted, bool enabled)
{
    const float size = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto  c    = bounds.getCentre();
    const float R    = size * 0.5f;

    constexpr float startAngle = -0.75f * kPiF, endAngle = 0.75f * kPiF;
    const float angle = startAngle + juce::jlimit (0.0f, 1.0f, value) * (endAngle - startAngle);
    const float alpha = enabled ? 1.0f : 0.4f;

    // ---- scale markings
    const int numTicks = size > 70.0f ? 31 : (size > 46.0f ? 21 : 11);

    for (int i = 0; i < numTicks; ++i)
    {
        const float a = startAngle + (float) i / (float) (numTicks - 1) * (endAngle - startAngle);
        const bool major = (i % 5) == 0;
        const float r0 = R * (major ? 0.86f : 0.90f), r1 = R * 0.985f;
        const float sx = std::sin (a), cy = -std::cos (a);

        g.setColour ((major ? col::creamDim : col::creamFaint).withAlpha (alpha * (major ? 0.95f : 0.7f)));
        g.drawLine (c.x + sx * r0, c.y + cy * r0, c.x + sx * r1, c.y + cy * r1, major ? 1.1f : 0.8f);
    }

    // ---- value arc in a recessed track
    {
        const float rt = R * 0.775f;
        const float th = juce::jmax (1.8f, R * 0.075f);

        juce::Path track;
        track.addCentredArc (c.x, c.y, rt, rt, 0.0f, startAngle, endAngle, true);
        g.setColour (col::groove);
        g.strokePath (track, juce::PathStrokeType (th + 1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (juce::Colour (0xff24272b));
        g.strokePath (track, juce::PathStrokeType (th, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const float from = centre >= 0.0f ? startAngle + centre * (endAngle - startAngle) : startAngle;

        if (std::abs (angle - from) > 0.01f)
        {
            juce::Path arc;
            arc.addCentredArc (c.x, c.y, rt, rt, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
            g.setColour (accent.withAlpha (0.25f * alpha));
            g.strokePath (arc, juce::PathStrokeType (th + 3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (accent.withAlpha (alpha));
            g.strokePath (arc, juce::PathStrokeType (th, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    // ---- body
    const float rb = R * 0.62f;

    for (int i = 3; i >= 1; --i)        // soft contact shadow
    {
        const float grow = (float) i * R * 0.035f;
        g.setColour (juce::Colours::black.withAlpha (0.16f));
        g.fillEllipse (c.x - rb - grow, c.y - rb - grow + R * 0.09f, 2.0f * (rb + grow), 2.0f * (rb + grow));
    }

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3e44), c.x, c.y - rb, juce::Colour (0xff0e0f11), c.x, c.y + rb, false));
    g.fillEllipse (c.x - rb, c.y - rb, 2.0f * rb, 2.0f * rb);

    // knurled grip
    {
        const int teeth = juce::jlimit (24, 72, (int) (rb * 1.6f));
        for (int i = 0; i < teeth; ++i)
        {
            const float a = angle + (float) i / (float) teeth * 2.0f * kPiF;
            const float sx = std::sin (a), cy = -std::cos (a);
            g.setColour (((i & 1) != 0 ? juce::Colours::white : juce::Colours::black).withAlpha ((i & 1) != 0 ? 0.09f : 0.35f));
            g.drawLine (c.x + sx * rb * 0.86f, c.y + cy * rb * 0.86f, c.x + sx * rb * 0.985f, c.y + cy * rb * 0.985f, 1.0f);
        }
    }

    // cap
    const float rc = rb * 0.82f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2e3136), c.x, c.y - rc, juce::Colour (0xff17191c), c.x, c.y + rc, false));
    g.fillEllipse (c.x - rc, c.y - rc, 2.0f * rc, 2.0f * rc);

    {
        // spun-metal sheen: faint radial strokes that turn with the knob
        juce::Random pattern (0x5eed);
        for (int i = 0; i < 28; ++i)
        {
            const float a = angle + (float) i / 28.0f * 2.0f * kPiF + pattern.nextFloat() * 0.12f;
            const float sx = std::sin (a), cy = -std::cos (a);
            g.setColour (juce::Colours::white.withAlpha (0.018f + 0.035f * pattern.nextFloat()));
            g.drawLine (c.x + sx * rc * 0.12f, c.y + cy * rc * 0.12f, c.x + sx * rc * 0.96f, c.y + cy * rc * 0.96f, 1.0f);
        }
    }

    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawEllipse (c.x - rc, c.y - rc, 2.0f * rc, 2.0f * rc, 1.0f);

    {
        juce::Path rim;
        rim.addCentredArc (c.x, c.y, rb - 0.6f, rb - 0.6f, 0.0f, -0.9f, 0.9f, true);
        g.setColour (juce::Colours::white.withAlpha (0.20f));
        g.strokePath (rim, juce::PathStrokeType (1.0f));
    }

    // pointer
    {
        const float sx = std::sin (angle), cy = -std::cos (angle);
        const auto pointer = highlighted ? col::cream.interpolatedWith (accent, 0.35f) : col::cream;
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawLine (c.x + sx * rc * 0.34f, c.y + cy * rc * 0.34f + 0.8f, c.x + sx * rc * 0.92f, c.y + cy * rc * 0.92f + 0.8f, juce::jmax (1.6f, rc * 0.15f));
        g.setColour (pointer.withAlpha (alpha));
        g.drawLine (c.x + sx * rc * 0.34f, c.y + cy * rc * 0.34f, c.x + sx * rc * 0.92f, c.y + cy * rc * 0.92f, juce::jmax (1.4f, rc * 0.13f));
    }
}

// ============================================================================
//  look and feel
// ============================================================================
DlayLookAndFeel::DlayLookAndFeel()
{
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff121416));
    setColour (juce::PopupMenu::textColourId, col::cream);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, col::orange);
    setColour (juce::PopupMenu::highlightedTextColourId, col::ink);
    setColour (juce::PopupMenu::headerTextColourId, col::amber);

    setColour (juce::TextEditor::backgroundColourId, col::screen);
    setColour (juce::TextEditor::textColourId, col::cream);
    setColour (juce::TextEditor::highlightColourId, col::orange.withAlpha (0.55f));
    setColour (juce::TextEditor::highlightedTextColourId, col::cream);
    setColour (juce::TextEditor::outlineColourId, col::edge);
    setColour (juce::TextEditor::focusedOutlineColourId, col::amber);
    setColour (juce::CaretComponent::caretColourId, col::amber);

    setColour (juce::TextButton::buttonColourId, col::panelHi);
    setColour (juce::TextButton::buttonOnColourId, col::orange);
    setColour (juce::TextButton::textColourOffId, col::cream);
    setColour (juce::TextButton::textColourOnId, col::ink);

    setColour (juce::ScrollBar::thumbColourId, col::steel);
    setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff121416));
    setColour (juce::TooltipWindow::textColourId, col::cream);
    setColour (juce::Label::textColourId, col::cream);
}

// ---- popup menus -------------------------------------------------------------
juce::Font DlayLookAndFeel::getPopupMenuFont()
{
    return font (FontKind::semiBold, 14.5f * uiScale, 0.07f);
}

void DlayLookAndFeel::getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int,
                                                 int& idealWidth, int& idealHeight)
{
    if (isSeparator)
    {
        idealWidth = 50;
        idealHeight = juce::roundToInt (9.0f * uiScale);
        return;
    }

    const auto f = getPopupMenuFont();
    idealHeight = juce::roundToInt (25.0f * uiScale);
    idealWidth  = juce::roundToInt (textWidth (f, text) + 58.0f * uiScale);
}

void DlayLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.fillAll (juce::Colour (0xff121416));
    g.setColour (col::edge);
    g.drawRect (0, 0, width, height, 1);
    g.setColour (col::orange.withAlpha (0.8f));
    g.fillRect (0, 0, width, juce::jmax (1, juce::roundToInt (2.0f * uiScale)));
}

void DlayLookAndFeel::drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& sectionName)
{
    g.setFont (font (FontKind::semiBold, 11.5f * uiScale, 0.2f));
    g.setColour (col::amber);
    g.drawText (sectionName.toUpperCase(), area.reduced (juce::roundToInt (12.0f * uiScale), 0), juce::Justification::centredLeft, true);
}

void DlayLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                                         bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                                         const juce::String& shortcutKeyText, const juce::Drawable*, const juce::Colour*)
{
    if (isSeparator)
    {
        g.setColour (col::edge);
        g.fillRect (area.reduced (juce::roundToInt (10.0f * uiScale), 0).withSizeKeepingCentre (area.getWidth() - 20, 1));
        return;
    }

    auto r = area.toFloat();
    const bool lit = isHighlighted && isActive;

    if (lit)
    {
        g.setColour (col::orange);
        g.fillRect (r.reduced (3.0f * uiScale, 1.0f));
    }

    const float pad = 12.0f * uiScale;
    const float ledX = r.getX() + pad;

    if (isTicked)
        drawLed (g, { ledX + 3.0f * uiScale, r.getCentreY() }, 3.2f * uiScale, lit ? col::ink : col::amber, 1.0f);

    g.setFont (getPopupMenuFont());
    g.setColour (lit ? col::ink : (isActive ? col::cream : col::creamFaint));
    g.drawText (text, r.withTrimmedLeft (pad + 16.0f * uiScale).withTrimmedRight (pad), juce::Justification::centredLeft, true);

    if (shortcutKeyText.isNotEmpty())
    {
        g.setFont (monoFont (11.0f * uiScale));
        g.setColour (lit ? col::ink : col::creamFaint);
        g.drawText (shortcutKeyText, r.withTrimmedRight (pad), juce::Justification::centredRight, true);
    }

    if (hasSubMenu)
    {
        juce::Path arrow;
        const float h = 4.0f * uiScale;
        const float x = r.getRight() - pad;
        arrow.addTriangle (x - h, r.getCentreY() - h, x - h, r.getCentreY() + h, x, r.getCentreY());
        g.setColour (lit ? col::ink : col::creamDim);
        g.fillPath (arrow);
    }
}

// ---- tooltips ------------------------------------------------------------------
juce::TextLayout DlayLookAndFeel::layoutTooltip (const juce::String& text) const
{
    juce::AttributedString s;
    s.setJustification (juce::Justification::centredLeft);
    s.append (text, font (FontKind::medium, 13.5f * uiScale, 0.03f), col::cream);

    juce::TextLayout layout;
    layout.createLayoutWithBalancedLineLengths (s, 290.0f * uiScale);
    return layout;
}

juce::Rectangle<int> DlayLookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                                        juce::Rectangle<int> parentArea)
{
    const auto layout = layoutTooltip (tipText);
    const int w = (int) std::ceil (layout.getWidth() + 22.0f * uiScale);
    const int h = (int) std::ceil (layout.getHeight() + 14.0f * uiScale);

    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6,
                                 w, h)
             .constrainedWithin (parentArea);
}

void DlayLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    const auto b = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);

    g.setColour (juce::Colour (0xf2121416));
    g.fillRoundedRectangle (b, 3.0f * uiScale);
    g.setColour (col::edge);
    g.drawRoundedRectangle (b.reduced (0.5f), 3.0f * uiScale, 1.0f);
    g.setColour (col::amber);
    g.fillRect (juce::Rectangle<float> (0.0f, 3.0f * uiScale, 2.0f * uiScale, (float) height - 6.0f * uiScale));

    layoutTooltip (text).draw (g, b.reduced (11.0f * uiScale, 7.0f * uiScale));
}

// ---- misc ------------------------------------------------------------------------
void DlayLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height,
                                     bool isScrollbarVertical, int thumbStartPosition, int thumbSize, bool isMouseOver, bool isMouseDown)
{
    g.setColour (col::groove.withAlpha (0.7f));
    g.fillRoundedRectangle ((float) x + 1.0f, (float) y, (float) width - 2.0f, (float) height, 3.0f);

    if (thumbSize > 0)
    {
        const auto thumb = isScrollbarVertical ? juce::Rectangle<float> ((float) x + 2.0f, (float) thumbStartPosition, (float) width - 4.0f, (float) thumbSize)
                                               : juce::Rectangle<float> ((float) thumbStartPosition, (float) y + 2.0f, (float) thumbSize, (float) height - 4.0f);
        g.setColour ((isMouseOver || isMouseDown) ? col::orange : col::steel.withAlpha (0.8f));
        g.fillRoundedRectangle (thumb, 2.5f);
    }
}

void DlayLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor&)
{
    drawScreen (g, juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (1.0f), 3.0f);
}

void DlayLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    if (editor.hasKeyboardFocus (true))
    {
        g.setColour (col::amber.withAlpha (0.8f));
        g.drawRoundedRectangle (juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (1.0f), 3.0f, 1.0f);
    }
}

juce::Font DlayLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return font (FontKind::semiBold, juce::jmin (13.0f, (float) buttonHeight * 0.55f), 0.12f);
}

void DlayLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                            bool highlighted, bool down)
{
    const auto b = button.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = button.getToggleState();

    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.fillRoundedRectangle (b.translated (0.0f, 1.0f), 3.0f);

    auto base = on ? col::orange : (down ? juce::Colour (0xff101214) : (highlighted ? juce::Colour (0xff2a2e33) : col::panelHi));
    g.setGradientFill (juce::ColourGradient (base.brighter (0.08f), 0.0f, b.getY(), base.darker (0.12f), 0.0f, b.getBottom(), false));
    g.fillRoundedRectangle (b, 3.0f);

    g.setColour (juce::Colours::white.withAlpha (on ? 0.25f : 0.08f));
    g.drawRoundedRectangle (b.reduced (0.5f), 3.0f, 0.8f);
    g.setColour (juce::Colours::black.withAlpha (0.85f));
    g.drawRoundedRectangle (b, 3.0f, 1.0f);
}

void DlayLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool highlighted, bool down)
{
    const bool on = button.getToggleState();
    g.setFont (getTextButtonFont (button, button.getHeight()));
    g.setColour ((on ? col::ink : (down ? col::amber : (highlighted ? col::cream : col::creamDim)))
                     .withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.4f));
    g.drawText (button.getButtonText().toUpperCase(), button.getLocalBounds().translated (0, down ? 1 : 0),
                juce::Justification::centred, false);
}

} // namespace dlay::ui
