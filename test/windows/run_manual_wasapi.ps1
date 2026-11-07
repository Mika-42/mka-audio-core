# Explicitly build/run one audible test without editing DISABLED_ in source.
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidateSet('sine', 'record')]
    [string]$Test,
    [Parameter(Position = 1)]
    [string]$BuildDirectory = 'cmake-build-debug',
    [string]$Configuration = 'Debug'
)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath (Join-Path $BuildDirectory 'CMakeCache.txt'))) {
    [Console]::Error.WriteLine("No configured CMake build in $BuildDirectory; configure with MKA_BUILD_WASAPI=ON first.")
    exit 2
}
& cmake --build $BuildDirectory --config $Configuration --target mka-audio-wasapi-tests
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$executable = Join-Path $BuildDirectory 'mka-audio-wasapi-tests.exe'
if (-not (Test-Path -LiteralPath $executable)) {
    $executable = Join-Path (Join-Path $BuildDirectory $Configuration) 'mka-audio-wasapi-tests.exe'
}
if (-not (Test-Path -LiteralPath $executable)) {
    [Console]::Error.WriteLine("Test executable not found in $BuildDirectory ($Configuration).")
    exit 2
}
$testName = if ($Test -eq 'sine') { 'TestPlaySineWave' } else { 'TestRecordAndPlayback' }
& $executable "--gtest_filter=WasapiManualTest.DISABLED_$testName" --gtest_also_run_disabled_tests
exit $LASTEXITCODE
