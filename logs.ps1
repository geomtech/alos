param(
    [string]$LogPath = (Join-Path $PSScriptRoot "serial.log"),
    [int]$Tail = 200,
    [switch]$ErrorsOnly
)

$ErrorActionPreference = "Stop"

Write-Host "=== ALOS serial logs ==="
Write-Host "File: $LogPath"
Write-Host "Ctrl+C to stop following logs."
Write-Host ""

while (-not (Test-Path -LiteralPath $LogPath)) {
    Start-Sleep -Milliseconds 250
}

if ($ErrorsOnly) {
    Get-Content -LiteralPath $LogPath -Tail $Tail -Wait |
        Where-Object { $_ -match '\[(ERROR|WARN)\]|PANIC|FAULT|EXCEPTION|TRIPLE|RESET' }
} else {
    Get-Content -LiteralPath $LogPath -Tail $Tail -Wait
}
