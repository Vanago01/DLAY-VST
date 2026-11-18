#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/** DLAY visual language: a white instrument with windows onto deep space.

    The body is light - soft white cards, white dials, pill-shaped keys. Everything that
    shows sound (the echo display, the read-outs, the MOTION editor) is a dark screen:
    near-black with a magenta and violet haze and a few stars. One accent runs through all
    of it: pink turning into purple. */
namespace dlay::ui
{

namespace col
{
    // ---- the body: light, cool, almost white ---------------------------------
    inline const juce::Colour page        { 0xffe9e9f0 };       // behind the cards
    inline const juce::Colour card        { 0xfffafafd };
    inline const juce::Colour cardLow     { 0xffefeff5 };       // fields and switch tracks
    inline const juce::Colour line        { 0xffdddde7 };       // hairlines
    inline const juce::Colour track       { 0xffd6d6e1 };       // empty dial arcs and meters
    inline const juce::Colour ink         { 0xff15131c };       // text
    inline const juce::Colour inkDim      { 0xff5c596a };
    inline const juce::Colour inkFaint    { 0xff9593a4 };
    inline const juce::Colour dark        { 0xff1b1924 };       // the dark dial, pressed keys

    // ---- the screens: deep space -----------------------------------------------
    inline const juce::Colour space       { 0xff07050c };
    inline const juce::Colour spaceHi     { 0xff16121f };
    inline const juce::Colour haze        { 0xff4c1d38 };       // magenta nebula
    inline const juce::Colour hazeViolet  { 0xff2a1758 };
    inline const juce::Colour star        { 0xfff6f3ff };       // text on a screen
    inline const juce::Colour starDim     { 0xffaaa5bb };
    inline const juce::Colour starFaint   { 0xff6f6a80 };

    // ---- the accent: pink into purple -------------------------------------------
    inline const juce::Colour pink        { 0xffff5fa8 };
    inline const juce::Colour magenta     { 0xffe662c7 };
    inline const juce::Colour purple      { 0xffb068f8 };
    inline const juce::Colour violet      { 0xff7a3ff2 };       // the accent where it has to read as text on white
    inline const juce::Colour ice         { 0xff8fd8ff };       // FREEZE
    inline const juce::Colour hot         { 0xffff3d6e };       // clipping, errors
}

enum class FontKind { medium, semiBold, bold, heavy, mono };

/** Embedded typefaces: Geist for everything you read, Geist Mono for labels and numbers.

    Sizes are em sizes in design pixels, the way a web page counts them: capitals of a
    14 px font stand about 10 px tall. (JUCE's own "height" is the full line height, a
    third more than that, which makes small sizes easy to misjudge.) */
juce::Font font (FontKind kind, float px, float tracking = 0.0f);

inline juce::Font labelFont (float px = 14.0f)         { return font (FontKind::semiBold, px, 0.01f); }
inline juce::Font monoFont (float px = 13.0f)          { return font (FontKind::mono, px, 0.0f); }

/** Small capitals for section names and units: mono, widely spaced. */
inline juce::Font capsFont (float px = 11.5f)          { return font (FontKind::mono, px, 0.1f); }

float textWidth (const juce::Font& f, const juce::String& text);

/** "WOW/FLT" -> "Wow/Flt": legends of the small dials are set in mixed case. */
juce::String titleCase (const juce::String& text);

/** Keeps the embedded typefaces loaded. The editor owns one for its lifetime; without it
    every font request would have to load the assets again. */
class AssetLifetime
{
public:
    AssetLifetime();
    ~AssetLifetime();

private:
    struct Holder;
    std::unique_ptr<Holder> holder;
};

// ---- drawing helpers --------------------------------------------------------
/** The accent as a gradient between two points: pink, magenta, purple. */
juce::ColourGradient accentGradient (juce::Point<float> from, juce::Point<float> to, float alpha = 1.0f);

/** Accent gradient running left to right across a rectangle. */
juce::ColourGradient accentGradient (juce::Rectangle<float> across, float alpha = 1.0f);

/** The page behind the cards: almost white, with a breath of the nebula in two corners.
    full is the whole page in the current coordinates; the clip decides how much is drawn. */
void drawPage (juce::Graphics&, juce::Rectangle<float> full);

/** A module card: white, rounded, lifted off the page by a soft shadow. */
void drawCard (juce::Graphics&, juce::Rectangle<float> bounds, float corner = 18.0f, bool withShadow = true);

/** Section legend: a pink index and the name in spaced capitals, e.g. "01  ENGINE". */
void drawSectionHeader (juce::Graphics&, juce::Rectangle<float> bounds, const juce::String& index, const juce::String& title);

/** Indicator dot with a soft halo. brightness 0..1. */
void drawLed (juce::Graphics&, juce::Point<float> centre, float radius, juce::Colour colour, float brightness);

/** A window onto space: dark glass with a magenta and violet haze. */
void drawScreen (juce::Graphics&, juce::Rectangle<float> bounds, float corner = 12.0f, float haze = 1.0f);

/** Scatters a repeatable field of faint stars over an area. */
void drawStars (juce::Graphics&, juce::Rectangle<float> bounds, juce::uint32 seed, int count, float brightness = 1.0f);

/** Text on a screen, with a faint halo. */
void drawGlowText (juce::Graphics&, const juce::String& text, juce::Rectangle<float> bounds,
                   juce::Justification justification, const juce::Font& f, juce::Colour colour);

/** Text filled with the accent gradient. */
void drawAccentText (juce::Graphics&, const juce::String& text, juce::Rectangle<float> bounds,
                     juce::Justification justification, const juce::Font& f);

/** A recessed field on a card (value boxes, text fields, switch tracks). */
void drawField (juce::Graphics&, juce::Rectangle<float> bounds, float corner);

/** Rotary control. value/centre are 0..1 proportions; centre < 0 = unipolar. */
void drawKnob (juce::Graphics&, juce::Rectangle<float> bounds, float value, float centre,
               bool darkBody, bool highlighted, bool enabled);

// ---- look and feel ------------------------------------------------------------
class DlayLookAndFeel : public juce::LookAndFeel_V4
{
public:
    DlayLookAndFeel();

    float uiScale = 1.0f;

    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                            bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                            const juce::String& shortcutKeyText, const juce::Drawable* icon,
                            const juce::Colour* textColour) override;
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuSectionHeader (juce::Graphics&, const juce::Rectangle<int>& area, const juce::String& sectionName) override;
    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardMenuItemHeight,
                                    int& idealWidth, int& idealHeight) override;

    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                           juce::Rectangle<int> parentArea) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;

    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height, bool isScrollbarVertical,
                        int thumbStartPosition, int thumbSize, bool isMouseOver, bool isMouseDown) override;

    void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    void drawCornerResizer (juce::Graphics&, int w, int h, bool isMouseOver, bool isMouseDragging) override;

private:
    juce::TextLayout layoutTooltip (const juce::String& text) const;
};

} // namespace dlay::ui
