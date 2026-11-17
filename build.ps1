# One-command build on Windows (Docker Desktop): .\build.ps1 C:\path\to\dynwar.zip   (output in .\out)
param([Parameter(Mandatory=$true)][string]$Zip)
$ErrorActionPreference = "Stop"
$zipPath = (Resolve-Path $Zip).Path
Set-Location $PSScriptRoot
$ErrorActionPreference = "Continue"   # (docker writes to stderr when an image is missing: not an error here)
$tc = "ghcr.io/ihamana/dynastywars32x-toolchain:gcc13.2-1"   # prebuilt cross compilers (docker/toolchain.Dockerfile)
docker image inspect $tc *> $null
if ($LASTEXITCODE) { docker pull --platform linux/amd64 $tc }
if ($LASTEXITCODE) {
    Write-Host "toolchain image not available: building the cross compilers (20-40 minutes, once)"
    docker build --platform linux/amd64 -f docker/toolchain.Dockerfile -t $tc .
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
}
$ErrorActionPreference = "Stop"
docker build --platform linux/amd64 --build-arg TOOLCHAIN=$tc -t dynastywars32x .
if ($LASTEXITCODE) { exit $LASTEXITCODE }
docker run --rm --platform linux/amd64 -v "${PWD}:/dw" -v "${zipPath}:/rom/dynwar.zip:ro" dynastywars32x
exit $LASTEXITCODE
