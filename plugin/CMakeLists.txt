cmake_minimum_required(VERSION 3.22)

# macOS: one universal binary (Apple Silicon + Intel), runs on 10.13 and later.
# Must be set before project().
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")
endif()

project(ModdedFkingCycles VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)

# JUCE: either point JUCE_DIR at a local checkout, or let CMake download it.
if(DEFINED JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 9.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND FORMATS AU)
endif()

juce_add_plugin(ModelCyclesCC
    COMPANY_NAME "m_reilly"
    PLUGIN_MANUFACTURER_CODE Mrly
    PLUGIN_CODE Mfkc
    PRODUCT_NAME "Modded_f-king_cycles"
    BUNDLE_ID "com.mreilly.moddedfkingcycles"
    FORMATS ${FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Tools)

target_sources(ModelCyclesCC PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/DeviceLink.cpp
    Source/AudioLink.cpp
    Source/ElektronFs.cpp
    Source/SampleLibrary.cpp
    Source/SampleView.cpp)

target_compile_definitions(ModelCyclesCC PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(ModelCyclesCC
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_devices
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# macOS: JUCE's VST3 post-build step (moduleinfo) breaks the bundle's signature,
# so ad-hoc re-sign both bundles at the end of the build.
if(APPLE)
    add_custom_command(TARGET ModelCyclesCC_VST3 POST_BUILD
        COMMAND codesign --force --deep --sign - "$<TARGET_BUNDLE_DIR:ModelCyclesCC_VST3>"
        VERBATIM)
    add_custom_command(TARGET ModelCyclesCC_AU POST_BUILD
        COMMAND codesign --force --deep --sign - "$<TARGET_BUNDLE_DIR:ModelCyclesCC_AU>"
        VERBATIM)
endif()

# Optional headless tests of the CC logic
option(BUILD_TESTS "Build the CC logic tests" OFF)
if(BUILD_TESTS)
    juce_add_console_app(ModelCyclesCC_Tests PRODUCT_NAME "ModelCyclesCC_Tests")
    target_sources(ModelCyclesCC_Tests PRIVATE
        Tests/ProcessorTests.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/DeviceLink.cpp Source/AudioLink.cpp
        Source/ElektronFs.cpp Source/SampleLibrary.cpp Source/SampleView.cpp)
    target_compile_definitions(ModelCyclesCC_Tests PRIVATE
        JucePlugin_Name="Modded_f-king_cycles" JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(ModelCyclesCC_Tests PRIVATE
        juce::juce_audio_utils juce::juce_audio_devices juce::juce_recommended_config_flags)
endif()
