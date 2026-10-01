param(
    [Parameter(Mandatory)][string]$ChromiumDirectory,
    [string]$LibcxxDirectory = (Join-Path $ChromiumDirectory "..\alos-libcxx-21"),
    [string]$GnDirectory = (Join-Path $ChromiumDirectory "..\gn-linux-amd64"),
    [string[]]$Targets = @("base:base"),
    [switch]$GenOnly,
    [switch]$NativeTests,
    [switch]$OzoneProbe,
    [switch]$MojoTests,
    [switch]$MojoBindings,
    [switch]$KeepGoing,
    [switch]$Rust,
    [switch]$V8Probe,
    [int]$Jobs = 8
)
# gn gen out/alos puis ninja dans l'image alos-runtime. Les outils du build
# (gn, python, protoc...) sont Linux host ; les objets du toolchain par defaut
# //build/toolchain/alos:clang_x64 visent ALOS avec la libc et libc++ ALOS.
$ErrorActionPreference = "Stop"
if ($OzoneProbe) { $NativeTests = $true }
if ($MojoBindings) { $MojoTests = $true }
# Sonde de compilation V8 ciblee (//v8:v8_libbase) dans out/alos-v8 : elle ne
# remplace pas le profil out/alos et ne construit ni ne lie le moteur complet.
if ($V8Probe -and $MojoBindings) { throw "-V8Probe et -MojoBindings utilisent des root targets distincts." }
if ($Rust -and ($V8Probe -or $MojoBindings)) { throw "-Rust utilise un root target dedie." }
if ($V8Probe -and -not $PSBoundParameters.ContainsKey("Targets")) { $Targets = @("v8:v8_libbase") }
if ($Rust -and -not $PSBoundParameters.ContainsKey("Targets")) { $Targets = @("alos_rust:rust_smoke") }
$outDirectory = if ($V8Probe) { "out/alos-v8" } elseif ($Rust) { "out/alos-rust" } else { "out/alos" }
$rustEnabled = $Rust -or $OzoneProbe
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
$src = (Resolve-Path $ChromiumDirectory).Path
$revision = & git -C $src rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or
    $revision.Trim() -ne "670b6f192f4668d2ac2c06bd77ec3e4eeda7d648") {
    throw "Chromium 140.0.7339.80 (670b6f192f4668d2ac2c06bd77ec3e4eeda7d648) est requis."
}
$libcxx = (Resolve-Path $LibcxxDirectory).Path
$gn = (Resolve-Path $GnDirectory).Path
$toolchainTemplate = Join-Path $repo "ports\chromium\build\alos-toolchain.gn"
$toolchainDirectory = Join-Path $src "build\toolchain\alos"
$toolchainDestination = Join-Path $toolchainDirectory "BUILD.gn"
if (Test-Path -LiteralPath $toolchainDestination) {
    $expected = [IO.File]::ReadAllText($toolchainTemplate).Replace("`r`n", "`n")
    $actual = [IO.File]::ReadAllText($toolchainDestination).Replace("`r`n", "`n")
    if ($actual -ne $expected) {
        $beforeLibm = $expected.Replace(
            '  extra_ldflags += " $alos_root/src/userland/libm/libm.a"' + "`n", "")
        if ($actual -ne $beforeLibm) {
            throw "Toolchain ALOS local divergent : $toolchainDestination"
        }
        Copy-Item -LiteralPath $toolchainTemplate -Destination $toolchainDestination
    }
} else {
    New-Item -ItemType Directory -Force $toolchainDirectory | Out-Null
    Copy-Item -LiteralPath $toolchainTemplate -Destination $toolchainDestination
}
foreach ($patchName in @(
    "chromium-140.0.7339.80-alos-clocks.patch",
    "chromium-140.0.7339.80-alos-cctz.patch",
    "chromium-140.0.7339.80-alos-raw-stack-diagnostics.patch",
    "chromium-alos-pa-native-stack-collector.patch",
    "chromium-alos-native-thread-platform.patch",
    "chromium-alos-native-thread-attributes.patch",
    "abseil-alos-static-elf-diagnostics.patch",
    "abseil-alos-thread-id.patch",
    "abseil-alos-thread-identity-no-signals.patch",
    "abseil-alos-native-mmap-allocator.patch",
    "chromium-alos-abseil-optional-signals.patch",
    "chromium-140.0.7339.80-alos-atomic-copy.patch",
    "chromium-140.0.7339.80-alos-file-comparison.patch",
    "chromium-alos-stack-trace-signal-types.patch",
    "chromium-alos-native-stack-diagnostics.patch",
    "chromium-alos-native-tests.patch",
    "base-message-pump-alos-dispatch.patch",
    "chromium-alos-system-memory.patch",
    "chromium-alos-native-resources.patch",
    "chromium-alos-native-shared-memory.patch",
    "chromium-alos-drive-info-capability.patch",
    "chromium-alos-native-malloc-metrics.patch",
    "chromium-alos-unix-credentials-capability.patch",
    "chromium-alos-process-title-groups.patch",
    "chromium-alos-shared-purge-capability.patch",
    "chromium-alos-disable-rust-logger.patch",
    "chromium-alos-skia-no-rust-bridge.patch",
    "chromium-alos-blink-rust-crash-gate.patch",
    "chromium-alos-platform-gates.patch",
    "chromium-alos-rust-target.patch"
)) {
    $patch = Join-Path $repo ("ports\chromium\patches\" + $patchName)
    & git -C $src apply --reverse --check $patch 2>$null
    if ($LASTEXITCODE -ne 0) {
        & git -C $src apply --check $patch 2>$null
        if ($LASTEXITCODE) { throw "Patch $patchName incompatible avec le checkout Chromium." }
        & git -C $src apply $patch
        if ($LASTEXITCODE) { throw "Application du patch $patchName echouee." }
    }
    foreach ($dependency in @(
        @{ Path = "third_party\perfetto"; Patch = "perfetto-alos-thread-id.patch"; Strip = 1 },
        @{ Path = "third_party\perfetto"; Patch = "perfetto-alos-unsupported-regex.patch"; Strip = 1 },
        @{ Path = "third_party\perfetto"; Patch = "perfetto-alos-optional-signals.patch"; Strip = 3 },
        @{ Path = "third_party\perfetto"; Patch = "perfetto-alos-native-shared-memory.patch"; Strip = 1 },
        @{ Path = "third_party\icu"; Patch = "icu-alos-no-decimal-signals.patch"; Strip = 3 },
        @{ Path = "third_party\dawn"; Patch = "dawn-alos-common.patch"; Strip = 1 },
        @{ Path = "third_party\skia"; Patch = "skia-alos-features.patch"; Strip = 1 }
    )) {
        $directory = Join-Path $src $dependency.Path
        $patch = Join-Path $repo ("ports\chromium\patches\" + $dependency.Patch)
        $strip = "-p$($dependency.Strip)"
        & git -C $directory apply $strip --reverse --check $patch 2>$null
        if ($LASTEXITCODE -ne 0) {
            & git -C $directory apply $strip --check $patch 2>$null
            if ($LASTEXITCODE) { throw "Patch $($dependency.Patch) incompatible avec $directory." }
            & git -C $directory apply $strip $patch
            if ($LASTEXITCODE) { throw "Application du patch $($dependency.Patch) echouee." }
        }
    }
}
foreach ($library in @("libc++.a", "libc++abi.a")) {
    if (-not (Test-Path (Join-Path $libcxx "lib\$library"))) {
        throw "Bibliotheque ALOS absente : $library (voir build-libcxx.ps1)"
    }
    foreach ($name in @("message_pump_alos.h", "message_pump_alos.cc")) {
        $template = Join-Path $repo "ports\chromium\patches\templates\base\message_loop\$name"
        $destination = Join-Path $src "base\message_loop\$name"
        if (Test-Path -LiteralPath $destination) {
            $expected = [IO.File]::ReadAllText($template).Replace("`r`n", "`n")
            $actual = [IO.File]::ReadAllText($destination).Replace("`r`n", "`n")
            if ($actual -ne $expected) { throw "Source ALOS locale divergente : $destination" }
        } else {
            Copy-Item -LiteralPath $template -Destination $destination
        }
        foreach ($name in @("process_alos.cc", "launch_alos.cc", "kill_alos.cc")) {
            $template = Join-Path $repo "ports\chromium\patches\templates\base\process\$name"
            $destination = Join-Path $src "base\process\$name"
            if (Test-Path -LiteralPath $destination) {
                $expected = [IO.File]::ReadAllText($template).Replace("`r`n", "`n")
                $actual = [IO.File]::ReadAllText($destination).Replace("`r`n", "`n")
                if ($actual -ne $expected) { throw "Source processus ALOS divergente : $destination" }
            } else {
                Copy-Item -LiteralPath $template -Destination $destination
            }
        }
    }
}
$threadTemplate = Join-Path $repo "ports\chromium\patches\templates\base\threading\platform_thread_alos.cc"
$threadDestination = Join-Path $src "base\threading\platform_thread_alos.cc"
if (Test-Path -LiteralPath $threadDestination) {
    if ([IO.File]::ReadAllText($threadDestination).Replace("`r`n", "`n") -ne
        [IO.File]::ReadAllText($threadTemplate).Replace("`r`n", "`n")) {
        throw "Source thread ALOS divergente : $threadDestination"
    }
} else {
    Copy-Item -LiteralPath $threadTemplate -Destination $threadDestination
}
$args_gn = @(
    'target_os = "alos"', 'target_cpu = "x64"', 'is_debug = false',
    'is_component_build = false', 'use_sysroot = false',
    "enable_rust = " + $(if ($rustEnabled) { "true" } else { "false" }),
    "enable_rust_cxx = false",
    'enable_chromium_prelude = false', 'clang_use_chrome_plugins = false',
    'use_fuzztest_wrapper = false', 'use_llvm_libatomic = false',
    '# Pas d''interposition malloc : la libc ALOS reste l''allocateur global.',
    '# PartitionAlloc natif reste compile, sans les profils BRP/compression',
    '# qui dependent de son installation comme allocateur global.',
    'use_allocator_shim = false', 'use_partition_alloc = true',
    'use_partition_alloc_as_malloc = false',
    'enable_backup_ref_ptr_support = false',
    'enable_pointer_compression_support = false',
    '# Concerne uniquement la libc++ in-tree de Chromium : la cible ALOS',
    '# utilise la libc++ LLVM 21 bd809ffb ALOS du toolchain //build/toolchain/alos.',
    'use_custom_libcxx = false', 'use_custom_libcxx_for_host = false',
    '# Pas de Crashpad sous ALOS : configuration amont sans rapport de crash.',
    'use_crash_key_stubs = true'
) -join "`n"
if ($NativeTests) {
    $testDirectory = Join-Path $src "alos_native"
    New-Item -ItemType Directory -Force $testDirectory | Out-Null
    foreach ($name in @("BUILD.gn", "base-smoke.cc", "nativeatomic-runtime-test.cc",
        "nativefile-comparison-test.cc", "base-stack-trace-smoke.cc", "base-io-pump-smoke.cc",
        "base-process-smoke.cc", "base-discardable-capability-smoke.cc",
        "base-thread-smoke.cc", "base-elf-reader-smoke.cc",
        "ozone-interface-probe.cc")) {
        Copy-Item -LiteralPath (Join-Path $repo "ports\chromium\tests\$name") -Destination $testDirectory
    }
    if ($OzoneProbe) {
        $ozoneSource = Join-Path $repo "ports\chromium\ozone"
        $ozoneDestination = Join-Path $testDirectory "ozone"
        if (Test-Path -LiteralPath $ozoneDestination) {
            Remove-Item -LiteralPath $ozoneDestination -Recurse -Force
        }
        Copy-Item -LiteralPath $ozoneSource -Destination $ozoneDestination -Recurse
        $ozoneConfigDestination = Join-Path $src "alos_ozone"
        if (Test-Path -LiteralPath $ozoneConfigDestination) {
            Remove-Item -LiteralPath $ozoneConfigDestination -Recurse -Force
        }
        New-Item -ItemType Directory -Force $ozoneConfigDestination | Out-Null
        Copy-Item -LiteralPath (Join-Path $ozoneSource "ozone_extra.gni") `
            -Destination $ozoneConfigDestination
    }
    $args_gn += "`nalos_build_native_tests = true"
}
if ($OzoneProbe) {
    $args_gn += "`nuse_ozone = true"
    $args_gn += "`nalos_build_ozone_probe = true"
    $args_gn += "`nozone_auto_platforms = false"
    $args_gn += "`nozone_extra_path = `"//alos_native/ozone/ozone_extra.gni`""
    $args_gn += "`nozone_platform = `"alos`""
    # Aucun pilote Vulkan sous ALOS : pas de chargeur libvulkan partage.
    $args_gn += "`nangle_shared_libvulkan = false"
    # Les outils hote Linux sont construits sans sysroot ; ALOS n'utilise
    # pas NSS, donc aucune toolchain ne doit exiger nss.pc.
    $args_gn += "`nuse_nss_certs = false"
    # Profil content_shell : pas de plateforme d'extensions Chrome.
    $args_gn += "`nenable_extensions = false"
    # Aucune pile d'impression ALOS ; l'outil hote ne doit pas exiger CUPS.
    $args_gn += "`nenable_printing = false"
    $args_gn += "`nuse_cups = false"
    $args_gn += "`nenable_pdf = false"
    $args_gn += "`nenable_av1_decoder = false"
    $args_gn += "`nv8_enable_temporal_support = false"
    $args_gn += "`nmedia_use_symphonia = false"
    $args_gn += "`nskia_use_fontconfig = false"
    # Bibliotheques systeme Linux absentes d'ALOS ; ces arguments sont
    # partages avec la toolchain hote, construite sans sysroot.
    foreach ($systemLibrary in @("use_xkbcommon", "use_glib", "use_udev",
            "use_dbus", "use_gio", "use_pangocairo", "use_gtk", "use_alsa",
            "use_pulseaudio", "use_vaapi", "rtc_use_pipewire", "use_qt")) {
        $args_gn += "`n$systemLibrary = false"
    }
    if ($Targets.Count -eq 1 -and $Targets[0] -eq "base:base") {
        $Targets = @("alos_native/ozone:ozone_interface_probe")
    }
}
if ($Rust) {
    $rustDirectory = Join-Path $src "alos_rust"
    New-Item -ItemType Directory -Force $rustDirectory | Out-Null
    foreach ($name in @("BUILD.gn", "rust-smoke.cc", "rust-smoke.rs")) {
        Copy-Item -LiteralPath (Join-Path $repo "ports\chromium\tests\rust\$name") `
            -Destination $rustDirectory
    }
}
if ($rustEnabled) {
    $args_gn += "`nremoved_rust_stdlib_libs = [ `"test`" ]"
}
if ($MojoTests) {
        $mojoPatch = Join-Path $repo "ports\chromium\patches\chromium-alos-mojo-unnamed-platform.patch"
        & git -C $src apply --reverse --check $mojoPatch 2>$null
        if ($LASTEXITCODE) {
            & git -C $src apply --check $mojoPatch
            if ($LASTEXITCODE) { throw "Patch Mojo ALOS incompatible." }
            & git -C $src apply $mojoPatch
            if ($LASTEXITCODE) { throw "Patch Mojo ALOS echoue." }
        }
        $source = Join-Path $repo "ports\chromium\patches\templates\mojo\named_platform_channel_alos.cc"
        $destination = Join-Path $src "mojo\public\cpp\platform\named_platform_channel_alos.cc"
        if ((Test-Path $destination) -and
            [IO.File]::ReadAllText($destination).Replace("`r`n", "`n") -ne
            [IO.File]::ReadAllText($source).Replace("`r`n", "`n")) {
            throw "Backend Mojo ALOS local divergent."
        }
        Copy-Item -LiteralPath $source -Destination $destination
        foreach ($entry in @(
            @{ Source = "mojo_buildflags.gni"; Destination = "mojo\public\cpp\bindings\mojo_buildflags.gni" },
            @{ Source = "buildflags.gn"; Destination = "mojo\public\cpp\bindings\buildflags\BUILD.gn" }
        )) {
            $source = Join-Path $repo "ports\chromium\patches\templates\mojo\$($entry.Source)"
            $destination = Join-Path $src $entry.Destination
            if ((Test-Path $destination) -and
                [IO.File]::ReadAllText($destination).Replace("`r`n", "`n") -ne
                [IO.File]::ReadAllText($source).Replace("`r`n", "`n")) {
                throw "Metadata Mojo locale divergente : $destination"
            }
            New-Item -ItemType Directory -Force (Split-Path $destination) | Out-Null
            Copy-Item -LiteralPath $source -Destination $destination
        }
        $directory = Join-Path $src "alos_mojo"
        New-Item -ItemType Directory -Force $directory | Out-Null
        foreach ($name in @("BUILD.gn", "mojo-ipcz-smoke.cc",
            "echo.mojom", "mojo-bindings-smoke.cc")) {
            Copy-Item -LiteralPath (Join-Path $repo "ports\chromium\tests\mojo\$name") `
                -Destination $directory
        }
        if (-not $MojoBindings) { $args_gn += "`nalos_build_mojo_tests = true" }
        $args_gn += "`nuse_blink = false"
        if ($MojoBindings) {
            $patch = Join-Path $repo "ports\chromium\patches\chromium-alos-mojom-cpp-only.patch"
            & git -C $src apply --reverse --check $patch 2>$null
            if ($LASTEXITCODE) {
                & git -C $src apply --check $patch
                if ($LASTEXITCODE) { throw "Patch bindings C++ incompatible." }
                & git -C $src apply $patch
                if ($LASTEXITCODE) { throw "Patch bindings C++ echoue." }
            }
            $args_gn += "`nalos_mojom_cpp_only = true"
            $args_gn += "`nalos_build_mojo_bindings = true"
        }
    }
if ($V8Probe) {
    $v8 = Join-Path $src "v8"
    if ((git -C $v8 rev-parse HEAD 2>$null) -ne "fdb12b460f148895f6af2ff0e0d870ff8889f154") {
        throw "V8 14.0.365.4 absent : relancer fetch-chromium.ps1 -WithV8."
    }
    foreach ($v8Patch in @("v8-alos-os-detection.patch", "v8-alos-no-signals.patch", "v8-alos-platform.patch")) {
        $patch = Join-Path $repo "ports\chromium\patches\$v8Patch"
        & git -C $v8 apply --reverse --check $patch 2>$null
        if ($LASTEXITCODE) {
            & git -C $v8 apply --check $patch
            if ($LASTEXITCODE) { throw "Patch V8 $v8Patch incompatible." }
            & git -C $v8 apply $patch
            if ($LASTEXITCODE) { throw "Patch V8 $v8Patch echoue." }
        }
    }
    # Temporal depend de crates Rust ; Rust est absent du profil ALOS.
    $args_gn += "`nv8_enable_temporal_support = false"
}
New-Item -ItemType Directory -Force (Join-Path $src $outDirectory) | Out-Null
[IO.File]::WriteAllText((Join-Path $src "$outDirectory/args.gn"), "$args_gn`n")
$mounts = @("run", "--rm", "-v", "${src}:/chromium", "-v", "${gn}:/gn:ro",
            "-v", "${libcxx}:/libcxx:ro", "-v", "${repo}:/alos:ro",
            "-w", "/chromium", "alos-runtime")
$generate = @("/gn/gn", "gen", $outDirectory)
if ($MojoBindings) {
    $generate += "--root-target=//alos_mojo:mojo_bindings_smoke"
}
if ($V8Probe) { $generate += "--root-target=//v8:v8_libbase" }
if ($OzoneProbe) { $generate += "--root-target=//alos_native/ozone:ozone_interface_probe" }
if ($Rust) { $generate += "--root-target=//alos_rust:rust_smoke" }
& docker @mounts @generate
if ($LASTEXITCODE) { throw "gn gen $outDirectory echoue." }
if ($GenOnly) { return }
$ninja = @("ninja", "-C", $outDirectory, "-j", "$Jobs")
if ($KeepGoing) { $ninja += @("-k", "0") }
& docker @mounts @ninja @Targets
if ($LASTEXITCODE) { throw "ninja $Targets echoue." }
