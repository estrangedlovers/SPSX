#include "SidePanel.h"
#include "core/sampler/SampleEdit.h"
#include "core/fx/EffectInfo.h"

namespace spsx::ui
{
    namespace
    {
        const juce::Colour kBg       { 0xff17181b };
        const juce::Colour kPanel    { 0xff202125 };
        const juce::Colour kText     { 0xffd9d9d4 };
        const juce::Colour kDim      { 0xff8d8e8a };
        const juce::Colour kAccent   { 0xffff3d24 };
        const juce::Colour kWave     { 0xffc8c8c2 };
        constexpr float kHandleGrab = 8.0f;

        juce::String bankPadName (PadId p)
        {
            return juce::String::charToString ((juce::juce_wchar) ('A' + p.bank)) + juce::String (p.pad + 1);
        }
    }

    // ---- WaveformView -----------------------------------------------------------------

    WaveformView::WaveformView (SamplerController& c, Engine& e) : controller (c), engine (e) {}

    const PadSlot* WaveformView::slot() const
    {
        if (! pad.isValid() || pad.bank >= controller.project().numBanks)
            return nullptr;
        const auto& s = controller.project().slot (pad);
        return s.isEmpty() ? nullptr : &s;
    }

    void WaveformView::setPad (PadId p)
    {
        if (p != pad)
        {
            pad = p;
            peaksFor = nullptr;
            repaint();
        }
    }

    void WaveformView::refresh()
    {
        const auto* s = slot();
        const auto pos = s ? engine.playPosition (pad) : -1;
        if (pos != lastPos || (s && s->settings != lastSettings) || (s && s->sample.get() != peaksFor) || (! s && peaksFor))
        {
            lastPos = pos;
            if (s) lastSettings = s->settings;
            repaint();
        }
    }

    void WaveformView::rebuildPeaks()
    {
        const auto* s = slot();
        peaks.clear();
        peaksFor = s ? s->sample.get() : nullptr;
        peaksWidth = getWidth();
        if (s == nullptr || peaksWidth <= 0)
            return;

        const auto& smp = *s->sample;
        const double perPixel = (double) smp.length() / peaksWidth;
        peaks.resize ((size_t) peaksWidth);
        for (int x = 0; x < peaksWidth; ++x)
        {
            const auto a = (SampleIndex) (x * perPixel);
            const auto b = juce::jmax (a + 1, (SampleIndex) ((x + 1) * perPixel));
            float lo = 0.0f, hi = 0.0f;
            for (int ch = 0; ch < smp.numChannels(); ++ch)
            {
                const float* d = smp.channel (ch);
                for (auto i = a; i < juce::jmin (b, smp.length()); ++i)
                {
                    lo = juce::jmin (lo, d[i]);
                    hi = juce::jmax (hi, d[i]);
                }
            }
            peaks[(size_t) x] = { lo, hi };
        }
    }

    float WaveformView::xFor (SampleIndex s) const
    {
        const auto* sl = slot();
        if (sl == nullptr || sl->sample->length() == 0)
            return 0.0f;
        return (float) ((double) s / (double) sl->sample->length() * getWidth());
    }

    SampleIndex WaveformView::sampleAt (float x) const
    {
        const auto* sl = slot();
        if (sl == nullptr)
            return 0;
        return (SampleIndex) juce::jlimit (0.0, (double) sl->sample->length(), (double) x / juce::jmax (1, getWidth()) * (double) sl->sample->length());
    }

