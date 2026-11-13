# One-command build on Windows (Docker Desktop): .\build.ps1 C:\path\to\dynwar.zip   (output in .\out)
param([Parameter(Mandatory=$true)][string]$Zip)
$ErrorActionPreference = "Stop"
$zipPath = (Resolve-Path $Zip).Path
Set-Location $PSScriptRoot
docker build --platform linux/amd64 -t dynastywars32x .
if ($LASTEXITCODE) { exit $LASTEXITCODE }
docker run --rm --platform linux/amd64 -v "${PWD}:/dw" -v "${zipPath}:/rom/dynwar.zip:ro" dynastywars32x
exit $LASTEXITCODE
