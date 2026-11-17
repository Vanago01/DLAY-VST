#pragma once

#include "PluginProcessor.h"
#include "UI/Theme.h"

/** DLAY's editor window.

    Everything is laid out once in fixed design units inside a content component; the
    window can be resized freely (fixed aspect ratio) and the content is simply scaled,
    so the interface stays crisp and proportioned from 60 % to 200 %.
*/
class DlayAudioProcessorEditor : public juce::AudioProcessorEditor,
                                 private juce::Timer
{
public:
    explicit DlayAudioProcessorEditor (DlayAudioProcessor&);
    ~DlayAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kBaseWidth      = 1240;
    static constexpr int kAdvancedHeight = 888;
    static constexpr int kSimpleHeight   = 580;

    /** Switches between the compact view and the full view with DETAIL and MOTION. */
    void setAdvancedView (bool advanced);
    bool isAdvancedView() const noexcept      { return advancedView; }

    void setWindowScale (double scale);

    /** Opens or closes the preset library overlay. */
    void showPresetBrowser (bool shouldShow);

    /** Selects the MOTION lane shown in the editor (0..5). */
    void showMotionLane (int laneIndex);

private:
    class Content;

    void timerCallback() override;
    int baseHeight() const noexcept           { return advancedView ? kAdvancedHeight : kSimpleHeight; }
    void applyConstraints();

    DlayAudioProcessor& dlayProcessor;
    dlay::ui::AssetLifetime assets;           // must outlive every component
    dlay::ui::DlayLookAndFeel lookAndFeel;
    std::unique_ptr<Content> content;
    std::unique_ptr<juce::TooltipWindow> tooltips;

    bool advancedView = true;
    juce::uint32 lastTick = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DlayAudioProcessorEditor)
};
