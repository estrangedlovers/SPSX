add_executable(SPSXTests
    AllocGuard.cpp
    TestResampler.cpp
    TestEngine.cpp
    TestEngineHost.cpp
    TestSampleEdit.cpp
    TestIO.cpp
    TestController.cpp
    TestMisc.cpp
    TestPanelModel.cpp
    TestEffects.cpp
    TestSampling.cpp
    TestSequencer.cpp)

spsx_use_shared_code(SPSXTests)
target_link_libraries(SPSXTests PRIVATE Catch2::Catch2WithMain)

include(${catch2_SOURCE_DIR}/extras/Catch.cmake)
catch_discover_tests(SPSXTests)
# Tests compare floats exactly on purpose (bit-exact paths). Source-level options are
# placed after JUCE's recommended warning flags, so they win.
get_target_property(spsx_test_sources SPSXTests SOURCES)
set_source_files_properties(${spsx_test_sources} PROPERTIES
    COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wno-float-equal>")
