#requires -Version 7.0

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot

. (Join-Path $PSScriptRoot 'idf_env.ps1')

Push-Location $RepoRoot
try {
    & idf.py -B build_d size
    if ($LASTEXITCODE -ne 0) {
        throw "size falhou com codigo $LASTEXITCODE."
    }

    & idf.py -B build_d size-components
    if ($LASTEXITCODE -ne 0) {
        throw "size-components falhou com codigo $LASTEXITCODE."
    }
}
finally {
    Pop-Location
}