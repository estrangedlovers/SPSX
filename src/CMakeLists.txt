target_sources(SPSX PRIVATE
    core/engine/Engine.cpp
    core/engine/EngineHost.cpp
    core/engine/Voice.cpp
    core/io/AudioFileIO.cpp
    core/sampler/SampleEdit.cpp
    core/sampler/StretchRenderer.cpp
    core/state/Project.cpp
    core/SamplerController.cpp
    plugin/PluginProcessor.cpp
    plugin/PluginEditor.cpp)

target_include_directories(SPSX PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
