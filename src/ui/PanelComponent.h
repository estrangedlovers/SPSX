#pragma once

#include "PanelLayout.h"
#include "core/ui_model/PanelModel.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <optional>
#include <set>

namespace spsx::ui
{
    /** The hardware panel. Pure view + mouse input: it renders a PanelView from the
        PanelModel and turns clicks and drags into model input. Static artwork is cached
        in an image at the current physical resolution; LEDs, pads, knobs and the display
        are drawn live. refresh() repaints only when the view or a blink phase changed. */
    class PanelComponent final : public juce::Component
    {
    public:
        explicit PanelComponent (PanelModel& model);

        /** Call once per frame (VBlank). */
        void refresh (double now);

        void setKeyLabels (std::map<Control, juce::String> controls, std::map<Knob, juce::String> knobs);
        void setShowKeyLabels (bool show);
        bool isShowingKeyLabels() const noexcept { return showKeys; }
        void setSelectedKnob (std::optional<Knob> k) { selectedKnob = k; repaint(); }

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

        /** MIDI learn mode: clicks choose the learn target instead of pressing. */
        void setLearnMode (bool on) { learnMode = on; }
        bool isLearnMode() const noexcept { return learnMode; }
        std::function<void()> onLearnTargetChanged;

        /** Logical panel units to component pixels. */
        juce::AffineTransform toScreen() const noexcept { return transform; }

    private:
        void renderStatic (juce::Graphics& g) const;
        void drawButton (juce::Graphics& g, const PanelLayout::ControlGeom& c, bool lit, bool latched) const;
        void drawPad (juce::Graphics& g, const PanelLayout::ControlGeom& c, bool lit) const;
        void drawKnob (juce::Graphics& g, const PanelLayout::KnobGeom& k, float value, bool selected) const;
        void drawDisplay (juce::Graphics& g, double now) const;
        void drawKeyLabels (juce::Graphics& g) const;
        bool ledIsLit (Led l, double now) const noexcept;
        juce::Colour ringColour (const PanelView& v, double now) const noexcept;
        juce::Rectangle<int> screenBounds (juce::Rectangle<float> logical, float margin) const noexcept;
        juce::Point<float> toLogical (juce::Point<float> p) const noexcept { return p.transformedBy (transform.inverted()); }
        double nowSeconds() const noexcept;

        PanelModel& model;
        PanelView view;
        double viewTime = 0.0;
        // What is visibly on screen, to repaint only what changed.
        std::array<bool, kNumControls> shownLit {};
        juce::Colour shownRing;
        std::uint32_t shownDisplayKey = 0;
        bool firstRefresh = true;

        juce::AffineTransform transform;
        float scale = 1.0f;
        juce::Image staticLayer;

        std::map<Control, juce::String> keyLabels;
        std::map<Knob, juce::String> knobKeyLabels;
        bool showKeys = false;
        bool learnMode = false;
        std::optional<Knob> selectedKnob;

        // mouse
        std::optional<Control> mouseControl;
        std::optional<Knob> dragKnob;
        double dragStartValue = 0.0;
        float dragStartY = 0.0f;
        std::set<Control> latched;   // Shift-click holds a button down (FUNC + pad with a mouse)

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PanelComponent)
    };
}
