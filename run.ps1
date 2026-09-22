<#
.SYNOPSIS
    ALOS - Build & Run script for Windows
.DESCRIPTION
    Builds ALOS kernel via Docker (cross-compiler) and runs it in QEMU on Windows.
    Equivalent to the Makefile targets: make iso, make run-qemu, make disk.img
.EXAMPLE
    .\run.ps1              # Build + Run
    .\run.ps1 build        # Build only (Docker)
    .\run.ps1 run          # Run only (QEMU)
    .\run.ps1 disk         # Rebuild disk.img only
    .\run.ps1 clean        # Clean build artifacts
    .\run.ps1 debug        # Run QEMU with GDB server (port 1234)
#>

param(
    [Parameter(Position = 0)]
    [ValidateSet("all", "build", "run", "disk", "clean", "debug")]
    [string]$Action = "all"
)

$ErrorActionPreference = "Stop"

# === Configuration ===
$QEMU = "C:\Program Files\qemu\qemu-system-x86_64.exe"
$QEMU_SHARE = "C:\Program Files\qemu\share"
$PROJECT_DIR = $PSScriptRoot
$DOCKER_IMAGE = "alos-build"

# UEFI firmware (EDK2 bundled with QEMU for Windows)
$OVMF_CODE = Join-Path $QEMU_SHARE "edk2-x86_64-code.fd"

# === Helper functions ===
function Write-Step($msg) {
    Write-Host "`n=== $msg ===" -ForegroundColor Cyan
}

function Assert-File($path, $name) {
    if (-not (Test-Path $path)) {
        Write-Host "ERROR: $name not found at $path" -ForegroundColor Red
        exit 1
    }
}

# === Actions ===

function Invoke-Build {
    Write-Step "Building ALOS via Docker"

    # Check Docker
    if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
        Write-Host "ERROR: Docker not found. Install Docker Desktop." -ForegroundColor Red
        exit 1
    }

    # Check if image exists, build if not
    $imageExists = docker images $DOCKER_IMAGE --format "{{.Repository}}" 2>$null
    if (-not $imageExists) {
        Write-Step "Building Docker image '$DOCKER_IMAGE' (first time, may take ~10 min)"
        docker build -t $DOCKER_IMAGE "$PROJECT_DIR"
        if ($LASTEXITCODE -ne 0) { Write-Host "Docker build failed!" -ForegroundColor Red; exit 1 }
    }

    # Run build inside Docker container
    Write-Step "Compiling kernel + creating ISO"
    docker run --rm -v "${PROJECT_DIR}:/root/env" $DOCKER_IMAGE make clean iso
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Build failed!" -ForegroundColor Red
        exit 1
    }

    Write-Step "Build successful"
    Write-Host "  Kernel: alos.elf"
    Write-Host "  ISO:    alos.iso"
    Write-Host "  Disk:   disk.img"
}

function Invoke-Disk {
    Write-Step "Rebuilding disk.img via Docker"

    docker run --rm -v "${PROJECT_DIR}:/root/env" $DOCKER_IMAGE make disk.img
    if ($LASTEXITCODE -ne 0) {
        Write-Host "disk.img build failed!" -ForegroundColor Red
        exit 1
    }

    Write-Step "disk.img rebuilt"
}

function Invoke-Run {
    param([switch]$Debug)

    Assert-File (Join-Path $PROJECT_DIR "alos.iso") "alos.iso"
    Assert-File (Join-Path $PROJECT_DIR "disk.img") "disk.img"
    Assert-File $QEMU "QEMU"
    Assert-File $OVMF_CODE "OVMF firmware"

    $serial_log = Join-Path $PROJECT_DIR "serial.log"
    if (Test-Path $serial_log) { Remove-Item $serial_log }
    New-Item -ItemType File -Path $serial_log -Force | Out-Null

    $mode = if ($Debug) { "DEBUG mode (GDB on port 1234)" } else { "normal mode" }
    Write-Step "Starting QEMU ($mode)"

    $qemuArgs = @(
        "-cdrom", (Join-Path $PROJECT_DIR "alos.iso"),
        "-m", "4096M",
        "-cpu", "qemu64",
        "-smp", "2",
        "-drive", "if=pflash,format=raw,readonly=on,file=$OVMF_CODE",
        "-boot", "d",
        "-netdev", "user,id=net0,net=10.0.2.0/24,dhcpstart=10.0.2.15,hostfwd=tcp::8080-:80",
        "-device", "virtio-net-pci,netdev=net0",
        "-drive", "file=$(Join-Path $PROJECT_DIR 'disk.img'),format=raw,index=0,media=disk",
        "-serial", "file:$serial_log",
        "-no-reboot",
        "-no-shutdown"
    )

    if ($Debug) {
        $qemuArgs += @("-s", "-S")
    }

    Write-Host "  Serial log: $serial_log" -ForegroundColor DarkGray
    Write-Host "  Network:    NAT (host:8080 -> guest:80)" -ForegroundColor DarkGray
    Write-Host "  RAM:        4 GB" -ForegroundColor DarkGray
    Write-Host "  Firmware:   UEFI (EDK2)" -ForegroundColor DarkGray
    if ($Debug) {
        Write-Host "  GDB:        localhost:1234 (waiting for connection)" -ForegroundColor Yellow
    }
    Write-Host ""

    & $QEMU @qemuArgs
}

function Invoke-Clean {
    Write-Step "Cleaning build artifacts"

    docker run --rm -v "${PROJECT_DIR}:/root/env" $DOCKER_IMAGE make clean
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Clean failed!" -ForegroundColor Red
        exit 1
    }

    # Remove Windows-generated files
    $toRemove = @("serial.log", "disk.vdi")
    foreach ($f in $toRemove) {
        $path = Join-Path $PROJECT_DIR $f
        if (Test-Path $path) {
            Remove-Item $path
            Write-Host "  Removed: $f"
        }
    }

    Write-Step "Clean complete"
}

# === Main ===
switch ($Action) {
    "all" {
        Invoke-Build
        Invoke-Run
    }
    "build" {
        Invoke-Build
    }
    "run" {
        Invoke-Run
    }
    "disk" {
        Invoke-Disk
    }
    "clean" {
        Invoke-Clean
    }
    "debug" {
        Invoke-Build
        Invoke-Run -Debug
    }
}
