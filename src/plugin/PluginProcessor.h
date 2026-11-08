#pragma once

#include "../core/engine/Engine.h"
#include "../core/engine/EngineHost.h"
#include "../core/SamplerController.h"
#include "../core/ui_model/PanelModel.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace spsx
{
    class PluginProcessor final : public juce::AudioProcessor, private juce::Timer
    {
    public:
        PluginProcessor();
        ~PluginProcessor() override;

        void prepareToPlay (double sampleRate, int samplesPerBlock) override;
        void releaseResources() override {}
        bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
        using AudioProcessor::processBlock;

        juce::AudioProcessorEditor* createEditor() override;
        bool hasEditor() const override { return true; }

        const juce::String getName() const override { return "SPSX"; }
        bool acceptsMidi() const override  { return true; }
        bool producesMidi() const override { return false; }
        bool isMidiEffect() const override { return false; }
        double getTailLengthSeconds() const override { return 0.0; }

        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return {}; }
        void changeProgramName (int, const juce::String&) override {}

        void getStateInformation (juce::MemoryBlock& destData) override;
        void setStateInformation (const void* data, int sizeInBytes) override;

        Engine& getEngine() noexcept { return engine; }
        PanelModel& getPanel() noexcept { return panel; }

        /** Authentic (hardware limits) or Extended mode. Message thread. */
        void setExtended (bool shouldBeExtended);
        bool isExtended() const noexcept { return extended; }

        /** Plugin FX processor mode: the track's audio runs through the effects. */
        void setFxMode (bool on) { panel.setFxProcessorMode (on); }
        bool isFxMode() const noexcept { return panel.isFxProcessorMode(); }
        std::function<void()> onModeChanged;

        static double nowSeconds() noexcept { return juce::Time::getMillisecondCounterHiRes() * 0.001; }
        SamplerController& getController() noexcept { return controller; }
        juce::AudioProcessorValueTreeState& getParameters() noexcept { return params; }

    private:
        void timerCallback() override;
        static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

        Engine engine;
        EngineHost host { engine };
        SamplerController controller { engine, Limits::authentic() };
        PanelModel panel { engine, controller };
        bool extended = false;
        juce::AudioProcessorValueTreeState params;
        std::atomic<float>* volumeParam = nullptr;
        float lastVolume = -1.0f;
        juce::AudioBuffer<float> inputScratch;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
    };
}
