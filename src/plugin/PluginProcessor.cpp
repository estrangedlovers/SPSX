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
        startTimerHz (30);
    }

    PluginProcessor::~PluginProcessor()
    {
        stopTimer();
    }

    void PluginProcessor::timerCallback()
    {
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
        lastVolume = -1.0f;
        // Engine::prepare keeps the pad table, so loaded samples survive a re-prepare.
    }

    void PluginProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
    {
        juce::ScopedNoDenormals noDenormals;
        midi.clear();   // MIDI note map arrives in Phase 6

        const float vol = volumeParam->load();
        if (! juce::exactlyEqual (vol, lastVolume))
        {
            Command c;
            c.type = Command::Type::setVolume;
            c.value = vol;
            host.scheduleAt (c, 0);
            lastVolume = vol;
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
                in[ch] = inputScratch.getReadPointer (ch);
            }
        }

        host.process (in, juce::jmin (2, numIn), buffer.getArrayOfWritePointers(), numOut, n);
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
        if (auto* vol = params.getParameter ("volume"))
            vol->setValueNotifyingHost ((float) v.getProperty ("volume", 0.8));

        // Phase 1: samples are referenced by absolute path. Embedding audio in the host
        // session is Phase 9.
        std::vector<std::pair<PadId, PadSlot>> toLoad;
        for (int i = 0; i < (int) p.pads.size(); ++i)
            if (p.pads[(size_t) i].file.isNotEmpty())
                toLoad.emplace_back (PadId::fromIndex (i), p.pads[(size_t) i]);

        controller.setProject (p);
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
