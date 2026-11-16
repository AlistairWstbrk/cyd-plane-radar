@echo off
rem Build the host simulator with MSVC (Visual Studio 2022 with C++ workload).
rem Run "pio run -e cyd" once first so .pio\libdeps (LovyanGFX, ArduinoJson) exists.
setlocal
set ROOT=%~dp0..
set LIBD=%ROOT%\.pio\libdeps\cyd
set LGFX=%LIBD%\LovyanGFX\src
set OBJ=%~dp0obj
if not exist "%LGFX%" (
  echo Missing %LGFX% - run "pio run -e cyd" first.
  exit /b 1
)
set PATH=%PATH%;C:\Program Files (x86)\Microsoft Visual Studio\Installer
set VCVARS="C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not defined VSCMD_VER call %VCVARS% >nul

if not exist "%OBJ%" mkdir "%OBJ%"
set FLAGS=/nologo /std:c++17 /EHsc /O2 /MP /utf-8 /W0 /DARDUINO=10819 /D_CRT_SECURE_NO_WARNINGS /DNOMINMAX /DLGFX_USE_V1
set FLAGS=%FLAGS% /DARDUINOJSON_ENABLE_PROGMEM=0 /DARDUINOJSON_ENABLE_ARDUINO_STREAM=0 /DARDUINOJSON_ENABLE_ARDUINO_PRINT=0
set FLAGS=%FLAGS% /FI"%~dp0stubs/sim_prefix.h"
set INC=/I"%~dp0stubs" /I"%ROOT%\include" /I"%~dp0." /I"%LGFX%" /I"%LIBD%\ArduinoJson\src"

set SRC="%~dp0sim_main.cpp" "%~dp0sim_runtime.cpp" "%~dp0lgfx_extra.cpp"
set SRC=%SRC% "%ROOT%\src\ui\app.cpp" "%ROOT%\src\ui\canvas.cpp" "%ROOT%\src\ui\radar_view.cpp" "%ROOT%\src\ui\screens.cpp"
set SRC=%SRC% "%ROOT%\src\ui\status_screens.cpp" "%ROOT%\src\ui\theme.cpp" "%ROOT%\src\ui\touch.cpp" "%ROOT%\src\ui\traffic.cpp"
set SRC=%SRC% "%ROOT%\src\services\settings.cpp" "%ROOT%\src\services\airport_store.cpp" "%ROOT%\src\services\airport_lookup.cpp"
set SRC=%SRC% "%ROOT%\src\services\adsb_client.cpp" "%ROOT%\src\services\device_code.cpp" "%ROOT%\src\data\large_airports_data.cpp" "%ROOT%\src\hardware\display_font.cpp" "%ROOT%\src\services\airport_search.cpp" "%ROOT%\src\data\airport_index_data.cpp" "%ROOT%\src\ui\search_screen.cpp" "%ROOT%\src\ui\detail_screen.cpp"
set SRC=%SRC% "%LGFX%\lgfx\v1\LGFXBase.cpp" "%LGFX%\lgfx\v1\lgfx_fonts.cpp" "%LGFX%\lgfx\v1\lgfx_v1.cpp"

rem C sources: compiled without ARDUINO (their pgmspace.h would pull in the C++ Arduino stub).
set U=%LGFX%\lgfx\utility
if not exist "%OBJ%\c" mkdir "%OBJ%\c"
if exist "%OBJ%\c\lgfx_efont_tw.obj" goto cdone
pushd "%OBJ%\c"
cl /nologo /c /O2 /W0 /D_CRT_SECURE_NO_WARNINGS /FI"%~dp0stubs\sim_prefix.h" "%U%\lgfx_qrcode.c" "%U%\lgfx_miniz.c" "%U%\lgfx_pngle.c" "%U%\lgfx_tjpgd.c" "%U%\lgfx_qoi.c" "%LGFX%\lgfx\Fonts\IPA\lgfx_font_japan.c" "%LGFX%\lgfx\Fonts\efont\*.c"
set CERR=%ERRORLEVEL%
popd
if not "%CERR%"=="0" exit /b 1
:cdone
set COBJ="%OBJ%\c\*.obj"

cl %FLAGS% %INC% /Fo"%OBJ%/" /Fe"%~dp0plane_radar_sim.exe" %SRC% %COBJ% /link /nologo
exit /b %ERRORLEVEL%
