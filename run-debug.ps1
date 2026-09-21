param(
    [int]$Tail = 200
)

$ErrorActionPreference = "Stop"

$runScript = Join-Path $PSScriptRoot "run.ps1"
$serialLog = Join-Path $PSScriptRoot "serial.log"
$logsScript = Join-Path $PSScriptRoot "logs.ps1"

if (-not (Test-Path -LiteralPath $runScript)) {
    throw "run.ps1 was not found at $runScript"
}

if (-not (Test-Path -LiteralPath $logsScript)) {
    throw "logs.ps1 was not found at $logsScript"
}

# Remove the previous session so the watcher never attaches to a stale file.
if (Test-Path -LiteralPath $serialLog) {
    Remove-Item -LiteralPath $serialLog -Force
}

Write-Host "=== Starting ALOS debug session ==="
Write-Host "Build/QEMU output will open in a second PowerShell window."
Write-Host "This window will show kernel serial logs as soon as QEMU creates serial.log."
Write-Host ""

$quotedRunScript = '"' + $runScript + '"'
$arguments = "-NoProfile -ExecutionPolicy Bypass -File $quotedRunScript"
$runner = Start-Process -FilePath "powershell.exe" -ArgumentList $arguments -PassThru

while (-not (Test-Path -LiteralPath $serialLog)) {
    if ($runner.HasExited) {
        throw "run.ps1 exited before serial.log was created (exit code $($runner.ExitCode)). Check the build/QEMU window."
    }
    Start-Sleep -Milliseconds 250
}

& $logsScript -LogPath $serialLog -Tail $Tail
