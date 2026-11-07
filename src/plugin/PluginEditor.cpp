#include "PluginEditor.h"

namespace spsx
{
    class PluginEditor::PadView final : public juce::Component
    {
    public:
        PadView (PluginEditor& o, int i) : owner (o), index (i) {}

        void paint (juce::Graphics& g) override
        {
            const auto id = owner.padAt (index);
            const bool loaded = ! owner.proc.getController().project().slot (id).isEmpty();
            const bool playing = owner.proc.getEngine().playPosition (id) >= 0;
            auto r = getLocalBounds().toFloat().reduced (3.0f);
            g.setColour (playing ? juce::Colour (0xffe03030) : loaded ? juce::Colour (0xff7a2a2a) : juce::Colour (0xff3a3a3a));
            g.fillRoundedRectangle (r, 6.0f);
            if (id == owner.current)
            {
                g.setColour (juce::Colours::white.withAlpha (0.6f));
                g.drawRoundedRectangle (r, 6.0f, 2.0f);
            }
            g.setColour (juce::Colours::white);
            g.drawText (juce::String (index + 1), r, juce::Justification::centred);
        }

        void mouseDown (const juce::MouseEvent&) override
        {
            owner.current = owner.padAt (index);
            Command c;
            c.type = Command::Type::padDown;
            c.pad = owner.current;
            owner.proc.getEngine().post (c);
            owner.refreshModeButtons();
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            Command c;
            c.type = Command::Type::padUp;
            c.pad = owner.padAt (index);
            owner.proc.getEngine().post (c);
        }

    private:
        PluginEditor& owner;
        int index;
    };

    PluginEditor::PluginEditor (PluginProcessor& p) : AudioProcessorEditor (p), proc (p)
    {
        for (int i = 0; i < kPadsPerBank; ++i)
            addAndMakeVisible (pads.add (new PadView (*this, i)));

        for (int b = 0; b < kAuthenticBanks; ++b)
        {
            auto* btn = bankButtons.add (new juce::TextButton (juce::String::charToString ((juce::juce_wchar) ('A' + b))));
            btn->setClickingTogglesState (false);
            btn->onClick = [this, b] { bank = b; current = { b, current.pad }; refreshModeButtons(); repaint(); };
            addAndMakeVisible (btn);
        }

        int which = 0;
        for (auto* b : { &gate, &loop, &reverse, &lofi })
        {
            b->setClickingTogglesState (false);
            b->onClick = [this, which] { toggleMode (which); };
            addAndMakeVisible (b);
            ++which;
        }

        hold.onStateChange = [this]
        {
            if (hold.isDown())
            {
                Command c;
                c.type = Command::Type::holdDown;
                proc.getEngine().post (c);
            }
        };
        sub.onStateChange = [this]
        {
            if (sub.isDown() != subWasDown)
            {
                subWasDown = sub.isDown();
                Command c;
                c.type = subWasDown ? Command::Type::subDown : Command::Type::subUp;
                proc.getEngine().post (c);
            }
        };
        stopAll.onClick = [this]
        {
            Command c;
            c.type = Command::Type::stopAll;
            proc.getEngine().post (c);
        };
        for (auto* b : { &hold, &sub, &stopAll })
            addAndMakeVisible (b);

        volume.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        volume.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        volumeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.getParameters(), "volume", volume);
        addAndMakeVisible (volume);

        status.setText ("Phase 1 test UI: drop WAV/AIFF files on pads", juce::dontSendNotification);
        addAndMakeVisible (status);

        setSize (640, 420);
        refreshModeButtons();
        startTimerHz (30);
    }

    PluginEditor::~PluginEditor() { stopTimer(); }

    void PluginEditor::paint (juce::Graphics& g)
    {
        g.fillAll (juce::Colour (0xff202020));
    }

    void PluginEditor::resized()
    {
        auto r = getLocalBounds().reduced (10);
        status.setBounds (r.removeFromBottom (24));

        auto top = r.removeFromTop (30);
        for (auto* b : bankButtons)
            b->setBounds (top.removeFromLeft (40).reduced (2));
        volume.setBounds (top.removeFromRight (60));

        r.removeFromTop (6);
        auto modes = r.removeFromTop (30);
        for (auto* b : { &gate, &loop, &reverse, &lofi })
            b->setBounds (modes.removeFromLeft (90).reduced (2));

        r.removeFromTop (6);
        auto side = r.removeFromRight (120);
        for (auto* b : { &hold, &sub, &stopAll })
            b->setBounds (side.removeFromTop (side.getHeight() / 3).reduced (4));

        const int w = r.getWidth() / 4, h = r.getHeight() / 3;
        for (int i = 0; i < kPadsPerBank; ++i)
            pads[i]->setBounds (r.getX() + (i % 4) * w, r.getY() + (i / 4) * h, w, h);
    }

    void PluginEditor::filesDropped (const juce::StringArray& files, int x, int y)
    {
        int start = 0;
        for (int i = 0; i < pads.size(); ++i)
            if (pads[i]->getBounds().contains (x, y))
                start = i;

        // Like the hardware import: consecutive pads from the one under the cursor.
        for (int i = 0; i < files.size() && start + i < kPadsPerBank; ++i)
        {
            const PadId id { bank, start + i };
            proc.getController().loadFileAsync (id, juce::File (files[i]), [this, id] (SamplerController::Status s)
            {
                status.setText (s == SamplerController::Status::ok ? "Loaded pad " + juce::String (id.pad + 1)
                                                                   : juce::String ("UnS: could not load file"),
                                juce::dontSendNotification);
                repaint();
            });
        }
    }

    void PluginEditor::toggleMode (int which)
    {
        proc.getController().editSettings (current, [which] (PadSettings& s)
        {
            switch (which)
            {
                case 0: s.gate = ! s.gate; break;
                case 1: s.loop = ! s.loop; break;
                case 2: s.reverse = ! s.reverse; break;
                case 3: s.lofi = ! s.lofi; break;
            }
        });
        refreshModeButtons();
    }

    void PluginEditor::refreshModeButtons()
    {
        const auto* s = proc.getController().settings (current);
        const bool has = s != nullptr && ! proc.getController().project().slot (current).isEmpty();
        auto lit = [] (juce::TextButton& b, bool on)
        {
            b.setColour (juce::TextButton::buttonColourId, on ? juce::Colour (0xffd04020) : juce::Colour (0xff404040));
        };
        lit (gate, has && s->gate);
        lit (loop, has && s->loop);
        lit (reverse, has && s->reverse);
        lit (lofi, has && s->lofi);
        for (int b = 0; b < bankButtons.size(); ++b)
            lit (*bankButtons[b], b == bank);
    }

    void PluginEditor::timerCallback()
    {
        for (auto* p : pads)
            p->repaint();
    }
}
