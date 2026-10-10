cmake_minimum_required(VERSION 3.20)
project(CloudGreyVerbDSP LANGUAGES CXX)

include(CTest)

if(BUILD_TESTING)
    add_executable(CloudGreyVerbGranularBench cloud_grey_verb.cpp cloud_grey_verb_granular_bench.cpp)
    target_compile_features(CloudGreyVerbGranularBench PRIVATE cxx_std_17)
    target_compile_definitions(CloudGreyVerbGranularBench PRIVATE CLOUD_GREY_PROFILE_DESKTOP_STUDIO=1)
    foreach(profile IN ITEMS DESKTOP_STUDIO H5_LOW_CPU H5_BALANCED H7_HIGH_QUALITY)
        if(profile STREQUAL "DESKTOP_STUDIO")
            set(continuity_target CloudGreyVerbGranularContinuityTest)
        else()
            set(continuity_target CloudGreyVerbGranularContinuityTest_${profile})
        endif()
        add_executable(${continuity_target} cloud_grey_verb.cpp cloud_grey_verb_granular_continuity_test.cpp)
        target_compile_features(${continuity_target} PRIVATE cxx_std_17)
        target_compile_definitions(${continuity_target} PRIVATE CLOUD_GREY_PROFILE_${profile}=1)
        add_test(NAME ${continuity_target} COMMAND ${continuity_target} ${continuity_target}_after)
        set_tests_properties(${continuity_target} PROPERTIES TIMEOUT 600)
    endforeach()
    add_executable(CloudGreyVerbMilestoneTest
        cloud_grey_verb.cpp
        cloud_grey_verb_milestone_test.cpp
    )
    target_compile_features(CloudGreyVerbMilestoneTest PRIVATE cxx_std_17)
    target_compile_definitions(CloudGreyVerbMilestoneTest PRIVATE CLOUD_GREY_PROFILE_DESKTOP_STUDIO=1)
    add_test(NAME CloudGreyVerbMilestoneTest COMMAND CloudGreyVerbMilestoneTest)
    set_tests_properties(CloudGreyVerbMilestoneTest PROPERTIES TIMEOUT 120)

    add_executable(CloudGreyVerbFactoryPresetParityTest cloud_grey_verb.cpp cloud_grey_verb_factory_preset_test.cpp)
    target_compile_features(CloudGreyVerbFactoryPresetParityTest PRIVATE cxx_std_17)
    target_compile_definitions(CloudGreyVerbFactoryPresetParityTest PRIVATE CLOUD_GREY_PROFILE_DESKTOP_STUDIO=1)
    add_test(NAME CloudGreyVerbFactoryPresetParityTest COMMAND CloudGreyVerbFactoryPresetParityTest)

    # reverseMix must be a direction crossfade only: it may never shift the
    # nominal granular pitch or stall the read head at intermediate values.
    add_executable(CloudGreyVerbReverseMixTest cloud_grey_verb.cpp cloud_grey_verb_reverse_mix_test.cpp)
    target_compile_features(CloudGreyVerbReverseMixTest PRIVATE cxx_std_17)
    target_compile_definitions(CloudGreyVerbReverseMixTest PRIVATE CLOUD_GREY_PROFILE_DESKTOP_STUDIO=1)
    add_test(NAME CloudGreyVerbReverseMixTest COMMAND CloudGreyVerbReverseMixTest)
    set_tests_properties(CloudGreyVerbReverseMixTest PROPERTIES TIMEOUT 120)

    function(add_cgv_m2_profile_test target profile)
        add_executable(${target} cloud_grey_verb.cpp cloud_grey_verb_m2_regression_test.cpp)
        target_compile_features(${target} PRIVATE cxx_std_17)
        target_compile_definitions(${target} PRIVATE ${profile}=1)
        add_test(NAME ${target} COMMAND ${target})
        set_tests_properties(${target} PROPERTIES TIMEOUT 120)
    endfunction()
    add_cgv_m2_profile_test(CloudGreyVerbM2Test_LowCPU CLOUD_GREY_PROFILE_H5_LOW_CPU)
    add_cgv_m2_profile_test(CloudGreyVerbM2Test_Balanced CLOUD_GREY_PROFILE_H5_BALANCED)
    add_cgv_m2_profile_test(CloudGreyVerbM2Test_H7 CLOUD_GREY_PROFILE_H7_HIGH_QUALITY)
    add_cgv_m2_profile_test(CloudGreyVerbM2Test_Desktop CLOUD_GREY_PROFILE_DESKTOP_STUDIO)

    add_executable(CloudGreyVerbM2Bench
        cloud_grey_verb.cpp
        cloud_grey_verb_m2_bench.cpp
        offline_metrics.cpp
    )
    target_compile_features(CloudGreyVerbM2Bench PRIVATE cxx_std_17)
    target_compile_definitions(CloudGreyVerbM2Bench PRIVATE CLOUD_GREY_PROFILE_DESKTOP_STUDIO=1)

    add_executable(CloudGreyVerbOfflineMetricsTest offline_metrics.cpp offline_metrics_test.cpp)
    target_compile_features(CloudGreyVerbOfflineMetricsTest PRIVATE cxx_std_17)
    add_test(NAME CloudGreyVerbOfflineMetricsTest COMMAND CloudGreyVerbOfflineMetricsTest)

    add_executable(onset_audit cloud_grey_verb.cpp onset_audit.cpp)
    target_compile_features(onset_audit PRIVATE cxx_std_17)
    target_compile_definitions(onset_audit PRIVATE CLOUD_GREY_PROFILE_DESKTOP_STUDIO=1)

    add_executable(long_tail_test cloud_grey_verb.cpp long_tail_test.cpp)
    target_compile_features(long_tail_test PRIVATE cxx_std_17)
    target_compile_definitions(long_tail_test PRIVATE CLOUD_GREY_PROFILE_DESKTOP_STUDIO=1)

    # Stochastic FDN modulation milestone validation harness (diagnostic, not CTest).
    add_executable(stochastic_modulation_test cloud_grey_verb.cpp stochastic_modulation_test.cpp)
    target_compile_features(stochastic_modulation_test PRIVATE cxx_std_17)
    target_compile_definitions(stochastic_modulation_test PRIVATE CLOUD_GREY_PROFILE_DESKTOP_STUDIO=1)
endif()
