param(
    [ValidateSet("debug", "release", "both")][string]$Mode = "both",
    [string]$Target = "AxEng",
    [switch]$NoRun,
    [switch]$Test
)

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

foreach ($dir in "C:\Program Files\xmake", "C:\Program Files\LLVM\bin") {
    if ((Test-Path $dir) -and ($env:Path -notlike "*$dir*")) { $env:Path += ";$dir" }
}
if (-not $env:VCPKG_ROOT -and (Test-Path "e:\vcpkg")) { $env:VCPKG_ROOT = "e:\vcpkg" }
if (-not (Get-Command xmake -ErrorAction SilentlyContinue)) { throw "xmake not found" }

$modes = if ($Mode -eq "both") { @("debug", "release") } else { @($Mode) }
foreach ($m in $modes) {
    Write-Host "=== $m ===" -ForegroundColor Cyan
    xmake f -y -p windows -a x64 -m $m --toolchain=clang-cl
    if ($LASTEXITCODE) { throw "configure failed ($m)" }
    xmake -y
    if ($LASTEXITCODE) { throw "build failed ($m)" }
    if ($Test) {
        xmake run Tests
        if ($LASTEXITCODE) { throw "tests failed ($m)" }
    }
    if (-not $NoRun) {
        xmake run $Target -cxrv --in "$PSScriptRoot\editor" --out "E:\AxEdit" --allow-threads --allow-io --allow-os --project "$PSScriptRoot\tower_mancer" --compile_to "E:\TowerMancer"
        if ($LASTEXITCODE) { Write-Warning "$Target exited with code $LASTEXITCODE ($m)" }
    }
}
