#include "KeyMap.h"

namespace spsx::ui
{
    KeyMap KeyMap::defaults()
    {
        KeyMap m;
        auto bind = [&m] (int key, Control c) { m.table.push_back ({ key, c }); };

        const char padKeys[] = { '1', '2', '3', '4', 'Q', 'W', 'E', 'R', 'A', 'S', 'D', 'F' };
        for (int i = 0; i < 12; ++i)
            bind (padKeys[i], padControl (i));

        bind (juce::KeyPress::spaceKey, Control::subPad);
        bind ('G', Control::hold);
        bind ('T', Control::extSource);

        const char bankKeys[] = { 'Z', 'X', 'C', 'V', 'B' };
        for (int i = 0; i < 5; ++i)
            bind (bankKeys[i], (Control) ((int) Control::bankAF + i));

        bind ('5', Control::filterDrive);
        bind ('6', Control::voiceTrans);
        bind ('7', Control::delay);
        bind ('8', Control::isolator);
        bind ('9', Control::djfxLooper);
        bind ('0', Control::mfx);

        bind ('J', Control::gate);
        bind ('K', Control::loop);
        bind ('L', Control::reverse);
        bind (';', Control::lofi);
        bind ('\'', Control::stereo);

        bind ('M', Control::mark);
        bind ('N', Control::startEndLevel);
        bind (',', Control::timeBpm);
        bind ('.', Control::tapTempo);
        bind ('/', Control::remain);

        bind ('P', Control::patternSelect);
        bind ('O', Control::length);
        bind ('I', Control::quantize);
        bind ('U', Control::resample);

        bind (juce::KeyPress::returnKey, Control::rec);
        bind (juce::KeyPress::backspaceKey, Control::del);
        bind (juce::KeyPress::deleteKey, Control::del);
        bind (juce::KeyPress::escapeKey, Control::cancel);
        return m;
    }

    std::optional<Control> KeyMap::controlFor (int keyCode) const
    {
        for (auto& b : table)
            if (b.keyCode == keyCode)
                return b.control;
        return std::nullopt;
    }

    juce::String KeyMap::keyName (int keyCode)
    {
        if (keyCode == juce::KeyPress::spaceKey)     return "Space";
        if (keyCode == juce::KeyPress::returnKey)    return "Return";
        if (keyCode == juce::KeyPress::backspaceKey) return "Delete";
        if (keyCode == juce::KeyPress::deleteKey)    return "Del";
        if (keyCode == juce::KeyPress::escapeKey)    return "Esc";
        return juce::String::charToString ((juce::juce_wchar) keyCode);
    }

    juce::String KeyMap::labelFor (Control c) const
    {
        for (auto& b : table)
            if (b.control == c)
                return keyName (b.keyCode);
        if (c == Control::func)
            return "Option";
        return {};
    }

    std::map<Control, juce::String> KeyMap::labels() const
    {
        std::map<Control, juce::String> out;
        for (int i = 0; i < kNumControls; ++i)
        {
            const auto c = (Control) i;
            if (auto l = labelFor (c); l.isNotEmpty())
                out[c] = isBank (c) ? l + " / Shift" : l;
        }
        return out;
    }
}
