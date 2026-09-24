cmake_minimum_required(VERSION 3.20)
project(CloudGreyVerbDSP LANGUAGES CXX)

include(CTest)

if(BUILD_TESTING)
    add_executable(CloudGreyVerbMilestoneTest
        cloud_grey_verb.cpp
        cloud_grey_verb_milestone_test.cpp
    )
    target_compile_features(CloudGreyVerbMilestoneTest PRIVATE cxx_std_17)
    target_compile_definitions(CloudGreyVerbMilestoneTest PRIVATE CLOUD_GREY_PROFILE_DESKTOP_STUDIO=1)
    add_test(NAME CloudGreyVerbMilestoneTest COMMAND CloudGreyVerbMilestoneTest)
    set_tests_properties(CloudGreyVerbMilestoneTest PROPERTIES TIMEOUT 120)

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
    )
    target_compile_features(CloudGreyVerbM2Bench PRIVATE cxx_std_17)
    target_compile_definitions(CloudGreyVerbM2Bench PRIVATE CLOUD_GREY_PROFILE_DESKTOP_STUDIO=1)
endif()
