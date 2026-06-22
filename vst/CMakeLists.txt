cmake_minimum_required(VERSION 3.20)
project(CloudGreyVerbPlugin VERSION 1.0.0)

# ==============================================================================
# Fetch JUCE Framework (this will download JUCE when CMake runs)
# ==============================================================================
include(FetchContent)
FetchContent_Declare(
    juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.0
)
FetchContent_MakeAvailable(juce)

# ==============================================================================
# Configure the JUCE Plugin
# ==============================================================================
juce_add_plugin(CloudGreyVerbPlugin
    COMPANY_NAME "Alexandre Lunelli"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE AlxL
    PLUGIN_CODE ClGr
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "CloudGreyVerb"
)

target_sources(CloudGreyVerbPlugin
    PRIVATE
        PluginProcessor.cpp
        PluginProcessor.h
        PluginEditor.cpp
        PluginEditor.h
        ../src/dsp/cloud_grey_verb.cpp
        ../src/dsp/cloud_grey_verb.hpp
)

target_compile_definitions(CloudGreyVerbPlugin
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(CloudGreyVerbPlugin
    PRIVATE
        juce::juce_audio_plugin_client
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

target_include_directories(CloudGreyVerbPlugin PRIVATE ../src/dsp)
