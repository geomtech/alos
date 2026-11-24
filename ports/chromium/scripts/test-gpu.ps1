param(
    [switch]$SkipBuild,
    [string]$Cpu = "qemu64",
    [string]$Qemu = "C:\Program Files\qemu\qemu-system-x86_64.exe",
    [string]$OutputDirectory = (Join-Path ([IO.Path]::GetTempPath()) ("alos-gpu-" + [guid]::NewGuid())),
    [int]$QmpPort = 4488,
    [int]$TimeoutSeconds = 120
)
# Test du pilote virtio-gpu 2D : boot ALOS avec -device virtio-gpu-pci, attend
# l'auto-test noyau (fond bleu 0x000040C0 + rectangle rouge en [64,64,256,128]),
# puis capture le scanout par QMP `screendump` et verifie des pixels reels cote
# hote. Aucun disque utilisateur n'est touche (disque vide jetable).
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$output = (Resolve-Path $OutputDirectory).Path
$log = Join-Path $output "serial.log"
$ppm = Join-Path $output "gpu.ppm"
if (-not (Test-Path $Qemu)) { throw "QEMU introuvable : $Qemu" }
if (-not $SkipBuild) {
    docker run --rm -v "${repo}:/root/env" alos-runtime sh -c `
        'make -B -s alos.elf && make -B -s -C src/userland all && make -s fs_root && make -s iso -o disk.img -o verify-gui-image' `
        *> (Join-Path $output "build.log")
    if ($LASTEXITCODE -ne 0) { throw "Build echoue : $output\build.log" }
}
$disk = Join-Path $output "blank.disk"
$stream = [IO.File]::Create($disk); $stream.SetLength(64MB); $stream.Dispose()

$start = [Diagnostics.ProcessStartInfo]::new()
$start.FileName = $Qemu
$start.UseShellExecute = $false
$arguments = @(
    "-cdrom", (Join-Path $repo "alos.iso"),
    "-drive", "file=$disk,format=raw,if=ide",
    "-m", "1024M", "-cpu", $Cpu, "-smp", "1",
    "-netdev", "user,id=net0", "-device", "virtio-net-pci,netdev=net0",
    "-device", "virtio-gpu-pci,id=gpu0",
    "-display", "none", "-serial", "file:$log", "-monitor", "none",
    "-qmp", "tcp:127.0.0.1:$QmpPort,server=on,wait=off",
    "-no-reboot", "-no-shutdown"
)
if ($null -ne $start.ArgumentList) {
    foreach ($a in $arguments) { $start.ArgumentList.Add([string]$a) }
} else {
    $start.Arguments = (($arguments | ForEach-Object { '"' + ([string]$_).Replace('"', '\"') + '"' }) -join " ")
}
function Read-Serial {
    if (-not (Test-Path $log)) { return "" }
    $s = [IO.FileStream]::new($log, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
    $r = [IO.StreamReader]::new($s)
    try { return $r.ReadToEnd() } finally { $r.Dispose() }
}
function Invoke-Qmp([IO.StreamReader]$reader, [IO.StreamWriter]$writer, [string]$json) {
    $writer.WriteLine($json); $writer.Flush()
    while ($true) {
        $line = $reader.ReadLine()
        if ($null -eq $line) { throw "QMP ferme" }
        if ($line -match '"return"' -or $line -match '"error"') { return $line }
    }
}

$process = [Diagnostics.Process]::Start($start)
try {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $text = ""
    while ([DateTime]::UtcNow -lt $deadline -and -not $process.HasExited) {
        Start-Sleep -Milliseconds 500
        $text = Read-Serial
        if ($text.Contains("gpu selftest: rectangle flushed")) { break }
        if ($text.Contains("KERNEL PANIC") -or $text.Contains("System halted.") -or
            $text.Contains("gpu selftest flush failed")) { break }
    }
    if (-not $text.Contains("gpu selftest: rectangle flushed")) {
        # Diagnostic : registres du vCPU via QMP (aide a localiser un blocage).
        try {
            $diag = [Net.Sockets.TcpClient]::new("127.0.0.1", $QmpPort)
            $ds = $diag.GetStream(); $dr = [IO.StreamReader]::new($ds); $dw = [IO.StreamWriter]::new($ds)
            [void]$dr.ReadLine()
            [void](Invoke-Qmp $dr $dw '{"execute":"qmp_capabilities"}')
            $regs = Invoke-Qmp $dr $dw '{"execute":"human-monitor-command","arguments":{"command-line":"info registers"}}'
            Set-Content -Path (Join-Path $output "regs.txt") -Value $regs
            $diag.Dispose()
        } catch { }
        throw "Auto-test virtio-gpu non atteint : $log"
    }
    $client = [Net.Sockets.TcpClient]::new("127.0.0.1", $QmpPort)
    try {
        $netStream = $client.GetStream()
        $reader = [IO.StreamReader]::new($netStream)
        $writer = [IO.StreamWriter]::new($netStream)
        [void]$reader.ReadLine() # greeting
        [void](Invoke-Qmp $reader $writer '{"execute":"qmp_capabilities"}')
        $request = @{ execute = "screendump"; arguments = @{ filename = $ppm; device = "gpu0"; head = 0 } } | ConvertTo-Json -Compress
        $reply = Invoke-Qmp $reader $writer $request
        if ($reply -match '"error"') { throw "screendump refuse : $reply" }
    } finally { $client.Dispose() }
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    while (-not (Test-Path $ppm) -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 200 }
    Start-Sleep -Milliseconds 300
    $bytes = [IO.File]::ReadAllBytes($ppm)
    $header = [Text.Encoding]::ASCII.GetString($bytes, 0, [Math]::Min(32, $bytes.Length))
    if ($header -notmatch '^P6\s+(\d+)\s+(\d+)\s+255\s') { throw "PPM invalide : $ppm" }
    $width = [int]$Matches[1]; $height = [int]$Matches[2]
    $dataOffset = $Matches[0].Length
    function Get-Pixel([int]$x, [int]$y) {
        $i = $dataOffset + ($y * $width + $x) * 3
        return @($bytes[$i], $bytes[$i + 1], $bytes[$i + 2])
    }
    $red = Get-Pixel 100 100
    $blue = Get-Pixel 10 10
    $blue2 = Get-Pixel ($width - 5) ($height - 5)
    "scanout ${width}x${height} ; (100,100)=$($red -join ',') ; (10,10)=$($blue -join ',') ; coin=$($blue2 -join ',')"
    if ($red[0] -ne 255 -or $red[1] -ne 0 -or $red[2] -ne 0) { throw "Rectangle rouge absent : $ppm" }
    foreach ($p in @($blue, $blue2)) {
        if ($p[0] -ne 0 -or $p[1] -ne 0x40 -or $p[2] -ne 0xC0) { throw "Fond bleu incorrect : $ppm" }
    }
    "virtio-gpu 2D PASS : $ppm"
} finally {
    if (-not $process.HasExited) { $process.Kill() }
    $process.Dispose()
}
