cmake_minimum_required(VERSION 3.22)

# must be set before project() to take effect
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(OpenChoir VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# JUCE: use a local checkout if given (-DJUCE_PATH=...), otherwise download it.
# ---------------------------------------------------------------------------
if(DEFINED JUCE_PATH)
    add_subdirectory(${JUCE_PATH} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.4
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

if(APPLE)
    set(OPENCHOIR_FORMATS VST3 AU Standalone)
else()
    set(OPENCHOIR_FORMATS VST3)
endif()

set(OPENCHOIR_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

# Only for headless Linux build boxes that lack some X11 extension headers.
option(OPENCHOIR_MINIMAL_X11 "Build without Xrandr/Xinerama/Xcursor (Linux CI only)" OFF)
if(OPENCHOIR_MINIMAL_X11)
    list(APPEND OPENCHOIR_DEFS JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0 JUCE_ALSA=0 JUCE_JACK=0)
endif()

# ---------------------------------------------------------------------------
# OpenChoir: the AI choir instrument (Vocal -> Choir -> Mix, drag the result out)
# ---------------------------------------------------------------------------
juce_add_plugin(OpenChoir
    COMPANY_NAME                "OpenChoir"
    BUNDLE_ID                   "org.openchoir.openchoir.ai"
    PLUGIN_MANUFACTURER_CODE    Opch
    PLUGIN_CODE                 Ocai
    PRODUCT_NAME                "OpenChoir"
    DESCRIPTION                 "Turns one vocal into a choir of real singers"
    FORMATS                     ${OPENCHOIR_FORMATS}
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Instrument
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice)

target_sources(OpenChoir PRIVATE
    Source/ai/AIProcessor.cpp
    Source/ai/AIEditor.cpp)
target_compile_definitions(OpenChoir PUBLIC ${OPENCHOIR_DEFS})
target_link_libraries(OpenChoir
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# OpenChoir Ensemble: the real-time DSP choir effect (instant, no AI)
# ---------------------------------------------------------------------------
juce_add_plugin(OpenChoirEnsemble
    COMPANY_NAME                "OpenChoir"
    BUNDLE_ID                   "org.openchoir.openchoir"
    PLUGIN_MANUFACTURER_CODE    Opch
    PLUGIN_CODE                 Ochr
    PRODUCT_NAME                "OpenChoir Ensemble"
    DESCRIPTION                 "Real-time choir and gang vocal effect"
    FORMATS                     ${OPENCHOIR_FORMATS}
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Fx Modulation
    AU_MAIN_TYPE                kAudioUnitType_Effect)

target_sources(OpenChoirEnsemble PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)
target_compile_definitions(OpenChoirEnsemble PUBLIC ${OPENCHOIR_DEFS})
target_link_libraries(OpenChoirEnsemble
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Optional: offline renderer that runs the engine on a WAV file.
#   openchoir_render in.wav out.wav [preset-index]
# ---------------------------------------------------------------------------
option(OPENCHOIR_BUILD_RENDER "Build the offline WAV renderer" OFF)
if(OPENCHOIR_BUILD_RENDER)
    juce_add_console_app(openchoir_render PRODUCT_NAME "openchoir_render")
    target_sources(openchoir_render PRIVATE Tools/Render.cpp)
    target_include_directories(openchoir_render PRIVATE Source)
    target_compile_definitions(openchoir_render PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(openchoir_render PRIVATE
        juce::juce_audio_formats juce::juce_dsp
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()

# ---------------------------------------------------------------------------
# Optional: self-tests (state, presets, layouts, clicks) + editor screenshot.
#   openchoir_tests screenshot.png
# ---------------------------------------------------------------------------
option(OPENCHOIR_BUILD_TESTS "Build the plugin self-tests" OFF)
if(OPENCHOIR_BUILD_TESTS)
    juce_add_console_app(openchoir_tests PRODUCT_NAME "openchoir_tests")
    target_sources(openchoir_tests PRIVATE Tools/PluginTests.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_include_directories(openchoir_tests PRIVATE Source)
    target_compile_definitions(openchoir_tests PRIVATE ${OPENCHOIR_DEFS} JucePlugin_Name="OpenChoir Ensemble")
    target_link_libraries(openchoir_tests PRIVATE juce::juce_audio_utils juce::juce_dsp
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

    # end-to-end test of the AI instrument: load vocal -> render (test engine) -> mix -> export
    juce_add_console_app(openchoir_ai_tests PRODUCT_NAME "openchoir_ai_tests")
    target_sources(openchoir_ai_tests PRIVATE Tools/AITests.cpp Source/ai/AIProcessor.cpp Source/ai/AIEditor.cpp)
    target_include_directories(openchoir_ai_tests PRIVATE Source/ai)
    target_compile_definitions(openchoir_ai_tests PRIVATE ${OPENCHOIR_DEFS} JucePlugin_Name="OpenChoir" JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(openchoir_ai_tests PRIVATE juce::juce_audio_utils juce::juce_dsp
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
