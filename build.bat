@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if exist build rmdir /s /q build
mkdir build
rc /nologo app.rc
cl /nologo /std:c++17 /EHsc /O2 /W3 /utf-8 /DUNICODE /D_UNICODE /Isrc /Ithird_party\imgui /Ithird_party\imgui\backends /Ithird_party\imgui\misc\cpp ^
  src\main.cpp src\editor.cpp src\config.cpp ^
  src\json.cpp src\net.cpp src\library.cpp src\compat.cpp src\texture.cpp ^
  third_party\imgui\imgui.cpp third_party\imgui\imgui_draw.cpp ^
  third_party\imgui\imgui_tables.cpp third_party\imgui\imgui_widgets.cpp ^
  third_party\imgui\misc\cpp\imgui_stdlib.cpp ^
  third_party\imgui\backends\imgui_impl_win32.cpp third_party\imgui\backends\imgui_impl_dx11.cpp ^
  /Fe:build\xelauncher.exe ^
  /link app.res user32.lib gdi32.lib shell32.lib ole32.lib advapi32.lib d3d11.lib dxgi.lib d3dcompiler.lib winhttp.lib windowscodecs.lib /SUBSYSTEM:WINDOWS
if %errorlevel% neq 0 exit /b %errorlevel%
copy /y defaults.toml build\defaults.toml >nul
echo Build OK
