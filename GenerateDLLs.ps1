# Starsand Island - IL2CPP DLL Generation Script

param(
    [string]$GamePath = $PSScriptRoot + "\..",
    [switch]$Cpp2IL,
    [switch]$Il2CppDumper,
    [switch]$All
)

$ErrorActionPreference = "Stop"
$GamePath = $GamePath.TrimEnd("\")
$ToolsPath = Join-Path $GamePath "StarsandIslandMod\Tools"
$OutputPath = Join-Path (Join-Path $GamePath "StarsandIslandMod") "Generated"

# Game files
$GameExe = Join-Path $GamePath "StarsandIsland.exe"
$GameAssembly = Join-Path $GamePath "GameAssembly.dll"
$MetadataPath = Join-Path $GamePath "StarsandIsland_Data\il2cpp_data\Metadata\global-metadata.dat"

if (-not (Test-Path $GameExe)) {
    Write-Error "Game not found at: $GameExe"
    exit 1
}
if (-not (Test-Path $MetadataPath)) {
    Write-Error "Metadata not found at: $MetadataPath"
    exit 1
}

function Get-Cpp2IL {
    $url = "https://github.com/SamboyCoding/Cpp2IL/releases/download/2022.1.0-pre-release.20/Cpp2IL-Windows-2022.1.0-pre-release.20.zip"
    $zipPath = Join-Path $ToolsPath "Cpp2IL.zip"
    $extractPath = Join-Path $ToolsPath "Cpp2IL"
    
    if (-not (Test-Path $extractPath)) {
        Write-Host "Downloading Cpp2IL..."
        New-Item -ItemType Directory -Force -Path $ToolsPath | Out-Null
        Invoke-WebRequest -Uri $url -OutFile $zipPath -UseBasicParsing
        Expand-Archive -Path $zipPath -DestinationPath $extractPath -Force
        Remove-Item $zipPath -Force
    }
    return $extractPath
}

function Get-Il2CppDumper {
    $url = "https://github.com/Perfare/Il2CppDumper/releases/download/v6.7.46/Il2CppDumper-net6.zip"
    $zipPath = Join-Path $ToolsPath "Il2CppDumper.zip"
    $extractPath = Join-Path $ToolsPath "Il2CppDumper"
    
    if (-not (Test-Path $extractPath)) {
        Write-Host "Downloading Il2CppDumper..."
        New-Item -ItemType Directory -Force -Path $ToolsPath | Out-Null
        Invoke-WebRequest -Uri $url -OutFile $zipPath -UseBasicParsing
        Expand-Archive -Path $zipPath -DestinationPath $extractPath -Force
        Remove-Item $zipPath -Force
    }
    return $extractPath
}

function Run-Cpp2IL {
    Write-Host "`n=== Running Cpp2IL ===" -ForegroundColor Cyan
    $cpp2ilPath = Get-Cpp2IL
    $cpp2ilExe = Get-ChildItem -Path $cpp2ilPath -Filter "Cpp2IL.exe" -Recurse | Select-Object -First 1
    
    if (-not $cpp2ilExe) {
        $cpp2ilExe = Get-ChildItem -Path $cpp2ilPath -Filter "*.exe" -Recurse | Select-Object -First 1
    }
    
    $cpp2ilOut = Join-Path $OutputPath "cpp2il_out"
    New-Item -ItemType Directory -Force -Path $cpp2ilOut | Out-Null
    
    $args = @(
        "--game-path", $GamePath,
        "--output-path", $cpp2ilOut,
        "--just-give-me-dlls-asap-dammit"
    )
    
    Write-Host "Executing: $($cpp2ilExe.FullName) $($args -join ' ')"
    & $cpp2ilExe.FullName @args
    
    if ($LASTEXITCODE -eq 0) {
        Write-Host "Cpp2IL output: $cpp2ilOut" -ForegroundColor Green
        Write-Host "Open these DLLs in DnSpy to explore game code!"
    } else {
        Write-Host "Cpp2IL may have failed. Try adding: --force-unity-version 6000f1" -ForegroundColor Yellow
    }
}

function Run-Il2CppDumper {
    Write-Host "`n=== Running Il2CppDumper ===" -ForegroundColor Cyan
    $dumperPath = Get-Il2CppDumper
    $dumperExe = Get-ChildItem -Path $dumperPath -Filter "Il2CppDumper.exe" -Recurse | Select-Object -First 1
    
    if (-not $dumperExe) {
        $dumperExe = Get-ChildItem -Path $dumperPath -Filter "*.exe" -Recurse | Select-Object -First 1
    }
    
    $dumperOut = Join-Path $OutputPath "Il2CppDumper"
    New-Item -ItemType Directory -Force -Path $dumperOut | Out-Null
    
    $targetBinary = $GameAssembly
    if (-not (Test-Path $targetBinary)) {
        $targetBinary = $GameExe
    }
    
    Write-Host "Executing Il2CppDumper..."
    & $dumperExe.FullName $targetBinary $MetadataPath $dumperOut
    
    if ($LASTEXITCODE -eq 0) {
        Write-Host "Il2CppDumper output: $dumperOut" -ForegroundColor Green
        Write-Host "Contains: dummyDll/, il2cpp.h, script.json for Ghidra"
    }
}

# Main
if (-not ($Cpp2IL -or $Il2CppDumper -or $All)) {
    Write-Host "Starsand Island - DLL Generation Tool" -ForegroundColor Cyan
    Write-Host "Usage: .\GenerateDLLs.ps1 [-Cpp2IL] [-Il2CppDumper] [-All]"
    Write-Host ""
    Write-Host "  -Cpp2IL       Generate decompiled DLLs (for DnSpy, C# reference)"
    Write-Host "  -Il2CppDumper Generate dummy DLLs + il2cpp.h (for Ghidra)"
    Write-Host "  -All          Run both tools"
    Write-Host ""
    Write-Host "If MelonLoader's Il2CppInterop generator failed, run these tools"
    Write-Host "to get DLLs for exploring modding the game."
    exit 0
}

New-Item -ItemType Directory -Force -Path $OutputPath | Out-Null

if ($Cpp2IL -or $All) { Run-Cpp2IL }
if ($Il2CppDumper -or $All) { Run-Il2CppDumper }

Write-Host "`nDone! Check the Generated folder." -ForegroundColor Green
