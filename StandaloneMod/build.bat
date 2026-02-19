@echo off
echo === Building Starsand Island Standalone Mod ===

:: Setup Visual Studio environment
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1

:: Compile
echo Compiling mod_main.c...
cl /nologo /O2 /W3 /LD /MD ^
    /DWIN32 /D_WINDOWS /DNDEBUG /D_CRT_SECURE_NO_WARNINGS ^
    mod_main.c ^
    /Fe:version.dll ^
    /link /DEF:version.def /DLL ^
    user32.lib kernel32.lib

if %ERRORLEVEL% NEQ 0 (
    echo BUILD FAILED!
    pause
    exit /b 1
)

echo.
echo === BUILD SUCCESS ===
echo Output: version.dll
echo.
echo To install: copy version.dll to the game folder
echo   copy version.dll "C:\Program Files (x86)\Steam\steamapps\common\StarsandIsland\"
echo.

:: Auto-copy to game folder
copy /Y version.dll "C:\Program Files (x86)\Steam\steamapps\common\StarsandIsland\version.dll" >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    echo [OK] Installed to game folder!
) else (
    echo [WARN] Could not copy to game folder (game may be running)
)
