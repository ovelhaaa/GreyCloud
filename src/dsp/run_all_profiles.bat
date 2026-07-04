@echo off
echo Testing H5_LOW_CPU...
g++ -O3 -std=c++17 -DCLOUD_GREY_PROFILE_H5_LOW_CPU=1 cloud_grey_verb_fuzz_test.cpp cloud_grey_verb.cpp -I. -o fuzz_test_low
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%
fuzz_test_low.exe
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%

echo Testing H5_BALANCED...
g++ -O3 -std=c++17 -DCLOUD_GREY_PROFILE_H5_BALANCED=1 cloud_grey_verb_fuzz_test.cpp cloud_grey_verb.cpp -I. -o fuzz_test_bal
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%
fuzz_test_bal.exe
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%

echo Testing H7_HIGH_QUALITY...
g++ -O3 -std=c++17 -DCLOUD_GREY_PROFILE_H7_HIGH_QUALITY=1 cloud_grey_verb_fuzz_test.cpp cloud_grey_verb.cpp -I. -o fuzz_test_hq
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%
fuzz_test_hq.exe
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%

echo Testing DESKTOP_STUDIO...
g++ -O3 -std=c++17 -DCLOUD_GREY_PROFILE_DESKTOP_STUDIO=1 cloud_grey_verb_fuzz_test.cpp cloud_grey_verb.cpp -I. -o fuzz_test_desk
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%
fuzz_test_desk.exe
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%

echo All profiles passed!
