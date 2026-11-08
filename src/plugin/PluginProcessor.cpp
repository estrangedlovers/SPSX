#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace spsx
{
    PluginProcessor::PluginProcessor()
        : AudioProcessor (BusesProperties()
                              .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                              .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          params (*this, nullptr, "SPSX", createLayout())
    {
        volumeParam = params.getRawParameterValue ("volume");

        PanelModel::Callbacks cb;
        cb.setVolume = [this] (double v)
        {
            if (auto* p = params.getParameter ("volume"))
                p->setValueNotifyingHost ((float) v);
        };
        cb.freeBytes = []
        {
            return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getBytesFreeOnVolume();
        };
        panel.setCallbacks (cb);
        panel.setKnobPosition (Knob::volume, volumeParam->load());
        panel.sendMidiSettings();
        startTimerHz (60);
    }

    PluginProcessor::~PluginProcessor()
    {
        stopTimer();
    }

    void PluginProcessor::timerCallback()
    {
        // EXT SOURCE from MIDI note 46 goes through the panel so its LED and GATE
        // behaviour match a press of the pad (one UI frame late; SPEC U53).
        const auto now = nowSeconds();
        uiFifo.read (uiFifo.getNumReady()).forEach ([this, now] (int i)
        {
            const auto& e = uiEvents[(size_t) i];
            switch (e.kind)
            {
                case 0:  panel.release (Control::extSource, now); break;
                case 1:  panel.press (Control::extSource, now); break;
                default: panel.handleMidiControl (e.channel, e.number, e.value, now); break;
            }
        });
        controller.tick();
        panel.tick (nowSeconds());
        // Keep the VOLUME knob in step with host automation.
        panel.setKnobPosition (Knob::volume, volumeParam->load());
    }

    void PluginProcessor::setExtended (bool shouldBeExtended)
    {
        if (extended == shouldBeExtended)
            return;
        extended = shouldBeExtended;
        controller.setLimits (extended ? Limits::extended() : Limits::authentic());
        panel.sendMidiSettings();
        if (onModeChanged)
            onModeChanged();
    }

    juce::AudioProcessorValueTreeState::ParameterLayout PluginProcessor::createLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "volume", 1 }, "Volume",
                                                                 juce::NormalisableRange<float> (0.0f, 1.0f), 0.8f));
        return layout;
    }

    bool PluginProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        const auto out = layouts.getMainOutputChannelSet();
        if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
            return false;
        const auto in = layouts.getMainInputChannelSet();
        return in.isDisabled() || in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono();
    }

    void PluginProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
    {
        host.prepare (sampleRate, samplesPerBlock, controller.limits());
        inputScratch.setSize (2, samplesPerBlock);
        setLatencySamples (host.latencySamples());
        midiIn.prepare (sampleRate);
        lastVolume = -1.0f;
        // Engine::prepare keeps the pad table, so loaded samples survive a re-prepare.
    }

    void PluginProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
    {
        juce::ScopedNoDenormals noDenormals;

        const float vol = volumeParam->load();
        if (! juce::exactlyEqual (vol, lastVolume))
        {
            Command c;
            c.type = Command::Type::setVolume;
            c.value = vol;
            host.scheduleAt (c, 0);
            lastVolume = vol;
        }

        // Host transport (plugin only): the DAW's tempo and play state, used when no MIDI
        // clock is coming in.
        hostPos = {};
        if (wrapperType != wrapperType_Standalone)
            if (auto* ph = getPlayHead())
                if (auto pos = ph->getPosition())
                {
                    hostPos.playing = pos->getIsPlaying();
                    if (auto b = pos->getBpm()) hostPos.bpm = *b;
                    if (auto q = pos->getPpqPosition()) hostPos.ppq = *q;
                    hostPos.valid = hostPos.bpm > 0.0;
                }

        const int numIn = getTotalNumInputChannels();
        const int numOut = getTotalNumOutputChannels();
        const int n = buffer.getNumSamples();

        // In and out share the buffer: copy the input aside first.
        const float* in[2] = { nullptr, nullptr };
        if (numIn > 0)
        {
            // Sized in prepareToPlay; setSize only reallocates if a host exceeds the announced block.
            inputScratch.setSize (juce::jmin (2, numIn), n, false, false, true);
            for (int ch = 0; ch < juce::jmin (2, numIn); ++ch)
            {
                inputScratch.copyFrom (ch, 0, buffer, ch, 0, n);
                // The host's audio is untrusted (FX Mode, sampling): no NaN / Inf inside.
                auto* x = inputScratch.getWritePointer (ch);
                for (int i = 0; i < n; ++i)
                    if (! std::isfinite (x[i]))
                        x[i] = 0.0f;
                in[ch] = inputScratch.getReadPointer (ch);
            }
        }

        handleMidi (midi, in, juce::jmin (2, numIn), buffer.getArrayOfWritePointers(), numOut, n);
        midi.clear();
    }

    void PluginProcessor::applyTransport (int offset) noexcept
    {
        // MIDI clock wins while it is running; otherwise the DAW's transport. Both obey
        // MIDI SYNC: AUt = tempo + start/stop, tNp = tempo only, oFF = neither (SPEC U50).
        auto tr = midiIn.transportAt (offset);
        if (! tr.follow && hostPos.valid)
        {
            const auto& s = midiIn.getSettings();
            if (s.sync != MidiSettings::off)
            {
                tr.follow = true;
                tr.bpm = juce::jlimit (s.minBpm, s.maxBpm, hostPos.bpm);
                tr.playing = s.sync == MidiSettings::autoSync && hostPos.playing;
                tr.ppq = hostPos.ppq;
            }
        }
        engine.setHostTransport (tr.follow, tr.playing, tr.bpm, tr.ppq);
    }

    void PluginProcessor::handleMidi (const juce::MidiBuffer& midi, const float* const* in, int numIn,
                                      float* const* out, int numOut, int n) noexcept
    {
        midiIn.setSettings (engine.midiSettings());

        int done = 0;   // device samples already rendered this block
        auto sink = [this, &done] (const MidiInput::Event& e)
        {
            using K = MidiInput::Event::Kind;
            if (e.kind == K::extDown || e.kind == K::extUp || e.kind == K::controlChange)
            {
                UiMidi u;
                u.kind = e.kind == K::extUp ? 0 : e.kind == K::extDown ? 1 : 2;
                u.channel = (std::uint8_t) e.channel;
                u.number = (std::uint8_t) e.number;
                u.value = (std::uint8_t) e.value;
                uiFifo.write (1).forEach ([this, u] (int i) { uiEvents[(size_t) i] = u; });   // dropped if full
                return;
            }
            Command c;
            c.type = e.kind == K::padDown ? Command::Type::padDown
                   : e.kind == K::padUp   ? Command::Type::padUp
                                          : Command::Type::stopAll;
            c.pad = e.pad;
            c.velocity = e.velocity;
            c.index = e.oneShot ? 2 : 0;
            host.scheduleAt (c, juce::jmax (0, e.offset - done));
        };

        auto render = [&] (int from, int to)
        {
            if (to <= from)
                return;
            const float* inSeg[2] = { in[0] != nullptr ? in[0] + from : nullptr, in[1] != nullptr ? in[1] + from : nullptr };
            float* outSeg[8] {};
            for (int ch = 0; ch < juce::jmin (numOut, 8); ++ch)
                outSeg[ch] = out[ch] + from;
            applyTransport (from);
            host.process (inSeg, numIn, outSeg, juce::jmin (numOut, 8), to - from);
            done = to;
        };

        midiIn.beginBlock (n, sink);
        for (const auto meta : midi)
        {
            const int offset = juce::jlimit (0, juce::jmax (0, n - 1), meta.samplePosition);
            // Start / Stop / Continue / Song Position split the block so the sequencer
            // starts and stops on the message's sample.
            const auto& m = meta.getMessage();
            if ((m.isMidiStart() || m.isMidiStop() || m.isMidiContinue() || m.isSongPositionPointer()) && offset > done)
                render (done, offset);
            midiIn.handle (m, offset, sink);
        }
        render (done, n);
        for (int ch = juce::jmin (numOut, 8); ch < numOut; ++ch)
            juce::FloatVectorOperations::clear (out[ch], n);
        midiIn.endBlock();
    }

    juce::AudioProcessorEditor* PluginProcessor::createEditor()
    {
        return new PluginEditor (*this);
    }

    void PluginProcessor::getStateInformation (juce::MemoryBlock& destData)
    {
        auto* root = new juce::DynamicObject();
        root->setProperty ("project", controller.project().toVar());
        root->setProperty ("volume", volumeParam->load());
        root->setProperty ("extended", extended);
        root->setProperty ("fxMode", isFxMode());
        const auto json = juce::JSON::toString (juce::var (root));
        destData.replaceAll (json.toRawUTF8(), json.getNumBytesAsUTF8());
    }

    void PluginProcessor::setStateInformation (const void* data, int sizeInBytes)
    {
        const auto v = juce::JSON::parse (juce::String::fromUTF8 (static_cast<const char*> (data), sizeInBytes));
        Project p;
        if (Project::fromVar (v.getProperty ("project", {}), p).failed())
            return;

        setExtended ((bool) v.getProperty ("extended", false));
        setFxMode ((bool) v.getProperty ("fxMode", false));
        if (auto* vol = params.getParameter ("volume"))
            vol->setValueNotifyingHost ((float) v.getProperty ("volume", 0.8));

        // Phase 1: samples are referenced by absolute path. Embedding audio in the host
        // session is Phase 9.
        std::vector<std::pair<PadId, PadSlot>> toLoad;
        for (int i = 0; i < (int) p.pads.size(); ++i)
            if (p.pads[(size_t) i].file.isNotEmpty())
                toLoad.emplace_back (PadId::fromIndex (i), p.pads[(size_t) i]);

        controller.setProject (p);
        panel.syncEngine();
        for (auto& [pad, slot] : toLoad)
        {
            const auto settings = slot.settings;
            controller.loadFileAsync (pad, juce::File (slot.file), [this, pad, settings] (SamplerController::Status s)
            {
                if (s == SamplerController::Status::ok)
                    controller.editSettings (pad, [&settings] (PadSettings& ps) { ps = settings; });
            });
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new spsx::PluginProcessor();
}
