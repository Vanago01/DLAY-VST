#pragma once

#include "PluginProcessor.h"
#include "UI/Theme.h"

/** DLAY's editor window.

    Everything is laid out once in fixed design units inside a content component; the
    window can be resized freely (fixed aspect ratio) and the content is simply scaled,
    so the interface stays crisp and proportioned from 70 % to 200 %.

    The full view is 1280 x 720 design units: a top bar, the main row (engine, the echo
    display, character) and a lower card with three pages - TIME, SPACE and MOTION. The
    compact view leaves the lower card out.
*/
class DlayAudioProcessorEditor : public juce::AudioProcessorEditor,
                                 private juce::Timer
{
public:
    explicit DlayAudioProcessorEditor (DlayAudioProcessor&);
    ~DlayAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kBaseWidth      = 1280;
    static constexpr int kAdvancedHeight = 720;
    static constexpr int kSimpleHeight   = 480;

    static constexpr double kMinScale = 0.7, kMaxScale = 2.0;

    enum Page { pageTime = 0, pageSpace, pageMotion, numPages };

    /** Switches between the compact view and the full view with the lower card. */
    void setAdvancedView (bool advanced);
    bool isAdvancedView() const noexcept      { return advancedView; }

    void setWindowScale (double scale);

    /** Opens or closes the preset library overlay. */
    void showPresetBrowser (bool shouldShow);

    /** Turns the lower card to one of its pages. */
    void showPage (int pageIndex);

    /** Turns the lower card to MOTION and selects the lane shown there (0..5). */
    void showMotionLane (int laneIndex);

private:
    class Content;

    void timerCallback() override;
    int baseHeight() const noexcept           { return advancedView ? kAdvancedHeight : kSimpleHeight; }
    void applyConstraints();
    double openingScale() const;

    DlayAudioProcessor& dlayProcessor;
    dlay::ui::AssetLifetime assets;           // must outlive every component
    dlay::ui::DlayLookAndFeel lookAndFeel;
    std::unique_ptr<Content> content;
    std::unique_ptr<juce::TooltipWindow> tooltips;

    bool advancedView = true;
    juce::uint32 lastTick = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DlayAudioProcessorEditor)
};
