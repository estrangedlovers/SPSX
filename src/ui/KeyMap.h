#pragma once

#include "core/ui_model/Controls.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <optional>
#include <vector>

namespace spsx::ui
{
    /** Keyboard bindings for held controls (pads, buttons). Keys act like the hardware:
        key down = press, key up = release, so holding a key holds the button.
        Defaults follow the project's shortcut table with pads mapped spatially
        (1 2 3 4 / Q W E R / A S D F = pads 1-4 / 5-8 / 9-12). The remapper is Phase 9;
        bindings are data so it only has to edit this table.

        Not in this table because they are not "held" controls: Option (FUNC, handled as a
        modifier), Shift + Z..B (banks F..J), arrows (knob select/adjust), - and = (VOLUME),
        ? (overlay), Cmd shortcuts. */
    class KeyMap
    {
    public:
        struct Binding
        {
            int keyCode;
            Control control;
        };

        static KeyMap defaults();

        const std::vector<Binding>& bindings() const noexcept { return table; }
        std::optional<Control> controlFor (int keyCode) const;
        juce::String labelFor (Control c) const;
        std::map<Control, juce::String> labels() const;

        static juce::String keyName (int keyCode);

    private:
        std::vector<Binding> table;
    };
}
