param(
    [switch]$SkipBuild,
    [switch]$Runtime,
    [switch]$Fleet,
    [switch]$EntropyUnavailable,
    [switch]$StackProtectorOnly,
    [switch]$UnixSocketOnly,
    [switch]$DnsOnly,
    [string]$BaseSmokePath,
    [string]$MojoSmokePath,
    [string]$BaseNativeDirectory,
    [string[]]$BaseSmokeTargets = @("base_smoke", "atomic_smoke", "file_comparison_smoke",
        "stack_trace_smoke", "process_smoke", "discardable_capability_smoke", "thread_smoke", "elf_reader_smoke", "io_pump_smoke"),
    [switch]$BaseSmokeOnly,
    [switch]$ComplexCpp,
    [int]$ComplexRuns = 1,
    [string]$Cpu = "qemu64",
    [string]$Qemu = "C:\Program Files\qemu\qemu-system-x86_64.exe",
    [string]$OutputDirectory = (Join-Path ([IO.Path]::GetTempPath()) ("alos-vm-" + [guid]::NewGuid())),
    [int]$TimeoutSeconds = 180
)

$ErrorActionPreference = "Stop"
if ($StackProtectorOnly -and ($Runtime -or $ComplexCpp)) {
    throw "StackProtectorOnly est une suite isolee, incompatible avec Runtime/ComplexCpp."
}
if ($BaseSmokePath -and $BaseNativeDirectory) {
    throw "Choisir BaseSmokePath ou BaseNativeDirectory."
}
$hasBaseSmoke = [bool]($BaseSmokePath -or $BaseNativeDirectory)
if ($MojoSmokePath -and ($hasBaseSmoke -or $Runtime -or $Fleet -or $ComplexCpp -or
    $StackProtectorOnly -or $UnixSocketOnly -or $EntropyUnavailable)) {
    throw "MojoSmokePath est une suite isolee avec source d'entropie reelle."
}
$hasNativeSmoke = [bool]($hasBaseSmoke -or $MojoSmokePath)
if ($UnixSocketOnly -and ($Runtime -or $Fleet -or $ComplexCpp -or
    $StackProtectorOnly -or $hasBaseSmoke -or $DnsOnly)) {
    throw "UnixSocketOnly est une suite isolee."
}
if ($DnsOnly -and ($Runtime -or $Fleet -or $ComplexCpp -or
    $StackProtectorOnly -or $hasBaseSmoke -or $MojoSmokePath -or
    $EntropyUnavailable)) {
    throw "DnsOnly est une suite isolee avec reseau/DNS reel."
}
if ($BaseSmokeOnly -and (-not $hasBaseSmoke -or $Runtime -or $Fleet -or
    $ComplexCpp -or $StackProtectorOnly)) {
    throw "BaseSmokeOnly exige BaseSmokePath et une suite isolee."
}
if ($hasBaseSmoke -and $EntropyUnavailable) {
    throw "Le smoke Base protege exige une vraie source d'entropie."
}
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

