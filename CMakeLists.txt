cmake_minimum_required(VERSION 3.24)

option(SPSX_UNIVERSAL     "Build a universal (arm64 + x86_64) macOS binary" OFF)
option(SPSX_BUILD_TESTS   "Build unit tests"                                ON)
option(SPSX_BUILD_TOOLS   "Build headless render tool"                      ON)

if(APPLE)
    if(SPSX_UNIVERSAL)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    elseif(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "" FORCE)
    endif()
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
endif()

project(SPSX VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/Dependencies.cmake)

# ---------------------------------------------------------------------------
# Plugin + standalone. juce_add_plugin creates the static "shared code"
# target `SPSX`; tests and tools link that target so JUCE modules are
# compiled exactly once.
# ---------------------------------------------------------------------------
set(SPSX_FORMATS Standalone VST3)
if(APPLE)
    list(APPEND SPSX_FORMATS AU)
endif()

juce_add_plugin(SPSX
    COMPANY_NAME                "SPSX"
    BUNDLE_ID                   "com.spsx.spsx"
    PRODUCT_NAME                "SPSX"
    PLUGIN_MANUFACTURER_CODE    Spsx
    PLUGIN_CODE                 Spsx
    FORMATS                     ${SPSX_FORMATS}
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice
    VST3_CATEGORIES             Instrument Sampler
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT  "SPSX uses the microphone as its built-in mic input for sampling.")

add_subdirectory(src)

target_compile_definitions(SPSX PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(SPSX
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
        signalsmith-stretch
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Targets that link the shared code (tests, tools) need JUCE's include paths and
# module definitions but must not compile the module sources a second time.
function(spsx_use_shared_code target)
    target_link_libraries(${target} PRIVATE SPSX)
    target_include_directories(${target} PRIVATE $<TARGET_PROPERTY:SPSX,INCLUDE_DIRECTORIES>)
    target_compile_definitions(${target} PRIVATE $<TARGET_PROPERTY:SPSX,COMPILE_DEFINITIONS>)
endfunction()

if(SPSX_BUILD_TOOLS)
    add_subdirectory(tools/render_cli)
endif()

if(SPSX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
