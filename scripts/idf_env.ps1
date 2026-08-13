#requires -Version 7.0

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ExpectedVersion = 'ESP-IDF v5.5.4'
$env:IDF_TOOLS_PATH = 'D:\Espressif'
$env:IDF_PATH = 'D:\Espressif\frameworks\esp-idf-v5.5.4'
$env:IDF_PYTHON_ENV_PATH = 'D:\Espressif\python_env\idf5.5_py3.11_env'

$ExportScript = Join-Path $env:IDF_PATH 'export.ps1'

if (-not (Test-Path -LiteralPath $ExportScript)) {
    throw "ESP-IDF 5.5.4 nao encontrado: $ExportScript"
}

. $ExportScript

$versionOutput = (& idf.py --version 2>&1 | Out-String).Trim()

if ($LASTEXITCODE -ne 0) {
    throw 'Falha ao executar idf.py --version.'
}

if ($versionOutput -notmatch [regex]::Escape($ExpectedVersion)) {
    throw "Versao inesperada. Esperado '$ExpectedVersion'. Atual: '$versionOutput'."
}

Write-Host "Ambiente carregado: $versionOutput"