function Assert-WideConsoleBytes {
    $stream = [IO.FileStream]::new($log, [IO.FileMode]::Open,
        [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
    $memory = [IO.MemoryStream]::new()
    try {
        $stream.CopyTo($memory)
        $bytes = $memory.ToArray()
    } finally { $memory.Dispose(); $stream.Dispose() }
    $expected = [Text.Encoding]::ASCII.GetBytes("wide-format-test: console-bytes[") +
        [byte[]](0xc3,0xa9,0xf0,0x9f,0x98,0x80,0) +
        [Text.Encoding]::ASCII.GetBytes("]console-bytes-end")
    $matches = 0
    for ($i = 0; $i -le $bytes.Length - $expected.Length; $i++) {
        $j = 0
        while ($j -lt $expected.Length -and $bytes[$i + $j] -eq $expected[$j]) { $j++ }
        if ($j -eq $expected.Length) { $matches++ }
    }
    if ($matches -ne 1) { throw "Octets console wide incorrects (matches=$matches) : $log" }
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
wide-format-test
pthread-test
pthread-test
pthread-rwlock-test
pthread-attr-test
thread-id-test
calendar-test
env-test
uname-test
libc-common-test
fd-io-test
fs-metadata-test
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
if ($Fleet) {
    $entropyCommand = if ($EntropyUnavailable) { "entropy-test --unavailable" } else { "entropy-test" }
    $protectorCommand = if ($EntropyUnavailable) {
        "stack-protector-driver --unavailable"
    } else { "stack-protector-driver" }
    $startup = $startup.Replace("echo vm-suite-complete",
        "base-math-test`n$protectorCommand`necho vm-suite-complete")
    $startup = $startup.Replace("echo vm-suite-complete",
        "posix-file-test`nstdio-file-test`npositional-io-test`npath-match-test`nmincore-test`nsystem-info-test`nvector-io-test`nshared-memory-test`nresource-test`nnative-compat-test`nfs-attributes-test`nmsync-test`nprocess-control-test`npath-ops-test`nint128-runtime-test`nunix-socket-test`npipe-test`nsocket-test`ninet-test`n$entropyCommand`necho vm-suite-complete")
}
if ($StackProtectorOnly) {
    $command = if ($EntropyUnavailable) {
        "stack-protector-driver --unavailable"
    } else { "stack-protector-driver" }
    $startup = "echo vm-suite-begin`nbase-math-test`n$command`necho vm-suite-complete"
}
if ($UnixSocketOnly) {
    $startup = "echo vm-suite-begin`nint128-runtime-test`nunix-socket-test`necho vm-suite-complete"
}
if ($DnsOnly) {
    $startup = "echo vm-suite-begin`ninet-test --dns`necho vm-suite-complete"
}
$basePrograms = @()
if ($hasNativeSmoke) {
    if ($MojoSmokePath) {
        $basePrograms = @(@{ Path = (Resolve-Path -LiteralPath $MojoSmokePath).Path
            Name = "chromium-mojo-smoke"; Marker = "chromium-mojo-smoke: PASS" })
    } elseif ($BaseSmokePath) {
        $basePrograms = @(@{ Path = (Resolve-Path -LiteralPath $BaseSmokePath).Path
            Name = "chromium-base-smoke"; Marker = "chromium-base-smoke: Time PASS" })
    } else {
        $availableTests = @(
            @{ File = "base_smoke"; Name = "chromium-base-smoke"; Marker = "chromium-base-smoke: Time PASS" },
            @{ File = "atomic_smoke"; Name = "chromium-atomic-smoke"; Marker = "nativeatomic-runtime-test: PASS" },
            @{ File = "file_comparison_smoke"; Name = "chromium-file-smoke"; Marker = "nativefile-comparison-test: PASS" },
            @{ File = "stack_trace_smoke"; Name = "chromium-stack-smoke"; Marker = "chromium-stack-trace-smoke: PASS" },
            @{ File = "process_smoke"; Name = "chromium-process-smoke"; Marker = "chromium-process-smoke: PASS" },
            @{ File = "discardable_capability_smoke"; Name = "chromium-discardable-smoke"; Marker = "chromium-discardable-smoke: PASS" },
            @{ File = "thread_smoke"; Name = "chromium-thread-smoke"; Marker = "chromium-thread-smoke: PASS" },
            @{ File = "elf_reader_smoke"; Name = "chromium-elf-smoke"; Marker = "chromium-elf-reader-smoke: PASS" },
            @{ File = "io_pump_smoke"; Name = "chromium-io-smoke"; Marker = "base-io-pump-smoke: PASS" }
        )
        if (-not $BaseSmokeTargets.Count -or
            @($BaseSmokeTargets | Select-Object -Unique).Count -ne $BaseSmokeTargets.Count) {
            throw "Liste des smokes Base vide ou dupliquee."
        }
        foreach ($target in $BaseSmokeTargets) {
            $test = $availableTests | Where-Object { $_.File -eq $target }
            if (-not $test) { throw "Smoke Base inconnu : $target" }
            $basePrograms += @{ Path = (Resolve-Path -LiteralPath (
                Join-Path $BaseNativeDirectory $test.File)).Path
                Name = $test.Name; Marker = $test.Marker }
        }
    }
    foreach ($program in $basePrograms) {
        Copy-Item -LiteralPath $program.Path -Destination (Join-Path $output $program.Name)
        docker run --rm -v "${output}:/artifacts" alos-runtime `
            strip --strip-debug "/artifacts/$($program.Name)"
        if ($LASTEXITCODE) { throw "Strip copie smoke echoue : $($program.Name)" }
    }
    $commands = ($basePrograms | ForEach-Object { $_.Name }) -join "`n"
    if ($BaseSmokeOnly -or $MojoSmokePath) {
        $startup = "echo vm-suite-begin`n$commands`necho vm-suite-complete"
    } else {
        $startup = $startup.Replace("echo vm-suite-complete",
            "$commands`necho vm-suite-complete")
    }
}
[IO.File]::WriteAllText((Join-Path $output "startup.sh"), $startup.Replace("`r", "") + "`n")
[IO.File]::WriteAllBytes((Join-Path $output "wide-io-valid"), [byte[]](0xc3,0xa9,0xf0,0x9f,0x98,0x80,0))
[IO.File]::WriteAllBytes((Join-Path $output "wide-io-output"), [byte[]](33,33,33,33,33,33,33))
[IO.File]::WriteAllBytes((Join-Path $output "wide-io-invalid"), [byte[]](0xff))
[IO.File]::WriteAllBytes((Join-Path $output "wide-io-incomplete"), [byte[]](0xc3))
[IO.File]::WriteAllBytes((Join-Path $output "fd-io-data"),
    [Text.Encoding]::ASCII.GetBytes("fixture-" + ("." * 24)))
[IO.File]::WriteAllBytes((Join-Path $output "fd-io-zero"), [byte[]]@())
[IO.File]::WriteAllBytes((Join-Path $output "metadata-payload"),
    [Text.Encoding]::ASCII.GetBytes("metadata-fixture`n"))
[IO.File]::WriteAllBytes((Join-Path $output "metadata-malformed"), [byte[]](0x78))
$metadataCommands = @(
    "mkdir /metadata-fixture",
    "mkdir /posix-test",
    "mkdir /metadata-fixture/empty",
    "write /artifacts/metadata-payload /metadata-fixture/payload",
    "set_inode_field /metadata-fixture/payload mode 0100640",
    "set_inode_field /metadata-fixture/payload uid 42",
    "set_inode_field /metadata-fixture/payload gid 43",
    "set_inode_field /metadata-fixture/payload atime 1700000001",
    "set_inode_field /metadata-fixture/payload mtime 1700000002",
    "set_inode_field /metadata-fixture/payload ctime 1700000003",
    "write /artifacts/metadata-malformed /metadata-fixture/malformed",
    "set_inode_field /metadata-fixture/malformed mode 040755"
) -join "`n"
[IO.File]::WriteAllText((Join-Path $output "metadata.commands"), $metadataCommands + "`n")
# Windows PowerShell 5 transforme les lignes stderr des programmes natifs en
# ErrorRecord. debugfs ecrit sa banniere/version sur stderr meme quand il
# reussit, ce qui devenait une erreur terminante avec ErrorActionPreference=Stop.
$previousErrorActionPreference = $ErrorActionPreference
$ErrorActionPreference = "Continue"
try {
    docker run --rm -v "${repo}:/root/env" -v "${output}:/artifacts" alos-build sh -c `
        'truncate -s 64M /artifacts/test.disk && mkfs.ext2 -q -F -d fs_root /artifacts/test.disk && debugfs -w -R "rm /config/startup.sh" /artifacts/test.disk && debugfs -w -R "write /artifacts/startup.sh /config/startup.sh" /artifacts/test.disk && for name in wide-io-valid wide-io-output wide-io-invalid wide-io-incomplete fd-io-data fd-io-zero; do debugfs -w -R "write /artifacts/$name /$name" /artifacts/test.disk || exit; done && debugfs -w -f /artifacts/metadata.commands /artifacts/test.disk' `
        *> (Join-Path $output "image.log")
    $imageExitCode = $LASTEXITCODE
} finally {
    $ErrorActionPreference = $previousErrorActionPreference
}
if ($imageExitCode -ne 0) { throw "Creation image echouee : $output\image.log" }
foreach ($program in $basePrograms) {
    $name = $program.Name
    $previousErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        docker run --rm -v "${output}:/artifacts" alos-build sh -c `
            "debugfs -w -R `"write /artifacts/$name /bin/$name`" /artifacts/test.disk && debugfs -R `"dump /bin/$name /artifacts/verify-$name`" /artifacts/test.disk && cmp /artifacts/$name /artifacts/verify-$name" `
            *> (Join-Path $output "$name-image.log")
        $stageExitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }
    if ($stageExitCode -ne 0) { throw "Staging smoke Base echoue : $name" }
    Remove-Item -LiteralPath (Join-Path $output "verify-$name")
}

$start = [Diagnostics.ProcessStartInfo]::new()
$start.FileName = $Qemu
$start.UseShellExecute = $false

# ProcessStartInfo.ArgumentList n'existe que sur les runtimes .NET modernes.
# Windows PowerShell 5.1 (.NET Framework) expose la propriete mais elle peut
# etre absente/null selon le runtime. Construire d'abord une liste neutre puis
# utiliser ArgumentList quand disponible, sinon .Arguments.
$qemuArguments = @(
    "-cdrom", (Join-Path $repo "alos.iso"),
    "-drive", "file=$(Join-Path $output 'test.disk'),format=raw,if=ide",
    "-m", "1024M", "-cpu", $Cpu, "-smp", "1",
    "-netdev", "user,id=net0", "-device", "virtio-net-pci,netdev=net0",
    "-display", "none", "-serial", "file:$log", "-monitor", "none",
    "-no-reboot", "-no-shutdown"
)
if (($Fleet -or $StackProtectorOnly -or $hasNativeSmoke) -and -not $EntropyUnavailable) {
    $qemuArguments += @("-object", "rng-builtin,id=alos_rng", "-device",
        "virtio-rng-pci,rng=alos_rng,disable-modern=on")
}

if ($null -ne $start.ArgumentList) {
    foreach ($argument in $qemuArguments) {
        $start.ArgumentList.Add([string]$argument)
    }
} else {
    # Tous les arguments QEMU utilises ici sont sans guillemets embarques.
    # Les entourer systematiquement protege aussi les chemins contenant des espaces.
    $quotedArguments = foreach ($argument in $qemuArguments) {
        '"' + ([string]$argument).Replace('"', '\"') + '"'
    }
    $start.Arguments = $quotedArguments -join " "
}

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
    if (-not $StackProtectorOnly -and -not $BaseSmokeOnly -and -not $UnixSocketOnly -and
        -not $DnsOnly -and -not $MojoSmokePath) {
      foreach ($marker in @(
        "mmap-test: ALL PASS", "fork-test: PASS",
        "exec-test: PASS after 40", "Compteur final", "Reply received, seq:"
    )) {
        if (-not $text.Contains($marker)) { throw "Resultat manquant '$marker' : $log" }
    }
    if ($text.Contains("mmap-test: FAIL") -or $text.Contains("fork-test: FAIL") -or
        $text.Contains("exec-test: FAIL")) { throw "Regression detectee : $log" }
    }
    if ($Runtime) {
        foreach ($envMarker in @("uname-test: PASS", "env-test: PASS",
            "env-test: exec inherited constructor PASS", "env-test: exec empty PASS")) {
            if (-not $text.Contains($envMarker)) {
                throw "Resultat environnement manquant '$envMarker' : $log"
            }
        }
        if ($text.Contains("env-test: FAIL")) { throw "Regression environnement : $log" }
        Assert-WideConsoleBytes
        foreach ($marker in @("simd-context-test: PASS", "tls-test: PASS",
            "time-test: PASS", "strtod-test: PASS", "printf-test: PASS", "wide-format-test: PASS", "pthread-test: PASS", "pthread-rwlock-test: PASS", "pthread-attr-test: PASS", "thread-id-test: PASS", "calendar-test: PASS", "libc-common-test: PASS", "fd-io-test: PASS", "fs-metadata-test: PASS", "crt-cxx-test: PASS",
            "tls-cxx-test: PASS", "GLOBAL CTOR", "TLS CTOR",
            "TLS DTOR", "GLOBAL DTOR")) {
            if (-not $text.Contains($marker)) { throw "Resultat manquant '$marker' : $log" }
        }
        if ($text -match "(simd-context-test|tls-test|time-test|strtod-test|printf-test|wide-format-test|pthread-test|pthread-rwlock-test|pthread-attr-test|thread-id-test|calendar-test|libc-common-test|fd-io-test|fs-metadata-test|crt-cxx-test): FAIL") {
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
        foreach ($outputMarker in @(
            "libc-common-test: stdout[no-newline]stdout-end",
            "libc-common-test: stderr[bytes]stderr-end",
            "libc-common-test: perror-prefix: Invalid argument",
            "Numerical result out of range",
            "Input/output error"
        )) {
            if (-not $text.Contains($outputMarker)) {
                throw "Sortie libc manquante '$outputMarker' : $log"
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
        if ($Fleet -or $StackProtectorOnly) {
            if (-not $text.Contains("base-math-test: PASS") -or
                $text.Contains("base-math-test: FAIL")) {
                throw "Regression math Base : $log"
            }
            $protectorMarker = if ($EntropyUnavailable) {
                "stack-protector-driver: unavailable PASS"
            } else { "stack-protector-driver: PASS" }
            if (-not $text.Contains($protectorMarker) -or
                $text.Contains("stack-protector-driver: FAIL") -or
                $text.Contains("stack-protector-test: FAIL")) {
                throw "Regression stack protector : $log"
            }
            if ($EntropyUnavailable) {
                if (-not $text.Contains("stack-protector: secure guard unavailable") -or
                    $text.Contains("stack-protector-test: PASS")) {
                    throw "Guard non fail-closed : $log"
                }
            } elseif (-not $text.Contains("stack-protector-test: PASS") -or
                      -not $text.Contains("stack-protector: stack corruption detected")) {
                throw "Protection de pile non verifiee : $log"
            }
        }
        foreach ($program in $basePrograms) {
            if (-not $text.Contains($program.Marker)) {
                throw "Smoke natif Chromium Base incomplet : $($program.Name) : $log"
            }
        }
        if ($hasNativeSmoke -and $text -match "(?m)^(chromium-.*smoke|base-io-pump-smoke|nativeatomic-runtime-test|nativefile-comparison-test): FAIL\b") {
            throw "Smoke natif Chromium Base echoue : $log"
        }
        if ($Fleet) {
            $entropyMarker = if ($EntropyUnavailable) {
                "entropy-test: unavailable PASS"
            } else { "entropy-test: source/concurrent PASS" }
            foreach ($marker in @("posix-file-test: PASSED", "stdio-file-test: PASS", "positional-io-test: PASS", "path-match-test: PASS", "mincore-test: PASS", "system-info-test: PASS", "pipe-test: PASS",
                "socket-test: PASS", "inet-test: PASS", "vector-io-test: PASS",
                "[shared-memory-test] PASS", "[resource-test] PASS", "native-compat-test PASS", "fs-attributes-test: PASS", "msync-test: PASS", "process-control-test: PASS", "path-ops-test: PASS", $entropyMarker)) {
                if (-not $text.Contains($marker)) { throw "Resultat fleet manquant '$marker' : $log" }
            }
            if ($text -match "(?m)^(posix-file-test|stdio-file-test|positional-io-test|path-match-test|mincore-test|system-info-test|pipe-test|socket-test|inet-test|entropy-test)(:| )\s*FAIL(?:ED)?\b") {
                throw "Regression fleet : $log"
            }
            if ($text -match "(?m)^(vector-io-test(:| )|fs-attributes-test: |msync-test: |process-control-test: |path-ops-test: |\[(shared-memory-test|resource-test)\] |native-compat-test )FAIL\b") {
                throw "Regression frontier : $log"
            }
        }
    $suite = if ($Runtime) { "runtime" } else { "VM" }
    if ($Fleet -and (-not $text.Contains("unix-socket-test: PASS") -or
        -not $text.Contains("int128-runtime-test: PASS") -or
        $text.Contains("unix-socket-test: FAIL") -or
        $text.Contains("int128-runtime-test: FAIL"))) {
        throw "New local transport/runtime regression : $log"
    }
    if ($MojoSmokePath -and -not $text.Contains("chromium-mojo-smoke: child shared-handle PASS")) {
        throw "Mojo child shared-buffer result missing : $log"
    }
    if ($UnixSocketOnly -and (-not $text.Contains("unix-socket-test: PASS") -or
        $text.Contains("unix-socket-test: FAIL"))) {
        throw "Unix socket regression : $log"
    }
    if ($UnixSocketOnly -and (-not $text.Contains("int128-runtime-test: PASS") -or
        $text.Contains("int128-runtime-test: FAIL"))) {
        throw "Int128 compiler runtime regression : $log"
    }
    if ($DnsOnly -and (-not $text.Contains("inet-test: DNS PASS") -or
        -not $text.Contains("inet-test: PASS") -or
        $text.Contains("inet-test FAIL"))) {
        throw "DNS/getaddrinfo regression : $log"
    }
    Write-Host "ALOS $suite suite PASS : $log"
} finally {
    if (-not $process.HasExited) { Stop-Process -Id $process.Id }
    $process.Dispose()
}
