param(
    [Parameter(Mandatory)][string]$ChromiumDirectory,
    [Parameter(Mandatory)][string]$LibcxxDirectory,
    [string]$GnDirectory = (Join-Path $ChromiumDirectory "..\gn-linux-amd64"),
    [string[]]$Targets = @("base:base"),
    [switch]$GenOnly,
    [switch]$KeepGoing,
    [int]$Jobs = 8
)
# gn gen out/alos puis ninja dans l'image alos-runtime. Les outils du build
# (gn, python, protoc...) sont Linux host ; les objets du toolchain par defaut
# //build/toolchain/alos:clang_x64 visent ALOS avec la libc et libc++ ALOS.
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
$src = (Resolve-Path $ChromiumDirectory).Path
$libcxx = (Resolve-Path $LibcxxDirectory).Path
$gn = (Resolve-Path $GnDirectory).Path
foreach ($library in @("libc++.a", "libc++abi.a")) {
    if (-not (Test-Path (Join-Path $libcxx "lib\$library"))) {
        throw "Bibliotheque ALOS absente : $library (voir build-libcxx.ps1)"
    }
}
$args_gn = @(
    'target_os = "alos"', 'target_cpu = "x64"', 'is_debug = false',
    'is_component_build = false', 'use_sysroot = false', 'enable_rust = false',
    'enable_chromium_prelude = false', 'clang_use_chrome_plugins = false',
    'use_fuzztest_wrapper = false', 'use_llvm_libatomic = false',
    '# Concerne uniquement la libc++ in-tree de Chromium : la cible ALOS',
    '# utilise la libc++ LLVM 18.1.8 ALOS du toolchain //build/toolchain/alos.',
    'use_custom_libcxx = false', 'use_custom_libcxx_for_host = false'
) -join "`n"
New-Item -ItemType Directory -Force (Join-Path $src "out\alos") | Out-Null
[IO.File]::WriteAllText((Join-Path $src "out\alos\args.gn"), "$args_gn`n")
$mounts = @("run", "--rm", "-v", "${src}:/chromium", "-v", "${gn}:/gn:ro",
            "-v", "${libcxx}:/libcxx:ro", "-v", "${repo}:/alos:ro",
            "-w", "/chromium", "alos-runtime")
& docker @mounts /gn/gn gen out/alos
if ($LASTEXITCODE) { throw "gn gen out/alos echoue." }
if ($GenOnly) { return }
$ninja = @("ninja", "-C", "out/alos", "-j", "$Jobs")
if ($KeepGoing) { $ninja += @("-k", "0") }
& docker @mounts @ninja @Targets
if ($LASTEXITCODE) { throw "ninja $Targets echoue." }
