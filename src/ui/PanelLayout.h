#pragma once

#include "core/ui_model/Controls.h"
#include <juce_graphics/juce_graphics.h>
#include <vector>

namespace spsx::ui
{
    /** The panel is laid out once in logical units (1000 x 1450, the hardware's
        177.6 x 256.7 mm aspect) and scaled to the window. Positions follow the manual's
        panel diagram (p. 4 to 5): knobs across the top, the round display with the six
        effect buttons wrapped around it, PATTERN SEQUENCER and SAMPLE EDIT rows, the
        DEL/REC/RESAMPLE and CANCEL/REMAIN/MARK cluster, SAMPLE MODE and BANK rows, the
        4 x 3 pads and the HOLD / EXT SOURCE / SUB PAD column. */
    struct PanelLayout
    {
        static constexpr float kWidth = 1000.0f;
        static constexpr float kHeight = 1450.0f;

        enum class Shape { button, roundButton, ringSegment, pad, sidePad };

        struct ControlGeom
        {
            Control control;
            Shape shape;
            juce::Rectangle<float> bounds;     // logical units (for ring segments: bounding box)
            juce::String label;
            juce::String legend;               // printed below or beside (pad functions, PAUSE)
            juce::Path path;                   // ring segments only
        };

        struct KnobGeom
        {
            Knob knob;
            juce::Point<float> centre;
            float radius;
            juce::String label, legendTop, legendBottom;
        };

        juce::Point<float> displayCentre { 500.0f, 540.0f };
        float displayRadius = 168.0f;          // round display face
        float ringInner = 182.0f, ringOuter = 262.0f;   // effect button ring
        juce::Rectangle<float> displayWindow { 365.0f, 470.0f, 270.0f, 118.0f };
        juce::Point<float> peakLed { 695.0f, 262.0f };

        std::vector<ControlGeom> controls;
        std::vector<KnobGeom> knobs;

        static const PanelLayout& get();

        const ControlGeom* find (Control c) const;
        const KnobGeom* find (Knob k) const;

        /** Hit test in logical units. */
        const ControlGeom* controlAt (juce::Point<float> p) const;
        const KnobGeom* knobAt (juce::Point<float> p) const;

    private:
        PanelLayout();
    };
}
