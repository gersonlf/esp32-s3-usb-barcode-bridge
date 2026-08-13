#requires -Version 7.0

[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidatePattern('^COM\d+$')]
    [string]$Port
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot

. (Join-Path $PSScriptRoot 'idf_env.ps1')

Push-Location $RepoRoot
try {
    & idf.py -B build_d -p $Port erase-flash
    if ($LASTEXITCODE -ne 0) {
        throw "erase flash terminou com codigo $LASTEXITCODE."
    }
}
finally {
    Pop-Location
}