<#
    package_engine.ps1 — build the `engine` target and produce a trimmed
    release zip containing `include/` and the built library. Demos are excluded.

    Usage (PowerShell):

        ./package_engine.ps1 -OutDir dist -Config Release

#>
param(
    [string]$RepoRoot = "D:\C++_Projects\3DGEngine",
    [string]$BuildDir = "",
    [string]$Config = "Release",
    [string]$OutDir = "",
    [switch]$Reconfigure
)

$ErrorActionPreference = "Stop"
function Say($m) { Write-Host "[package-engine] $m" -ForegroundColor Cyan }

if (-not $BuildDir) { $BuildDir = Join-Path $RepoRoot "build" }
if (-not $OutDir)   { $OutDir   = Join-Path $RepoRoot "dist" }

# 1) Configure with demos disabled by default for a minimal release. ----------------
$cacheFile = Join-Path $BuildDir "CMakeCache.txt"
if ($Reconfigure -or -not (Test-Path $cacheFile)) {
    Say "Configuring CMake (BUILD_DEMOS=OFF) ..."
    cmake -S $RepoRoot -B $BuildDir -DBUILD_DEMOS=OFF -DGAMEENGINE_STATIC_RUNTIME=ON -DCMAKE_BUILD_TYPE=$Config
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
} else {
    Say "Using existing build dir (pass -Reconfigure to force a fresh configure)"
}

# 2) Build the engine target. ------------------------------------------------------
Say "Building engine ($Config) ..."
cmake --build $BuildDir --target engine --config $Config
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

# 3) Install the engine into a staging folder. -------------------------------------
$stage = Join-Path $OutDir "3DGEngine-Engine"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force -Path $stage | Out-Null

Say "Staging engine artifacts (manual copy) ..."
# copy the built static library and public headers into the staging folder
$libSrc = Join-Path $BuildDir "engine/$Config/engine.lib"
if (-not (Test-Path $libSrc)) {
    # try common alternate path
    $libSrc = Join-Path $BuildDir "engine/Release/engine.lib"
}
if (-not (Test-Path $libSrc)) { throw "Could not find built engine lib: $libSrc" }

$libDir = Join-Path $stage "lib"
New-Item -ItemType Directory -Force -Path $libDir | Out-Null
Copy-Item $libSrc (Join-Path $libDir "engine.lib") -Force

# copy public headers
$includeSrc = Join-Path $RepoRoot "engine/include"
if (-not (Test-Path $includeSrc)) { throw "Could not find engine include dir: $includeSrc" }
Copy-Item $includeSrc $stage -Recurse -Force

# optionally remove any demo or content dirs if present
$possibleDemoDirs = @("Content","demos","samples")
foreach ($d in $possibleDemoDirs) {
    $p = Join-Path $stage $d
    if (Test-Path $p) { Remove-Item $p -Recurse -Force -ErrorAction SilentlyContinue }
}

# 5) Zip the staged engine. --------------------------------------------------------
$zip = Join-Path $OutDir "3DGEngine-Engine-$Config.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Say "Zipping -> $zip"
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip -Force

Say "DONE."
Say "Staged folder: $stage"
Say "Release zip: $zip"
#
# Test it: extract the zip and verify `include/` and `lib/` (or `bin/`) exist.
#
