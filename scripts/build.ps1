#requires -Version 7.0

[CmdletBinding()]
param(
    [switch]$Reconfigure
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $RepoRoot 'build_d'
$SdkConfig = Join-Path $RepoRoot 'sdkconfig'

. (Join-Path $PSScriptRoot 'idf_env.ps1')

Push-Location $RepoRoot
try {
    if ($Reconfigure) {
        Remove-Item -LiteralPath $BuildDir -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $SdkConfig -Force -ErrorAction SilentlyContinue

        & idf.py -B build_d set-target esp32s3
        if ($LASTEXITCODE -ne 0) {
            throw "set-target falhou com codigo $LASTEXITCODE."
        }
    }
    elseif (-not (Test-Path -LiteralPath $BuildDir) -or -not (Test-Path -LiteralPath $SdkConfig)) {
        & idf.py -B build_d set-target esp32s3
        if ($LASTEXITCODE -ne 0) {
            throw "set-target falhou com codigo $LASTEXITCODE."
        }
    }

    & idf.py -B build_d build
    if ($LASTEXITCODE -ne 0) {
        throw "Build falhou com codigo $LASTEXITCODE."
    }

    Write-Host 'Build concluido.'
}
finally {
    Pop-Location
}