#pragma once

#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace spsx
{
    /** Phase 1 test editor: a plain pad grid for trying the engine by hand. Drop audio
        files on a pad to load them; click (and hold, for GATE) to play. The hardware
        panel replaces this in Phase 2. */
    class PluginEditor final : public juce::AudioProcessorEditor,
                               public juce::FileDragAndDropTarget,
                               private juce::Timer
    {
    public:
        explicit PluginEditor (PluginProcessor&);
        ~PluginEditor() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
        void filesDropped (const juce::StringArray& files, int x, int y) override;

    private:
        class PadView;

        void timerCallback() override;
        PadId padAt (int index) const noexcept { return { bank, index }; }
        void refreshModeButtons();
        void toggleMode (int which);

        PluginProcessor& proc;
        int bank = 0;
        PadId current = { 0, 0 };
        juce::OwnedArray<PadView> pads;
        juce::OwnedArray<juce::TextButton> bankButtons;
        juce::TextButton gate { "GATE" }, loop { "LOOP" }, reverse { "REVERSE" }, lofi { "LO-FI" };
        juce::TextButton hold { "HOLD" }, sub { "SUB PAD" }, stopAll { "STOP ALL" };
        juce::Slider volume;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> volumeAttachment;
        juce::Label status;
        bool subWasDown = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
    };
}
