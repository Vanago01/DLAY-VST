#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/** DLAY visual language: a 1970s tape machine that ended up on a spacecraft.
    Almost-black charcoal, warm cream, muted orange, amber LEDs, a little electric cyan. */
namespace dlay::ui
{

namespace col
{
    inline const juce::Colour bg          { 0xff0b0c0e };
    inline const juce::Colour panel       { 0xff16181b };
    inline const juce::Colour panelHi     { 0xff1e2125 };
    inline const juce::Colour edge        { 0xff2c3035 };
    inline const juce::Colour groove      { 0xff060708 };

    inline const juce::Colour cream       { 0xffe9e0c9 };
    inline const juce::Colour creamDim    { 0xffa9a18d };
    inline const juce::Colour creamFaint  { 0xff6b665a };
    inline const juce::Colour ink         { 0xff1c1a16 };

    inline const juce::Colour orange      { 0xffd8743a };
    inline const juce::Colour orangeDeep  { 0xff9c4f27 };
    inline const juce::Colour amber       { 0xffffb03a };
    inline const juce::Colour amberDim    { 0xff7d5219 };

    inline const juce::Colour cyan        { 0xff62d8e6 };
    inline const juce::Colour cyanDim     { 0xff1f6570 };

    inline const juce::Colour silver      { 0xffbcc1c7 };
    inline const juce::Colour steel       { 0xff757c84 };
    inline const juce::Colour red         { 0xffe5504a };

    inline const juce::Colour screen      { 0xff07090a };       // CRT glass
}

enum class FontKind { medium, semiBold, bold, mono };

/** Embedded typefaces: Barlow Condensed for panel legends, Share Tech Mono for read-outs. */
juce::Font font (FontKind kind, float height, float tracking = 0.0f);

inline juce::Font labelFont (float height = 11.0f)     { return font (FontKind::semiBold, height, 0.09f); }
inline juce::Font monoFont (float height = 11.0f)      { return font (FontKind::mono, height, 0.0f); }

float textWidth (const juce::Font& f, const juce::String& text);

/** Tileable brushed-metal texture (generated once, shared by all editors). */
juce::Image brushedTexture();

/** Keeps the embedded typefaces and textures loaded. The editor owns one for its lifetime;
    without it every font request would have to load the assets again. */
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
/** A module panel: charcoal plate, brushed sheen, bevel and four screws. */
void drawPanel (juce::Graphics&, juce::Rectangle<float> bounds, bool screws = true);

/** Small legend with an index number and a rule, e.g. "01  DELAY ENGINE ────". */
void drawSectionHeader (juce::Graphics&, juce::Rectangle<float> bounds, const juce::String& index, const juce::String& title);

void drawScrew (juce::Graphics&, juce::Point<float> centre, float radius, float angle);

/** LED with a soft halo. brightness 0..1. */
void drawLed (juce::Graphics&, juce::Point<float> centre, float radius, juce::Colour colour, float brightness);

/** Recessed dark glass for read-outs and scopes. */
void drawScreen (juce::Graphics&, juce::Rectangle<float> bounds, float corner = 4.0f);

/** Text with a faint phosphor halo. */
void drawGlowText (juce::Graphics&, const juce::String& text, juce::Rectangle<float> bounds,
                   juce::Justification justification, const juce::Font& f, juce::Colour colour);

/** Rotary control. value/centre are 0..1 proportions; centre < 0 = unipolar. */
void drawKnob (juce::Graphics&, juce::Rectangle<float> bounds, float value, float centre,
               juce::Colour accent, bool highlighted, bool enabled);

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

private:
    juce::TextLayout layoutTooltip (const juce::String& text) const;
};

} // namespace dlay::ui
