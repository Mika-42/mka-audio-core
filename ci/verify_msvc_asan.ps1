$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$commands = Get-Content 'build/windows-wasapi-asan/compile_commands.json' -Raw | ConvertFrom-Json
$native = @($commands | Where-Object { $_.file -match 'src[/\\](core|backend[/\\]windows)' })
if ($native.Count -eq 0) { throw 'No core/Windows compile commands found' }
foreach ($entry in $native) {
    if ($entry.command -notmatch '/fsanitize=address' -or $entry.command -match '/RTC1') {
        throw "Missing or incompatible ASan instrumentation: $($entry.file)"
    }
}
if (@($commands | Where-Object { $_.file -match '(rt_alloc|test_rt)\.cpp$' }).Count -ne 0) {
    throw 'Allocator interposition must not be linked with ASan'
}
$source = Join-Path $env:RUNNER_TEMP 'mka-asan-witness.cpp'
$program = Join-Path $env:RUNNER_TEMP 'mka-asan-witness.exe'
$log = Join-Path $env:RUNNER_TEMP 'mka-asan-witness.log'
@'
__declspec(noinline) int readOutside() {
    auto* values = new int[4]{};
    volatile int index = 4;
    const int value = values[index];
    delete[] values;
    return value;
}
int main() { return readOutside(); }
'@ | Set-Content $source -Encoding utf8
& cl /nologo /EHsc /std:c++latest /fsanitize=address /Zi "/Fe:$program" $source /link /INCREMENTAL:NO
if ($LASTEXITCODE -ne 0) { throw 'ASan witness compilation failed' }
& $program > $log 2>&1
$code = $LASTEXITCODE
$output = Get-Content $log -Raw
if ($code -eq 0 -or $output -notmatch 'AddressSanitizer.*heap-buffer-overflow') {
    throw "ASan did not detect the witness overflow (exit $code): $output"
}
Write-Host 'MSVC ASan instrumentation verified; witness heap-buffer-overflow detected.'
exit 0
