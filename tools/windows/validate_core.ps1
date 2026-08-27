$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$Build = Join-Path $Root "build-windows-validation"
$ReportDir = Join-Path $Root "validation-output"
New-Item -ItemType Directory -Force -Path $ReportDir | Out-Null
$Log = Join-Path $ReportDir "core_validation_report.txt"
"Core 1.0 Windows / Vulkan Validation" | Set-Content $Log
function Need($cmd) { if (-not (Get-Command $cmd -ErrorAction SilentlyContinue)) { throw "Required tool missing: $cmd" }; "$cmd=$((Get-Command $cmd).Source)" | Add-Content $Log }
Need cmake; Need ninja; Need glslc; Need spirv-val
if (-not $env:VULKAN_SDK) { throw "VULKAN_SDK is not set. Reinstall/open a Vulkan SDK shell." }
"VULKAN_SDK=$env:VULKAN_SDK" | Add-Content $Log
$Spv = Join-Path $ReportDir "spv"; New-Item -ItemType Directory -Force -Path $Spv | Out-Null
Get-ChildItem (Join-Path $Root "shaders") -File | Where-Object { $_.Extension -in ".vert", ".frag", ".comp" } | ForEach-Object { $out=Join-Path $Spv ($_.Name+".spv"); & glslc $_.FullName -O -o $out; if($LASTEXITCODE){throw "glslc failed: $($_.Name)"}; & spirv-val $out; if($LASTEXITCODE){throw "spirv-val failed: $($_.Name)"} }
"shader_validation=PASS" | Add-Content $Log
cmake -S $Root -B $Build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCORE_BUILD_TESTS=ON -DCORE_BUILD_STRESS_TESTS=ON -DCORE_BUILD_BENCHMARKS=ON -DCORE_BUILD_DESKTOP=ON -DCORE_WARNINGS_AS_ERRORS=ON
if($LASTEXITCODE){throw "CMake configure failed"}
cmake --build $Build --parallel
if($LASTEXITCODE){throw "Build failed"}
ctest --test-dir $Build -C Release --output-on-failure
if($LASTEXITCODE){throw "CTest failed"}
& (Join-Path $Build "core_job_stress.exe")
if($LASTEXITCODE){throw "JobSystem stress failed"}
$env:CORE_VULKAN_VALIDATION="1"; $env:CORE_VALIDATION_FRAMES="300"; $env:CORE_GPU_REPORT=(Join-Path $ReportDir "core_gpu_report.txt")
& (Join-Path $Build "core_desktop.exe")
if($LASTEXITCODE){throw "Core Vulkan desktop validation failed with exit $LASTEXITCODE"}
if(-not (Test-Path $env:CORE_GPU_REPORT)){throw "GPU report missing"}
$Gpu = Get-Content $env:CORE_GPU_REPORT
$Gpu | Add-Content $Log
$errors = ($Gpu | Where-Object { $_ -match '^validation_errors=' }) -replace 'validation_errors=',''
$frames = ($Gpu | Where-Object { $_ -match '^frames_presented=' }) -replace 'frames_presented=',''
if([int]$errors -ne 0){throw "Vulkan validation errors: $errors"}
if([int64]$frames -lt 300){throw "Renderer did not present 300 frames"}
"FINAL=PASS" | Add-Content $Log
Write-Host "CORE VALIDATION PASS"
Write-Host "Report: $Log"
