#pragma once

#include "PluginProcessor.h"
#include "../ui/PanelComponent.h"
#include "../ui/SidePanel.h"
#include "../ui/KeyMap.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <set>

namespace spsx
{
    /** Main window: a slim top bar, the hardware panel and, in Extended mode, the side
        panel. Keyboard input maps to panel controls (ui::KeyMap); held keys hold buttons. */
    class PluginEditor final : public juce::AudioProcessorEditor,
                               public juce::FileDragAndDropTarget
    {
    public:
        explicit PluginEditor (PluginProcessor&);
        ~PluginEditor() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        bool keyPressed (const juce::KeyPress&) override;
        bool keyStateChanged (bool isKeyDown) override;
        void modifierKeysChanged (const juce::ModifierKeys&) override;
        void focusLost (FocusChangeType) override;

        bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
        void filesDropped (const juce::StringArray& files, int x, int y) override;

    private:
        static constexpr int kBarHeight = 34;
        static constexpr int kSideWidth = 360;
        static constexpr int kPanelBaseWidth = 560;

        void onFrame();
        void updateLayoutForMode();
        void importFiles (PadId start, juce::Array<juce::File> files);
        void openImportChooser (PadId start);
        void releaseAllKeys();
        double now() const { return PluginProcessor::nowSeconds(); }

        PluginProcessor& proc;
        PanelModel& model;
        ui::PanelComponent panel;
        ui::SidePanel side;
        ui::KeyMap keyMap = ui::KeyMap::defaults();

        juce::TextButton authenticButton { "Authentic" }, extendedButton { "Extended" }, keysButton { "Keys  ?" };
        juce::TextButton fxModeButton { "FX Mode" };
        juce::Label status;

        std::set<int> keysDown;
        bool altDown = false;
        Knob selectedKnob = Knob::ctrl1;
        bool knobSelectionVisible = false;

        std::unique_ptr<juce::FileChooser> chooser;
        juce::ComponentBoundsConstrainer constrainer;
        juce::VBlankAttachment vblank { this, [this] { onFrame(); } };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
    };
}
