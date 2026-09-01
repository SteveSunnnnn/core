$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$Build = Join-Path $Root "build-windows-validation"
$ReportDir = Join-Path $Root "validation-output"
New-Item -ItemType Directory -Force -Path $ReportDir | Out-Null
$Log = Join-Path $ReportDir "core_validation_report.txt"
"Core 1.0 Engine Validation" | Set-Content $Log
function Need($cmd) { if (-not (Get-Command $cmd -ErrorAction SilentlyContinue)) { throw "Required tool missing: $cmd" }; "$cmd=$((Get-Command $cmd).Source)" | Add-Content $Log }
Need cmake; Need ninja; Need glslc; Need spirv-val
if (-not $env:VULKAN_SDK) { throw "VULKAN_SDK is not set. Reinstall/open a Vulkan SDK shell." }
"VULKAN_SDK=$env:VULKAN_SDK" | Add-Content $Log
$Spv = Join-Path $ReportDir "spv"; New-Item -ItemType Directory -Force -Path $Spv | Out-Null
Get-ChildItem (Join-Path $Root "shaders") -File | Where-Object { $_.Extension -in ".vert", ".frag", ".comp" } | ForEach-Object { $out=Join-Path $Spv ($_.Name+".spv"); & glslc $_.FullName -O -o $out; if($LASTEXITCODE){throw "glslc failed: $($_.Name)"}; & spirv-val $out; if($LASTEXITCODE){throw "spirv-val failed: $($_.Name)"} }
"shader_validation=PASS" | Add-Content $Log
cmake -S $Root -B $Build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCORE_WARNINGS_AS_ERRORS=ON
if($LASTEXITCODE){throw "CMake configure failed"}
cmake --build $Build --target core_runtime core_cli core_world_compiler core_world_inspect core_asset_cooker core_asset_inspect core_architecture_cooker core_material_cooker --parallel
if($LASTEXITCODE){throw "Build failed"}
"FINAL=PASS" | Add-Content $Log
Write-Host "CORE VALIDATION PASS"
Write-Host "Report: $Log"
