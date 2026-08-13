#requires -Version 7.0

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ports = @(Get-CimInstance Win32_SerialPort | Select-Object DeviceID, Name)

if ($ports.Count -eq 0) {
    Write-Host 'Nenhuma porta serial encontrada.'
    exit 0
}

$ports | Format-Table -AutoSize