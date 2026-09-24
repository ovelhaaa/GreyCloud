cmake_minimum_required(VERSION 3.20)
project(NimbusReverbPlugin VERSION 1.0.0)
enable_testing()

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
juce_add_plugin(NimbusReverbPlugin
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
    PRODUCT_NAME "Nimbus"
)

juce_generate_juce_header(NimbusReverbPlugin)

juce_add_binary_data(NimbusAssets
    SOURCES
        nimbus_logo_custom.svg
)

target_sources(NimbusReverbPlugin
    PRIVATE
        PluginProcessor.cpp
        PluginProcessor.h
        PluginEditor.cpp
        PluginEditor.h
        LookAndFeel.cpp
        ../src/dsp/cloud_grey_verb.cpp
        ../src/dsp/cloud_grey_verb.hpp
)

target_compile_definitions(NimbusReverbPlugin
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        CLOUD_GREY_PROFILE_DESKTOP_STUDIO=1
)
target_compile_features(NimbusReverbPlugin PRIVATE cxx_std_17)

target_link_libraries(NimbusReverbPlugin
    PRIVATE
        NimbusAssets
        juce::juce_audio_plugin_client
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_gui_basics
        juce::juce_gui_extra
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

target_include_directories(NimbusReverbPlugin PRIVATE ../src/dsp)

add_executable(CloudGreyVerbMilestoneTest
    ../src/dsp/cloud_grey_verb.cpp
    ../src/dsp/cloud_grey_verb_milestone_test.cpp
)
target_compile_features(CloudGreyVerbMilestoneTest PRIVATE cxx_std_17)
target_compile_definitions(CloudGreyVerbMilestoneTest PRIVATE CLOUD_GREY_PROFILE_DESKTOP_STUDIO=1)
target_include_directories(CloudGreyVerbMilestoneTest PRIVATE ../src/dsp)
add_test(NAME CloudGreyVerbMilestoneTest COMMAND CloudGreyVerbMilestoneTest)
set_tests_properties(CloudGreyVerbMilestoneTest PROPERTIES TIMEOUT 120)

function(add_cgv_m2_profile_test target profile)
    add_executable(${target}
        ../src/dsp/cloud_grey_verb.cpp
        ../src/dsp/cloud_grey_verb_m2_regression_test.cpp
    )
    target_compile_features(${target} PRIVATE cxx_std_17)
    target_compile_definitions(${target} PRIVATE ${profile}=1)
    target_include_directories(${target} PRIVATE ../src/dsp)
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES TIMEOUT 120)
endfunction()
add_cgv_m2_profile_test(CloudGreyVerbM2Test_LowCPU CLOUD_GREY_PROFILE_H5_LOW_CPU)
add_cgv_m2_profile_test(CloudGreyVerbM2Test_Balanced CLOUD_GREY_PROFILE_H5_BALANCED)
add_cgv_m2_profile_test(CloudGreyVerbM2Test_H7 CLOUD_GREY_PROFILE_H7_HIGH_QUALITY)
add_cgv_m2_profile_test(CloudGreyVerbM2Test_Desktop CLOUD_GREY_PROFILE_DESKTOP_STUDIO)
