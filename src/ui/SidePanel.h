#pragma once

#include "core/ui_model/PanelModel.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace spsx::ui
{
    /** Waveform of the current pad with draggable start/end and a playhead.
        Dragging writes the region through SamplerController, so the engine, MARK LED and
        BPM follow exactly as with the hardware's START/END knobs. */
    class WaveformView final : public juce::Component
    {
    public:
        WaveformView (SamplerController& controller, Engine& engine);

        void setPad (PadId pad);
        void refresh();

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;

    private:
        void rebuildPeaks();
        float xFor (SampleIndex s) const;
        SampleIndex sampleAt (float x) const;
        const PadSlot* slot() const;

        SamplerController& controller;
        Engine& engine;
        PadId pad = kNoPad;
        const SampleData* peaksFor = nullptr;
        int peaksWidth = 0;
        std::vector<std::pair<float, float>> peaks;
        SampleIndex lastPos = -2;
        PadSettings lastSettings;
        enum class Handle { none, start, end } dragging = Handle::none;
    };

    /** Extended-mode side panel: pad info, waveform editor, pattern note view. */
    class SidePanel final : public juce::Component
    {
    public:
        SidePanel (PanelModel& model, SamplerController& controller, Engine& engine);

        void refresh();
        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        PanelModel& model;
        SamplerController& controller;
        WaveformView waveform;
        PadId shownPad = kNoPad;
        juce::String infoText;
    };
}
