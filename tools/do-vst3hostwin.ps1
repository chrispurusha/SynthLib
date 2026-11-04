# do-vst3hostwin.ps1 - builds tools\vst3hostwin.exe, the Windows test host for any SynthLib VST3
# (G2Alike's, the AllAlike plug-ins'). One self-contained exe; see Docs/code-notes/vst3hostwin.cpp.md.
#
#   powershell -ExecutionPolicy Bypass -File tools\do-vst3hostwin.ps1 [-Arch arm64|x64]
#
# Needs llvm-mingw (LLVM_MINGW, default the newest ~\Developer\llvm-mingw*), the VST3 SDK (VST3_SDK, default
# ~\Documents\vst3sdk) and miniaudio.h, taken from G2Alike's platform\windows beside this checkout (G2ALIKE_DIR).

param([string]$Arch = $(if ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64') { 'arm64' } else { 'x64' }))
$ErrorActionPreference = 'Stop'

$here = $PSScriptRoot
$llvm = $env:LLVM_MINGW
if (-not $llvm) {
    $llvm = Get-ChildItem "$env:USERPROFILE\Developer" -Directory -Filter 'llvm-mingw*' -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending | Select-Object -First 1 -ExpandProperty FullName
}
$triple = if ($Arch -eq 'arm64') { 'aarch64-w64-mingw32' } else { 'x86_64-w64-mingw32' }
$cxx = "$llvm\bin\$triple-clang++.exe"
$sdk = if ($env:VST3_SDK) { $env:VST3_SDK } else { "$env:USERPROFILE\Documents\vst3sdk" }
$g2 = if ($env:G2ALIKE_DIR) { $env:G2ALIKE_DIR } else { Join-Path (Split-Path -Parent (Split-Path -Parent $here)) 'G2Alike' }
$miniaudio = "$g2\platform\windows"

if (-not (Test-Path $cxx)) { throw "no llvm-mingw at '$llvm' - set LLVM_MINGW" }
if (-not (Test-Path "$sdk\pluginterfaces")) { throw "VST3 SDK not found at $sdk - set VST3_SDK" }
if (-not (Test-Path "$miniaudio\miniaudio.h")) { throw "miniaudio.h not found in $miniaudio - set G2ALIKE_DIR" }

$out = if ($Arch -eq 'arm64') { "$here\vst3hostwin.exe" } else { "$here\vst3hostwin-x64.exe" }
$sdkSources = @('pluginterfaces\base\funknown.cpp', 'pluginterfaces\base\coreiids.cpp',
                'public.sdk\source\vst\vstinitiids.cpp', 'public.sdk\source\common\commoniids.cpp') |
              ForEach-Object { "$sdk\$_" }

Write-Host "==> vst3hostwin for $triple"
& $cxx -std=gnu++17 -O2 -static -Wno-pragma-pack -isystem $sdk -isystem $miniaudio "$here\vst3hostwin.cpp" @sdkSources `
    -o $out -lwinmm -lole32 -luser32 -lgdi32 -lcomdlg32 -lshell32 -lpthread
if ($LASTEXITCODE) { throw 'build failed' }
Write-Host ("built: $out ({0:N1} MB)" -f ((Get-Item $out).Length / 1MB))
