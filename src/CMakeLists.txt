target_sources(SPSX PRIVATE
    core/engine/Engine.cpp
    core/engine/EngineHost.cpp
    core/engine/Voice.cpp
    core/engine/Sequencer.cpp
    core/midi/MidiInput.h
    core/midi/MidiFileExport.cpp
    core/io/AudioFileIO.cpp
    core/sampler/SampleEdit.cpp
    core/sampler/StretchRenderer.cpp
    core/state/Project.cpp
    core/SamplerController.cpp
    core/fx/EffectInfo.cpp
    core/fx/Effects.cpp
    core/fx/EffectsFilterDrive.cpp
    core/fx/EffectsTime.cpp
    core/fx/EffectsLofi.cpp
    core/fx/EffectsPitch.cpp
    core/fx/EffectsLoop.cpp
    core/fx/EffectHost.cpp
    core/ui_model/Controls.cpp
    core/ui_model/DisplayText.cpp
    core/ui_model/PanelModel.cpp
    ui/PanelLayout.cpp
    ui/PanelComponent.cpp
    ui/KeyMap.cpp
    ui/SidePanel.cpp
    plugin/PluginProcessor.cpp
    plugin/PluginEditor.cpp)

target_include_directories(SPSX PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
