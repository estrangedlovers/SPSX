#include "PanelLayout.h"
#include <cmath>

namespace spsx::ui
{
    namespace
    {
        constexpr float kPi = 3.14159265358979f;

        /** Annular sector centred on `angleDeg` (0 = right, counter-clockwise, screen y down). */
        juce::Path ringSegment (juce::Point<float> c, float r0, float r1, float angleDeg, float spanDeg)
        {
            // JUCE arcs measure clockwise from 12 o'clock.
            const float centre = (90.0f - angleDeg) * kPi / 180.0f;
            const float half = spanDeg * 0.5f * kPi / 180.0f;
            juce::Path p;
            p.addCentredArc (c.x, c.y, r1, r1, 0.0f, centre - half, centre + half, true);
            p.addCentredArc (c.x, c.y, r0, r0, 0.0f, centre + half, centre - half, false);
            p.closeSubPath();
            return p;
        }
    }

    const PanelLayout& PanelLayout::get()
    {
        static const PanelLayout layout;
        return layout;
    }

    PanelLayout::PanelLayout()
    {
        using R = juce::Rectangle<float>;
        auto add = [this] (Control c, Shape s, R r, juce::String label, juce::String legend = {})
        {
            controls.push_back ({ c, s, r, std::move (label), std::move (legend), {} });
        };

        // ---- knobs ------------------------------------------------------------------
        knobs.push_back ({ Knob::volume, { 130.0f, 175.0f }, 56.0f, "VOLUME", "", "MIN        MAX" });
        knobs.push_back ({ Knob::ctrl1, { 375.0f, 175.0f }, 62.0f, "CTRL 1", "", "CUTOFF  START/TIME" });
        knobs.push_back ({ Knob::ctrl2, { 585.0f, 175.0f }, 62.0f, "CTRL 2", "", "RESONANCE  END/BPM" });
        knobs.push_back ({ Knob::ctrl3, { 800.0f, 175.0f }, 62.0f, "CTRL 3 / MFX", "", "DRIVE  LEVEL" });
        knobs.push_back ({ Knob::micLevel, { 945.0f, 1235.0f }, 28.0f, "MIC LEVEL", "", "" });

        // ---- effect buttons around the display --------------------------------------
        const struct { Control c; float angle; const char* label; } ring[] = {
            { Control::filterDrive, 148.0f, "FILTER\n+DRIVE" },
            { Control::voiceTrans,  180.0f, "VOICE\nTRANS" },
            { Control::delay,       212.0f, "DELAY" },
            { Control::isolator,     32.0f, "ISOLATOR" },
            { Control::djfxLooper,    0.0f, "DJFX\nLOOPER" },
            { Control::mfx,         -32.0f, "MFX" },
        };
        for (auto& r : ring)
        {
            ControlGeom g { r.c, Shape::ringSegment, {}, r.label, {}, ringSegment (displayCentre, ringInner, ringOuter, r.angle, 28.0f) };
            g.bounds = g.path.getBounds();
            controls.push_back (std::move (g));
        }

        // ---- PATTERN SEQUENCER / SAMPLE EDIT row ------------------------------------
        add (Control::patternSelect, Shape::button, R (60, 822, 150, 58), "PATTERN\nSELECT");
        add (Control::length,        Shape::button, R (225, 822, 115, 58), "LENGTH");
        add (Control::quantize,      Shape::button, R (355, 822, 125, 58), "QUANTIZE");
        add (Control::startEndLevel, Shape::button, R (545, 822, 130, 58), "START/END\n/LEVEL");
        add (Control::timeBpm,       Shape::button, R (690, 822, 110, 58), "TIME\n/BPM");
        add (Control::tapTempo,      Shape::roundButton, R (850, 798, 108, 108), "TAP\nTEMPO", "PAUSE");

        // ---- DEL / REC / RESAMPLE, CANCEL / REMAIN / MARK ---------------------------
        add (Control::del,      Shape::button, R (60, 935, 110, 52), "DEL");
        add (Control::rec,      Shape::button, R (182, 935, 110, 52), "REC");
        add (Control::resample, Shape::button, R (304, 935, 120, 52), "RESAMPLE");
        add (Control::cancel,   Shape::button, R (60, 1010, 110, 52), "CANCEL");
        add (Control::remain,   Shape::button, R (182, 1010, 110, 52), "REMAIN");
        add (Control::mark,     Shape::button, R (304, 1010, 120, 52), "MARK");

        // ---- SAMPLE MODE ------------------------------------------------------------
        add (Control::lofi,    Shape::button, R (470, 935, 96, 52), "LO-FI");
        add (Control::stereo,  Shape::button, R (575, 935, 96, 52), "STEREO");
        add (Control::gate,    Shape::button, R (680, 935, 96, 52), "GATE");
        add (Control::loop,    Shape::button, R (785, 935, 96, 52), "LOOP");
        add (Control::reverse, Shape::button, R (890, 935, 96, 52), "REVERSE");

        // ---- BANK + FUNC ------------------------------------------------------------
        const char* banks[] = { "A/F", "B/G", "C/H", "D/I", "E/J" };
        for (int i = 0; i < 5; ++i)
            add ((Control) ((int) Control::bankAF + i), Shape::button, R (470.0f + i * 84.0f, 1010, 74, 52), banks[i]);
        add (Control::func, Shape::button, R (900, 1010, 86, 52), "FUNC");

        // ---- pads -------------------------------------------------------------------
        const char* legends[] = { "COPY", "EXCHANGE", "IMPORT", "AUTO TRIG LEVEL", "BACKUP LOAD", "BACKUP SAVE",
                                  "ILLUMINATION", "EFFECT LIMIT", "MIDI CH", "MIDI SYNC", "INPUT GAIN", "OUTPUT GAIN" };
        for (int i = 0; i < 12; ++i)
        {
            const float x = 60.0f + (float) (i % 4) * 162.0f;
            const float y = 1100.0f + (float) (i / 4) * 112.0f;
            add (padControl (i), Shape::pad, R (x, y, 148, 88), juce::String (i + 1), legends[i]);
        }

        add (Control::hold,      Shape::sidePad, R (725, 1100, 150, 88), "HOLD");
        add (Control::extSource, Shape::sidePad, R (725, 1212, 150, 88), "EXT\nSOURCE");
        add (Control::subPad,    Shape::sidePad, R (725, 1324, 150, 88), "SUB PAD");
        add (Control::mic,       Shape::button,  R (905, 1110, 80, 46), "MIC");
    }

    const PanelLayout::ControlGeom* PanelLayout::find (Control c) const
    {
        for (auto& g : controls)
            if (g.control == c)
                return &g;
        return nullptr;
    }

    const PanelLayout::KnobGeom* PanelLayout::find (Knob k) const
    {
        for (auto& g : knobs)
            if (g.knob == k)
                return &g;
        return nullptr;
    }

    const PanelLayout::ControlGeom* PanelLayout::controlAt (juce::Point<float> p) const
    {
        for (auto& g : controls)
        {
            if (g.shape == Shape::ringSegment ? g.path.contains (p)
                : g.shape == Shape::roundButton ? g.bounds.getCentre().getDistanceFrom (p) <= g.bounds.getWidth() * 0.5f
                : g.bounds.contains (p))
                return &g;
        }
        return nullptr;
    }

    const PanelLayout::KnobGeom* PanelLayout::knobAt (juce::Point<float> p) const
    {
        for (auto& k : knobs)
            if (k.centre.getDistanceFrom (p) <= k.radius * 1.15f)
                return &k;
        return nullptr;
    }
}
