# All third-party code is pinned here. Licenses are listed in THIRD_PARTY.md.
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# JUCE: used under its personal-use terms (see THIRD_PARTY.md).
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.17-security
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)

# signalsmith-stretch (MIT): Extended-mode Time Modify. Pulls signalsmith-linear (MIT).
FetchContent_Declare(signalsmith-stretch
    GIT_REPOSITORY https://github.com/Signalsmith-Audio/signalsmith-stretch.git
    GIT_TAG        1.4.0
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(signalsmith-stretch)

if(SPSX_BUILD_TESTS)
    # Catch2 (BSL-1.0)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.8.1
        GIT_SHALLOW    ON)
    FetchContent_MakeAvailable(Catch2)
endif()
