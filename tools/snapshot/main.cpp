// Renders the panel (and optionally the Extended side panel) to a PNG without a
// display, for reviewing the UI from a headless machine and for README screenshots.
//
//   panel_snapshot --out panel.png [--width 700] [--scale 2] [--extended] [--keys]
//                  [--pad A1=file.wav ...] [--script "down A1; turn ctrl3 0.7; ..."]
//
// Script steps (separated by ';'): down/up <control-or-pad>, press <control>,
// turn <ctrl1|ctrl2|ctrl3|volume> <0..1>, wait <seconds>, render <ms> (runs the engine).

#include "core/engine/Engine.h"
#include "core/SamplerController.h"
#include "core/ui_model/PanelModel.h"
#include "core/io/AudioFileIO.h"
#include "ui/PanelComponent.h"
#include "ui/SidePanel.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <iostream>

using namespace spsx;

namespace
{
    std::optional<Control> controlNamed (juce::String s)
    {
        s = s.toUpperCase();
        if (s.length() >= 2 && s[0] >= 'A' && s[0] <= 'J' && s.substring (1).containsOnly ("0123456789"))
            return padControl (s.substring (1).getIntValue() - 1);
        for (int i = 0; i < kNumControls; ++i)
            if (juce::String (controlName ((Control) i)).toUpperCase().removeCharacters (" /+-") == s.removeCharacters (" /+-"))
                return (Control) i;
        return std::nullopt;
    }

    int fail (const juce::String& m) { std::cerr << "panel_snapshot: " << m << std::endl; return 1; }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;

    juce::File out;
    int width = 700;
    float scale = 2.0f;
    bool extended = false, keys = false;
    juce::StringArray pads;
    juce::String script;

    for (int i = 1; i < argc; ++i)
    {
        const juce::String a (argv[i]);
        auto next = [&] { return i + 1 < argc ? juce::String (argv[++i]) : juce::String(); };
        if (a == "--out") out = juce::File::getCurrentWorkingDirectory().getChildFile (next());
        else if (a == "--width") width = next().getIntValue();
        else if (a == "--scale") scale = next().getFloatValue();
        else if (a == "--extended") extended = true;
        else if (a == "--keys") keys = true;
        else if (a == "--pad") pads.add (next());
        else if (a == "--script") script = next();
        else return fail ("unknown argument " + a);
    }
    if (out == juce::File()) return fail ("--out is required");

    const auto limits = extended ? Limits::extended() : Limits::authentic();
    Engine engine;
    engine.prepare (kEngineRate, 512, limits);
    SamplerController controller (engine, limits);
    PanelModel model (engine, controller);
    model.setCallbacks ({ {}, [] { return (juce::int64) 7'000'000'000LL; }, {} });

    for (auto& p : pads)
    {
        auto id = controlNamed (p.upToFirstOccurrenceOf ("=", false, false));
        if (! id || ! isPad (*id)) return fail ("bad pad " + p);
        const int bank = p.toUpperCase()[0] - 'A';
        if (controller.loadFile ({ bank, padIndex (*id) }, juce::File::getCurrentWorkingDirectory().getChildFile (p.fromFirstOccurrenceOf ("=", false, false)))
            != SamplerController::Status::ok)
            return fail ("cannot load " + p);
    }

    double now = 100.0;
    std::vector<float> l (512), r (512);
    auto runEngine = [&] (double ms)
    {
        for (int n = (int) (ms * 44.1 / 512.0) + 1; n > 0; --n)
        {
            float* o[2] = { l.data(), r.data() };
            engine.process (nullptr, 0, o, 2, 512);
        }
        controller.tick();
    };
    runEngine (20);

    for (auto step : juce::StringArray::fromTokens (script, ";", ""))
    {
        auto t = juce::StringArray::fromTokens (step.trim(), " ", "");
        t.removeEmptyStrings();
        if (t.isEmpty()) continue;
        if (t[0] == "turn")
        {
            const juce::StringArray names { "ctrl1", "ctrl2", "ctrl3", "volume" };
            model.turn ((Knob) names.indexOf (t[1]), t[2].getDoubleValue(), now);
        }
        else if (t[0] == "wait") now += t[1].getDoubleValue();
        else if (t[0] == "render") runEngine (t[1].getDoubleValue());
        else
        {
            auto c = controlNamed (t.size() > 1 ? t[1] : t[0]);
            if (! c) return fail ("unknown control in: " + step);
            if (t[0] == "down")      model.press (*c, now);
            else if (t[0] == "up")   model.release (*c, now);
            else { model.press (*c, now); model.release (*c, now); }
        }
        runEngine (5);
        model.tick (now);
    }

    const int panelH = (int) std::round (width * ui::PanelLayout::kHeight / ui::PanelLayout::kWidth);
    const int sideW = extended ? 360 : 0;

    juce::Component root;
    root.setSize (width + sideW, panelH);
    ui::PanelComponent panel (model);
    ui::SidePanel side (model, controller, engine);
    root.addAndMakeVisible (panel);
    panel.setBounds (0, 0, width, panelH);
    if (extended)
    {
        root.addAndMakeVisible (side);
        side.setBounds (width, 0, sideW, panelH);
        side.refresh();
    }
    panel.setShowKeyLabels (keys);
    if (keys)
        panel.setKeyLabels ({ { padControl (0), "1" }, { Control::func, "Option" } }, {});
    panel.refresh (now);

    juce::Image img (juce::Image::ARGB, (int) ((float) root.getWidth() * scale), (int) ((float) root.getHeight() * scale), true);
    {
        juce::Graphics g (img);
        g.addTransform (juce::AffineTransform::scale (scale));
        root.paintEntireComponent (g, true);
    }
    out.deleteFile();
    juce::PNGImageFormat png;
    juce::FileOutputStream os (out);
    if (! os.openedOk() || ! png.writeImageToStream (img, os))
        return fail ("cannot write " + out.getFullPathName());
    std::cout << "Wrote " << out.getFullPathName() << std::endl;
    return 0;
}
