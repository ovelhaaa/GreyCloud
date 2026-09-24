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

    add_executable(CloudGreyVerbM2RegressionTest
        cloud_grey_verb.cpp
        cloud_grey_verb_m2_regression_test.cpp
    )
    target_compile_features(CloudGreyVerbM2RegressionTest PRIVATE cxx_std_17)
    target_compile_definitions(CloudGreyVerbM2RegressionTest PRIVATE CLOUD_GREY_PROFILE_DESKTOP_STUDIO=1)
    add_test(NAME CloudGreyVerbM2RegressionTest COMMAND CloudGreyVerbM2RegressionTest)
    set_tests_properties(CloudGreyVerbM2RegressionTest PROPERTIES TIMEOUT 120)

    add_executable(CloudGreyVerbM2Bench
        cloud_grey_verb.cpp
        cloud_grey_verb_m2_bench.cpp
    )
    target_compile_features(CloudGreyVerbM2Bench PRIVATE cxx_std_17)
    target_compile_definitions(CloudGreyVerbM2Bench PRIVATE CLOUD_GREY_PROFILE_DESKTOP_STUDIO=1)
endif()
