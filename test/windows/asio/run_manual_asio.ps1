param(
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidateSet('sine', 'record')]
    [string]$Test,
    [Parameter(Position = 1)]
    [string]$BuildDirectory = 'build/windows-asio',
    [string]$Configuration = 'Debug'
)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath (Join-Path $BuildDirectory 'CMakeCache.txt'))) {
    [Console]::Error.WriteLine("Configure windows-asio with an external MKA_ASIO_SDK_DIR before running this script.")
    exit 2
}
& cmake --build $BuildDirectory --config $Configuration --target mka-audio-asio-hardware-tests
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$executable = Join-Path $BuildDirectory 'mka-audio-asio-hardware-tests.exe'
if (-not (Test-Path -LiteralPath $executable)) {
    $executable = Join-Path (Join-Path $BuildDirectory $Configuration) 'mka-audio-asio-hardware-tests.exe'
}
if (-not (Test-Path -LiteralPath $executable)) {
    [Console]::Error.WriteLine("Test executable not found in $BuildDirectory ($Configuration).")
    exit 2
}
$testName = if ($Test -eq 'sine') { 'TestPlaySineWave' } else { 'TestRecordAndPlayback' }
& $executable "--gtest_filter=AsioManualTest.DISABLED_$testName" --gtest_also_run_disabled_tests
exit $LASTEXITCODE
