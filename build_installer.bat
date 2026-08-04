@echo off
setlocal
if not exist build\xelauncher.exe (
    echo Run build.bat first to produce build\xelauncher.exe and build\defaults.toml
    exit /b 1
)
if not exist dist mkdir dist

wix extension add WixToolset.UI.wixext/5.0.2 >nul 2>&1

set "WIXUI="
for /r "%~dp0..\.wix" %%f in (WixToolset.UI.wixext.dll) do set "WIXUI=%%f"
if not defined WIXUI (
    for /r "%USERPROFILE%\.wix" %%f in (WixToolset.UI.wixext.dll) do set "WIXUI=%%f"
)
if not defined WIXUI (
    echo Could not find WixToolset.UI.wixext.dll. Install it with: wix extension add WixToolset.UI.wixext/5.0.2
    exit /b 1
)

wix build installer\xenia-config-editor.wxs -b build -ext "%WIXUI%" -o dist\XeLauncher.msi
if errorlevel 1 exit /b %errorlevel%
echo Installer built: dist\XeLauncher.msi
