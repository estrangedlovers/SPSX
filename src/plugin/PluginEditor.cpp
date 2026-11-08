#include "PluginEditor.h"
#include "../ui/PanelLayout.h"
#include "../core/midi/MidiFileExport.h"

namespace spsx
{
    PluginEditor::PluginEditor (PluginProcessor& p)
        : AudioProcessorEditor (p), proc (p), model (p.getPanel()),
          panel (p.getPanel()), side (p.getPanel(), p.getController(), p.getEngine())
    {
        addAndMakeVisible (panel);
        addChildComponent (side);

        for (auto* b : { &authenticButton, &extendedButton })
        {
            b->setRadioGroupId (1);
            b->setClickingTogglesState (true);
            b->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd04020));
            b->setWantsKeyboardFocus (false);
            addAndMakeVisible (b);
        }
        authenticButton.setToggleState (! proc.isExtended(), juce::dontSendNotification);
        extendedButton.setToggleState (proc.isExtended(), juce::dontSendNotification);
        authenticButton.onClick = [this] { if (authenticButton.getToggleState()) proc.setExtended (false); };
        extendedButton.onClick  = [this] { if (extendedButton.getToggleState())  proc.setExtended (true); };

        keysButton.setClickingTogglesState (true);
        keysButton.setWantsKeyboardFocus (false);
        keysButton.onClick = [this] { panel.setShowKeyLabels (keysButton.getToggleState()); };
        addAndMakeVisible (keysButton);

        // FX Mode only makes sense inside a host: the track's audio becomes EXT SOURCE.
        fxModeButton.setClickingTogglesState (true);
        fxModeButton.setWantsKeyboardFocus (false);
        fxModeButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd04020));
        fxModeButton.setTooltip ("Run the host track's audio through the effects (EXT SOURCE always on)");
        fxModeButton.setToggleState (proc.isFxMode(), juce::dontSendNotification);
        fxModeButton.onClick = [this] { proc.setFxMode (fxModeButton.getToggleState()); };
        if (proc.wrapperType != juce::AudioProcessor::wrapperType_Standalone)
            addAndMakeVisible (fxModeButton);

        // Extended MIDI: learn, velocity curve, pattern export.
        midiButton.setWantsKeyboardFocus (false);
        midiButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd04020));
        midiButton.setTooltip ("MIDI learn, velocity curve and pattern export (Extended)");
        midiButton.onClick = [this] { showMidiMenu(); };
        addChildComponent (midiButton);
        panel.onLearnTargetChanged = [this] { updateLearnStatus(); };

        status.setColour (juce::Label::textColourId, juce::Colour (0xff8d8e8a));
        status.setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (status);

        // Key labels for the overlay.
        std::map<Knob, juce::String> knobLabels { { Knob::ctrl1, "Left/Right" }, { Knob::volume, "- =" } };
        panel.setKeyLabels (keyMap.labels(), knobLabels);

        auto callbacks = PanelModel::Callbacks {};
        callbacks.setVolume = [this] (double v)
        {
            if (auto* param = proc.getParameters().getParameter ("volume"))
                param->setValueNotifyingHost ((float) v);
        };
        callbacks.freeBytes = []
        {
            return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getBytesFreeOnVolume();
        };
        callbacks.requestImport = [this] (PadId start) { openImportChooser (start); };
        model.setCallbacks (callbacks);

        proc.onModeChanged = [this]
        {
            authenticButton.setToggleState (! proc.isExtended(), juce::dontSendNotification);
            extendedButton.setToggleState (proc.isExtended(), juce::dontSendNotification);
            updateLayoutForMode();
        };

        setWantsKeyboardFocus (true);
        setResizable (true, true);
        setConstrainer (&constrainer);
        updateLayoutForMode();
    }

    PluginEditor::~PluginEditor()
    {
        releaseAllKeys();
        proc.onModeChanged = nullptr;
        // Keep VOLUME and free-space callbacks; drop the one that needs this window.
        auto callbacks = PanelModel::Callbacks {};
        callbacks.setVolume = [&p = proc] (double v)
        {
            if (auto* param = p.getParameters().getParameter ("volume"))
                param->setValueNotifyingHost ((float) v);
        };
        callbacks.freeBytes = []
        {
            return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getBytesFreeOnVolume();
        };
        model.setCallbacks (callbacks);
    }

    void PluginEditor::updateLayoutForMode()
    {
        const bool ext = proc.isExtended();
        side.setVisible (ext);
        midiButton.setVisible (ext);
        if (! ext && panel.isLearnMode())
        {
            panel.setLearnMode (false);
            model.armMidiLearn (std::nullopt);
            midiButton.setToggleState (false, juce::dontSendNotification);
            updateLearnStatus();
        }

        // Keep the panel's 1000 x 1450 aspect; the side panel adds a fixed-width column
        // scaled with the panel.
        const double panelAspect = ui::PanelLayout::kWidth / ui::PanelLayout::kHeight;
        const int panelH = getHeight() > kBarHeight ? getHeight() - kBarHeight : 812;
        const int panelW = (int) std::round (panelH * panelAspect);
        const int sideW = ext ? kSideWidth : 0;

        constrainer.setMinimumSize ((int) (420 * panelAspect) + sideW, 420 + kBarHeight);
        constrainer.setMaximumSize (4000, 4000);
        // Aspect is approximate with the side panel: keep it on the panel part.
        constrainer.setFixedAspectRatio ((double) (panelW + sideW) / (double) (panelH + kBarHeight));

        setSize (panelW + sideW, panelH + kBarHeight);
        resized();
    }

    void PluginEditor::paint (juce::Graphics& g)
    {
        g.fillAll (juce::Colour (0xff101012));
        g.setColour (juce::Colour (0xff1c1d20));
        g.fillRect (getLocalBounds().removeFromTop (kBarHeight));
    }

    void PluginEditor::resized()
    {
        auto r = getLocalBounds();
        auto bar = r.removeFromTop (kBarHeight).reduced (6, 4);
        authenticButton.setBounds (bar.removeFromLeft (90));
        extendedButton.setBounds (bar.removeFromLeft (90));
        bar.removeFromLeft (8);
        keysButton.setBounds (bar.removeFromLeft (80));
        if (fxModeButton.isVisible())
        {
            bar.removeFromLeft (8);
            fxModeButton.setBounds (bar.removeFromLeft (80));
        }
        if (midiButton.isVisible())
        {
            bar.removeFromLeft (8);
            midiButton.setBounds (bar.removeFromLeft (64));
        }
        status.setBounds (bar);

        if (proc.isExtended())
            side.setBounds (r.removeFromRight (kSideWidth));
        panel.setBounds (r);
    }

    void PluginEditor::showMidiMenu()
    {
        juce::PopupMenu m;
        m.addItem ("MIDI learn (click a knob or button, then move a controller)", true, panel.isLearnMode(), [this]
        {
            panel.setLearnMode (! panel.isLearnMode());
            if (! panel.isLearnMode())
                model.armMidiLearn (std::nullopt);
            midiButton.setToggleState (panel.isLearnMode(), juce::dontSendNotification);
            updateLearnStatus();
        });
        const auto mappings = (int) proc.getController().project().device.midiMap.size();
        m.addItem ("Clear MIDI mappings (" + juce::String (mappings) + ")", mappings > 0, false, [this]
        {
            model.clearMidiMap();
            updateLearnStatus();
        });
        m.addSeparator();
        juce::PopupMenu curves;
        const int curve = proc.getController().project().device.velocityCurve;
        const char* names[] = { "Fixed (full level)", "Linear", "Soft" };
        for (int i = 0; i < 3; ++i)
            curves.addItem (names[i], true, curve == i, [this, i] { model.setVelocityCurve (i); });
        m.addSubMenu ("Velocity curve", curves);
        m.addSeparator();
        m.addItem ("Export patterns as MIDI files...", [this] { exportPatterns(); });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (midiButton));
    }

    void PluginEditor::updateLearnStatus()
    {
        if (! panel.isLearnMode())
        {
            status.setText ({}, juce::dontSendNotification);
            return;
        }
        if (auto t = model.midiLearnTarget())
            status.setText (juce::String ("Learn: move a controller for ")
                                + (t->knob ? knobName ((Knob) t->id) : controlName ((Control) t->id)),
                            juce::dontSendNotification);
        else
            status.setText ("Learn: click a knob or button", juce::dontSendNotification);
    }

    void PluginEditor::exportPatterns()
    {
        chooser = std::make_unique<juce::FileChooser> ("Export patterns to folder",
                                                       juce::File::getSpecialLocation (juce::File::userMusicDirectory));
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [this] (const juce::FileChooser& fc)
        {
            const auto dir = fc.getResult();
            if (! dir.isDirectory())
                return;
            const auto& project = proc.getController().project();
            int written = 0;
            for (int i = 0; i < (int) project.patterns.size(); ++i)
            {
                const auto& pat = project.patterns[(size_t) i];
                if (pat == nullptr)
                    continue;
                const auto id = PadId::fromIndex (i);
                const auto name = juce::String ("Pattern ") + juce::String::charToString ((juce::juce_wchar) ('A' + id.bank))
                                  + juce::String (id.pad + 1) + ".mid";
                if (midi::exportPattern (*pat, project.device.midiChannel, dir.getChildFile (name)))
                    ++written;
            }
            status.setText (juce::String (written) + (written == 1 ? " pattern" : " patterns") + " exported",
                            juce::dontSendNotification);
        });
    }

    void PluginEditor::onFrame()
    {
        if (panel.isLearnMode() && ! model.midiLearnTarget().has_value() && status.getText().startsWith ("Learn: move"))
            updateLearnStatus();   // a controller was bound
        panel.refresh (now());
        if (side.isVisible())
            side.refresh();
    }

    // ---- keyboard ---------------------------------------------------------------------

    bool PluginEditor::keyPressed (const juce::KeyPress& key)
    {
        const auto mods = key.getModifiers();
        if (mods.isCommandDown())
            return false;   // Cmd shortcuts (save/open/export/settings) arrive with projects (Phase 7/9)

        const int code = key.getKeyCode();
        const double t = now();

        if (key.getTextCharacter() == '?' || (code == '/' && mods.isShiftDown()))
        {
            keysButton.setToggleState (! keysButton.getToggleState(), juce::sendNotification);
            return true;
        }

        // Shift + Z X C V B: banks F..J in one press.
        const char bankKeys[] = { 'Z', 'X', 'C', 'V', 'B' };
        for (int i = 0; i < 5; ++i)
        {
            if (code == bankKeys[i])
            {
                model.selectBank (mods.isShiftDown() ? i + 5 : i, t);
                return true;
            }
        }

        // Knob selection and adjustment.
        const Knob order[] = { Knob::ctrl1, Knob::ctrl2, Knob::ctrl3, Knob::volume };
        auto indexOf = [&order] (Knob k) { for (int i = 0; i < 4; ++i) if (order[i] == k) return i; return 0; };
        if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey)
        {
            const int i = (indexOf (selectedKnob) + (code == juce::KeyPress::rightKey ? 1 : 3)) % 4;
            selectedKnob = order[i];
            knobSelectionVisible = true;
            panel.setSelectedKnob (selectedKnob);
            return true;
        }
        if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
        {
            const double step = (mods.isShiftDown() ? 1.0 : 4.0) / 127.0;
            const double dir = code == juce::KeyPress::upKey ? 1.0 : -1.0;
            model.turn (selectedKnob, model.knob (selectedKnob) + dir * step, t);
            knobSelectionVisible = true;
            panel.setSelectedKnob (selectedKnob);
            return true;
        }
        if (code == '-' || code == '=')
        {
            model.turn (Knob::volume, model.knob (Knob::volume) + (code == '=' ? 1.0 : -1.0) * 4.0 / 127.0, t);
            return true;
        }

        // Held controls are handled in keyStateChanged; claim the key so the host ignores it.
        return keyMap.controlFor (code).has_value();
    }

    bool PluginEditor::keyStateChanged (bool)
    {
        const double t = now();
        bool handled = false;

        for (auto& b : keyMap.bindings())
        {
            if (isBank (b.control))
                continue;   // banks select on keyPressed (direct, with Shift for F..J)

            const bool down = juce::KeyPress::isKeyCurrentlyDown (b.keyCode);
            const bool wasDown = keysDown.count (b.keyCode) > 0;
            if (down == wasDown)
                continue;

            // '/' with Shift is the overlay, not REMAIN.
            if (b.control == Control::remain && down && juce::ModifierKeys::currentModifiers.isShiftDown())
                continue;
            if (down && juce::ModifierKeys::currentModifiers.isCommandDown())
                continue;

            handled = true;
            if (down)
            {
                keysDown.insert (b.keyCode);
                // Backspace and Delete share DEL: only one press.
                if (b.control == Control::del && (keysDown.count (juce::KeyPress::backspaceKey) + keysDown.count (juce::KeyPress::deleteKey)) > 1)
                    continue;
                model.press (b.control, t);
            }
            else
            {
                keysDown.erase (b.keyCode);
                if (b.control == Control::del && (keysDown.count (juce::KeyPress::backspaceKey) + keysDown.count (juce::KeyPress::deleteKey)) > 0)
                    continue;
                model.release (b.control, t);
            }
        }
        if (handled)
            panel.refresh (t);
        return handled;
    }

    void PluginEditor::modifierKeysChanged (const juce::ModifierKeys& mods)
    {
        // Option = FUNC, held like the hardware button.
        if (mods.isAltDown() != altDown)
        {
            altDown = mods.isAltDown();
            if (altDown) model.press (Control::func, now());
            else         model.release (Control::func, now());
            panel.refresh (now());
        }
    }

    void PluginEditor::releaseAllKeys()
    {
        const double t = now();
        for (int code : keysDown)
            if (auto c = keyMap.controlFor (code))
                model.release (*c, t);
        keysDown.clear();
        if (altDown)
        {
            model.release (Control::func, t);
            altDown = false;
        }
    }

    void PluginEditor::focusLost (FocusChangeType)
    {
        // A key released while another window had focus would otherwise stay held.
        releaseAllKeys();
    }

    // ---- import -----------------------------------------------------------------------

    void PluginEditor::openImportChooser (PadId start)
    {
        chooser = std::make_unique<juce::FileChooser> ("Import audio files", juce::File(), "*.wav;*.aif;*.aiff;*.flac");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectMultipleItems,
                              [this, start] (const juce::FileChooser& fc) { importFiles (start, fc.getResults()); });
    }

    void PluginEditor::importFiles (PadId start, juce::Array<juce::File> files)
    {
        // Like the hardware's IMPORT (p. 36): ASCII filename order, consecutive pads from
        // the chosen one, protected banks skipped, at most 120 files.
        std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
        {
            return a.getFileName().toStdString() < b.getFileName().toStdString();
        });

        auto& c = proc.getController();
        int index = start.index();
        int done = 0;
        for (auto& f : files)
        {
            while (index < c.project().numBanks * kPadsPerBank && c.project().sampleBankProtected[(size_t) (index / kPadsPerBank)])
                index += kPadsPerBank - index % kPadsPerBank;
            if (index >= c.project().numBanks * kPadsPerBank || done >= 120)
                break;

            const auto pad = PadId::fromIndex (index++);
            ++done;
            c.loadFileAsync (pad, f, [this, pad] (SamplerController::Status s)
            {
                status.setText (s == SamplerController::Status::ok
                                    ? "Loaded " + juce::String::charToString ((juce::juce_wchar) ('A' + pad.bank)) + juce::String (pad.pad + 1)
                                    : juce::String ("UnS: could not load a file"),
                                juce::dontSendNotification);
            });
        }
    }

    void PluginEditor::filesDropped (const juce::StringArray& files, int x, int y)
    {
        // Drop onto a pad: load there and onwards (same rules as IMPORT).
        const auto local = panel.getLocalPoint (this, juce::Point<int> (x, y)).toFloat();
        const auto logical = local.transformedBy (panel.toScreen().inverted());
        int padIndex = 0;
        if (auto* g = ui::PanelLayout::get().controlAt (logical); g != nullptr && isPad (g->control))
            padIndex = spsx::padIndex (g->control);

        juce::Array<juce::File> list;
        for (auto& f : files)
            list.add (juce::File (f));
        importFiles ({ model.bank(), padIndex }, list);
    }
}
