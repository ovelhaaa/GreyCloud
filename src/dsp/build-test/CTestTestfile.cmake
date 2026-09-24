# CMake generated Testfile for 
# Source directory: C:/progs/vst/GreyCloud/src/dsp
# Build directory: C:/progs/vst/GreyCloud/src/dsp/build-test
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[CloudGreyVerbMilestoneTest]=] "C:/progs/vst/GreyCloud/src/dsp/build-test/CloudGreyVerbMilestoneTest.exe")
set_tests_properties([=[CloudGreyVerbMilestoneTest]=] PROPERTIES  TIMEOUT "120" _BACKTRACE_TRIPLES "C:/progs/vst/GreyCloud/src/dsp/CMakeLists.txt;13;add_test;C:/progs/vst/GreyCloud/src/dsp/CMakeLists.txt;0;")
add_test([=[CloudGreyVerbM2Test_LowCPU]=] "C:/progs/vst/GreyCloud/src/dsp/build-test/CloudGreyVerbM2Test_LowCPU.exe")
set_tests_properties([=[CloudGreyVerbM2Test_LowCPU]=] PROPERTIES  TIMEOUT "120" _BACKTRACE_TRIPLES "C:/progs/vst/GreyCloud/src/dsp/CMakeLists.txt;20;add_test;C:/progs/vst/GreyCloud/src/dsp/CMakeLists.txt;23;add_cgv_m2_profile_test;C:/progs/vst/GreyCloud/src/dsp/CMakeLists.txt;0;")
add_test([=[CloudGreyVerbM2Test_Balanced]=] "C:/progs/vst/GreyCloud/src/dsp/build-test/CloudGreyVerbM2Test_Balanced.exe")
set_tests_properties([=[CloudGreyVerbM2Test_Balanced]=] PROPERTIES  TIMEOUT "120" _BACKTRACE_TRIPLES "C:/progs/vst/GreyCloud/src/dsp/CMakeLists.txt;20;add_test;C:/progs/vst/GreyCloud/src/dsp/CMakeLists.txt;24;add_cgv_m2_profile_test;C:/progs/vst/GreyCloud/src/dsp/CMakeLists.txt;0;")
add_test([=[CloudGreyVerbM2Test_H7]=] "C:/progs/vst/GreyCloud/src/dsp/build-test/CloudGreyVerbM2Test_H7.exe")
set_tests_properties([=[CloudGreyVerbM2Test_H7]=] PROPERTIES  TIMEOUT "120" _BACKTRACE_TRIPLES "C:/progs/vst/GreyCloud/src/dsp/CMakeLists.txt;20;add_test;C:/progs/vst/GreyCloud/src/dsp/CMakeLists.txt;25;add_cgv_m2_profile_test;C:/progs/vst/GreyCloud/src/dsp/CMakeLists.txt;0;")
add_test([=[CloudGreyVerbM2Test_Desktop]=] "C:/progs/vst/GreyCloud/src/dsp/build-test/CloudGreyVerbM2Test_Desktop.exe")
set_tests_properties([=[CloudGreyVerbM2Test_Desktop]=] PROPERTIES  TIMEOUT "120" _BACKTRACE_TRIPLES "C:/progs/vst/GreyCloud/src/dsp/CMakeLists.txt;20;add_test;C:/progs/vst/GreyCloud/src/dsp/CMakeLists.txt;26;add_cgv_m2_profile_test;C:/progs/vst/GreyCloud/src/dsp/CMakeLists.txt;0;")