    void WaveformView::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff0b0b0c));
        g.fillRoundedRectangle (r, 6.0f);

        const auto* s = slot();
        if (s == nullptr)
        {
            g.setColour (kDim);
            g.setFont (juce::Font (juce::FontOptions (14.0f)));
            g.drawText ("Empty pad", r, juce::Justification::centred);
            return;
        }
        if (peaksFor != s->sample.get() || peaksWidth != getWidth())
            rebuildPeaks();

        const float mid = r.getCentreY(), half = r.getHeight() * 0.45f;
        const float xs = xFor (s->settings.start), xe = xFor (s->settings.end);

        for (int x = 0; x < (int) peaks.size(); ++x)
        {
            const bool inside = (float) x >= xs && (float) x <= xe;
            g.setColour (inside ? kWave : kWave.withAlpha (0.25f));
            const auto [lo, hi] = peaks[(size_t) x];
            g.drawVerticalLine (x, mid - hi * half, mid - lo * half + 1.0f);
        }

        g.setColour (kAccent.withAlpha (0.12f));
        g.fillRect (juce::Rectangle<float> (xs, r.getY(), xe - xs, r.getHeight()));

        for (float hx : { xs, xe })
        {
            g.setColour (kAccent);
            g.drawLine (hx, r.getY(), hx, r.getBottom(), 2.0f);
            g.fillRect (juce::Rectangle<float> (hx - 5.0f, r.getY(), 10.0f, 10.0f));
        }

        if (lastPos >= 0)
        {
            g.setColour (juce::Colours::white);
            const float px = xFor (lastPos);
            g.drawLine (px, r.getY(), px, r.getBottom(), 1.5f);
        }
    }

    void WaveformView::mouseMove (const juce::MouseEvent& e)
    {
        const auto* s = slot();
        if (s == nullptr) return;
        const bool near = std::abs (e.position.x - xFor (s->settings.start)) < kHandleGrab
                       || std::abs (e.position.x - xFor (s->settings.end)) < kHandleGrab;
        setMouseCursor (near ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
    }

    void WaveformView::mouseDown (const juce::MouseEvent& e)
    {
        const auto* s = slot();
        if (s == nullptr) return;
        const float ds = std::abs (e.position.x - xFor (s->settings.start));
        const float de = std::abs (e.position.x - xFor (s->settings.end));
        dragging = ds < de ? Handle::start : Handle::end;
        mouseDrag (e);
    }

    void WaveformView::mouseDrag (const juce::MouseEvent& e)
    {
        const auto* s = slot();
        if (s == nullptr || dragging == Handle::none) return;
        const auto len = s->sample->length();
        const auto minLen = edit::minRegion (len);
        const auto at = sampleAt (e.position.x);

        controller.editSettings (pad, [&] (PadSettings& st)
        {
            if (dragging == Handle::start)
                edit::setRegion (st, juce::jlimit<SampleIndex> (0, st.end - minLen, at), st.end, len);
            else
                edit::setRegion (st, st.start, juce::jlimit<SampleIndex> (st.start + minLen, len, at), len);
        });
        repaint();
    }

    void WaveformView::mouseUp (const juce::MouseEvent&) { dragging = Handle::none; }

    void WaveformView::mouseDoubleClick (const juce::MouseEvent&)
    {
        // Reset to the whole sample (same as clearing MARK).
        if (const auto* s = slot())
        {
            const auto len = s->sample->length();
            controller.editSettings (pad, [len] (PadSettings& st) { edit::setRegion (st, 0, len, len); });
        }
    }

    // ---- SidePanel --------------------------------------------------------------------

    SidePanel::SidePanel (PanelModel& m, SamplerController& c, Engine& e)
        : model (m), controller (c), waveform (c, e)
    {
        addAndMakeVisible (waveform);
    }

    void SidePanel::refresh()
    {
        const auto pad = model.currentPad();
        waveform.setPad (pad);
        waveform.refresh();

        juce::String text;
        const auto& slot = controller.project().slot (pad);
        text << "Pad " << bankPadName (pad) << "\n";
        if (slot.isEmpty())
            text << "Empty\n";
        else
        {
            const auto& s = slot.settings;
            const auto& smp = *slot.sample;
            const double rate = kEngineRate;
            text << juce::File (slot.file).getFileName() << "\n"
                 << juce::String (smp.seconds(), 2) << " s, " << (smp.numChannels() > 1 ? "stereo" : "mono")
                 << (smp.isQuantized16() ? ", 16-bit" : ", 32-bit float") << "\n"
                 << "Region " << juce::String ((double) s.start / rate, 3) << " to " << juce::String ((double) s.end / rate, 3) << " s\n"
                 << "BPM " << juce::String (s.bpm, 1) << "   LEVEL " << s.level << "\n"
                 << (s.gate ? "GATE " : "") << (s.loop ? "LOOP " : "") << (s.reverse ? "REVERSE " : "") << (s.lofi ? "LO-FI " : "") << "\n"
                 << "Time Modify: " << (s.timeModify == TimeModifyMode::off ? juce::String ("off")
                                       : s.timeModify == TimeModifyMode::pattern ? juce::String ("follow pattern")
                                       : juce::String (s.timeModifyBpm, 1) + " BPM") << "\n";
        }
        // Effect in use and its parameters, as the display would show them.
        const int effect = model.activeEffectIndex();
        text << "\nEffect: ";
        if (effect < 0)
            text << "off";
        else
        {
            const auto& info = fx::info (effect);
            const auto& p = model.effects().params[(size_t) effect];
            text << info.name << "\n";
            for (int k = 0; k < 3; ++k)
                text << "  " << info.params[(size_t) k].code << " "
                     << fx::display (effect, k, p[(size_t) k], 120.0).toString() << (k < 2 ? "" : "\n");
        }
        if (model.isExtCurrent())
            text << "EXT SOURCE " << (model.isExtOn() ? "on" : "off") << "\n";

        if (text != infoText || pad != shownPad)
        {
            infoText = text;
            shownPad = pad;
            repaint();
        }
    }

    void SidePanel::paint (juce::Graphics& g)
    {
        g.fillAll (kBg);
        auto r = getLocalBounds().reduced (12);

        auto section = [&g] (juce::Rectangle<int> area, const juce::String& title)
        {
            g.setColour (kPanel);
            g.fillRoundedRectangle (area.toFloat(), 8.0f);
            g.setColour (kDim);
            g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
            g.drawText (title, area.reduced (10, 6).removeFromTop (16), juce::Justification::centredLeft);
        };

        auto info = r.removeFromTop (230);
        section (info, "PAD INFO");
        g.setColour (kText);
        g.setFont (juce::Font (juce::FontOptions (14.0f)));
        g.drawMultiLineText (infoText, info.getX() + 10, info.getY() + 40, info.getWidth() - 20);

        r.removeFromTop (10);
        auto wave = r.removeFromTop (220);
        section (wave, "WAVEFORM  (drag start/end, double-click to reset)");

        r.removeFromTop (10);
        section (r, "PATTERN");
        g.setColour (kDim);
        g.setFont (juce::Font (juce::FontOptions (13.0f)));
        g.drawText ("Pattern note view arrives with the sequencer (Phase 5).", r.reduced (10), juce::Justification::centred);
    }

    void SidePanel::resized()
    {
        auto r = getLocalBounds().reduced (12);
        r.removeFromTop (230 + 10);
        auto wave = r.removeFromTop (220);
        waveform.setBounds (wave.reduced (10).withTrimmedTop (22));
    }
}
