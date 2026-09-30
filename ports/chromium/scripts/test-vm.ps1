param(
    [switch]$SkipBuild,
    [switch]$Runtime,
    [switch]$ComplexCpp,
    [int]$ComplexRuns = 1,
    [string]$Cpu = "qemu64",
    [string]$Qemu = "C:\Program Files\qemu\qemu-system-x86_64.exe",
    [string]$OutputDirectory = (Join-Path ([IO.Path]::GetTempPath()) ("alos-vm-" + [guid]::NewGuid())),
    [int]$TimeoutSeconds = 180
)

$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$output = (Resolve-Path $OutputDirectory).Path
$log = Join-Path $output "serial.log"

function Read-Serial {
    $stream = [IO.FileStream]::new($log, [IO.FileMode]::Open,
        [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
    $reader = [IO.StreamReader]::new($stream)
    try { return $reader.ReadToEnd() } finally { $reader.Dispose() }
}

if (-not (Test-Path $Qemu)) { throw "QEMU introuvable : $Qemu" }
if (-not $SkipBuild) {
    docker run --rm -v "${repo}:/root/env" alos-runtime sh -c `
        'make -B -s alos.elf && make -B -s -C src/userland all && make -s fs_root && make -s iso -o disk.img -o verify-gui-image' `
        *> (Join-Path $output "build.log")
    if ($LASTEXITCODE -ne 0) { throw "Build echoue : $output\build.log" }
}
if ($ComplexCpp) {
    if ($ComplexRuns -lt 1 -or $ComplexRuns -gt 100) { throw "ComplexRuns doit etre entre 1 et 100." }
    $complex = Join-Path $repo "src\userland\complex-cpp-test"
    if (-not (Test-Path $complex)) { throw "complex-cpp-test non compile : $complex" }
    Copy-Item -LiteralPath $complex -Destination (Join-Path $repo "fs_root\bin\complex-cpp-test")
}

# Ne jamais reformater le disque de travail de l'utilisateur.
$startup = @"
echo vm-suite-begin
mmap-test
fork-test
exec-test
threads-test
ls /bin
ping 10.0.2.2 -c 1
echo vm-suite-complete
"@
if ($Runtime) {
    $startup = @"
echo vm-suite-begin
simd-context-test
simd-context-test
simd-context-test
tls-test
tls-test
tls-test
time-test
strtod-test
printf-test
pthread-test
pthread-test
crt-cxx-test
tls-cxx-test
mmap-test
fork-test
exec-test
threads-test
ls /bin
ping 10.0.2.2 -c 1
echo vm-suite-complete
"@
}
if ($ComplexCpp) {
    $startup = $startup.Replace("echo vm-suite-complete", ("complex-cpp-test`n" * $ComplexRuns) +
        "echo vm-suite-complete")
}
[IO.File]::WriteAllText((Join-Path $output "startup.sh"), $startup.Replace("`r", "") + "`n")
docker run --rm -v "${repo}:/root/env" -v "${output}:/artifacts" alos-build sh -c `
    'truncate -s 64M /artifacts/test.disk && mkfs.ext2 -q -F -d fs_root /artifacts/test.disk && debugfs -w -R "rm /config/startup.sh" /artifacts/test.disk && debugfs -w -R "write /artifacts/startup.sh /config/startup.sh" /artifacts/test.disk' `
    *> (Join-Path $output "image.log")
if ($LASTEXITCODE -ne 0) { throw "Creation image echouee : $output\image.log" }

$start = [Diagnostics.ProcessStartInfo]::new()
$start.FileName = $Qemu
$start.UseShellExecute = $false
foreach ($argument in @(
    "-cdrom", (Join-Path $repo "alos.iso"),
    "-drive", "file=$(Join-Path $output 'test.disk'),format=raw,if=ide",
    "-m", "1024M", "-cpu", $Cpu, "-smp", "1",
    "-netdev", "user,id=net0", "-device", "virtio-net-pci,netdev=net0",
    "-display", "none", "-serial", "file:$log", "-monitor", "none",
    "-no-reboot", "-no-shutdown"
)) { $start.ArgumentList.Add($argument) }

$process = [Diagnostics.Process]::Start($start)
try {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $complete = $false
    $text = ""
    while ([DateTime]::UtcNow -lt $deadline -and -not $process.HasExited) {
        Start-Sleep -Milliseconds 500
        if (Test-Path $log) {
            $text = Read-Serial
            if ($text.Contains("vm-suite-complete")) { $complete = $true; break }
            if ($text.Contains("KERNEL PANIC") -or $text.Contains("System halted.")) { break }
        }
    }
    if (-not $complete) { throw "Suite incomplete : $log" }
    foreach ($marker in @(
        "mmap-test: ALL PASS", "fork-test: PASS",
        "exec-test: PASS after 40", "Compteur final", "Reply received, seq:"
    )) {
        if (-not $text.Contains($marker)) { throw "Resultat manquant '$marker' : $log" }
    }
    if ($text.Contains("mmap-test: FAIL") -or $text.Contains("fork-test: FAIL") -or
        $text.Contains("exec-test: FAIL")) { throw "Regression detectee : $log" }
    if ($Runtime) {
        foreach ($marker in @("simd-context-test: PASS", "tls-test: PASS",
            "time-test: PASS", "strtod-test: PASS", "printf-test: PASS", "pthread-test: PASS", "crt-cxx-test: PASS",
            "tls-cxx-test: PASS", "GLOBAL CTOR", "TLS CTOR",
            "TLS DTOR", "GLOBAL DTOR")) {
            if (-not $text.Contains($marker)) { throw "Resultat manquant '$marker' : $log" }
        }
        if ($text -match "(simd-context-test|tls-test|time-test|strtod-test|printf-test|pthread-test|crt-cxx-test): FAIL") {
            throw "Regression runtime : $log"
        }
        foreach ($expected in @(
            @{ Marker = "simd-context-test: PASS"; Count = 3 },
            @{ Marker = "tls-test: PASS"; Count = 3 },
            @{ Marker = "pthread-test: PASS"; Count = 2 }
        )) {
            if ([regex]::Matches($text, [regex]::Escape($expected.Marker)).Count -ne $expected.Count) {
                throw "Nombre de repetitions incorrect pour '$($expected.Marker)' : $log"
            }
        }
        if ($text -notmatch '(?s)GLOBAL CTOR.*TLS CTOR.*MAIN.*TLS DTOR.*GLOBAL DTOR') {
            throw "Ordre des constructeurs/destructeurs incorrect : $log"
        }
    }
    if ($ComplexCpp) {
        if ([regex]::Matches($text, "complex-cpp-test: ALL PASS").Count -ne $ComplexRuns -or
            [regex]::Matches($text, "complex-cpp-test: GLOBAL DTOR PASS").Count -ne $ComplexRuns -or
            $text.Contains("complex-cpp-test: FAIL")) {
            throw "Echec complex-cpp-test : $log"
        }
    }
    $suite = if ($Runtime) { "runtime" } else { "VM" }
    Write-Host "ALOS $suite suite PASS : $log"
} finally {
    if (-not $process.HasExited) { Stop-Process -Id $process.Id }
    $process.Dispose()
}
