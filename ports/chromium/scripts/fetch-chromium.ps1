param(
    [Parameter(Mandatory)][string]$ChromiumDirectory,
    [switch]$SkipPatch,
    [switch]$FullCheckout,
    [switch]$WithV8
)
# Checkout Chromium 140.0.7339.80 reproductible pour le bootstrap ALOS :
# clone sparse de chromium/src puis dependances DEPS aux revisions figees.
# Idempotent : un checkout existant est seulement complete.
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
$tag = "140.0.7339.80"
$commit = "670b6f192f4668d2ac2c06bd77ec3e4eeda7d648"

# Repertoires de chromium/src necessaires au graphe GN de //base et a ses
# outils host (generateurs Python, protoc, GRIT...).
$sparse = @(
    "base", "build", "build_overrides", "buildtools", "chrome/enterprise_companion",
    "content", "crypto", "device", "gpu", "media", "mojo", "net", "services",
    "skia", "third_party/ipcz", "ui",
    "testing", "tools/clang", "tools/gn", "tools/grit", "tools/gritsettings", "tools/nocompile", "tools/protoc_wrapper",
    "third_party/abseil-cpp", "third_party/boringssl", "third_party/ced",
    "third_party/closure_compiler", "third_party/fuzztest",
    "third_party/google_benchmark", "third_party/google-closure-library", "third_party/googletest",
    "third_party/jsoncpp", "third_party/libxml", "third_party/modp_b64",
    "third_party/partition_alloc", "third_party/protobuf",
    "third_party/protobuf-javascript", "third_party/re2", "third_party/sqlite",
    "third_party/zlib"
)

# Entrees de DEPS (chemin, URL, revision, sparse ou $null = complet).
$deps = @(
    @{ Path = "third_party/googletest/src"; Url = "https://chromium.googlesource.com/external/github.com/google/googletest.git";
       Rev = "373af2e3df71599b87a40ce0e37164523849166b"; Sparse = $null },
    @{ Path = "third_party/boringssl/src"; Url = "https://boringssl.googlesource.com/boringssl.git";
       Rev = "0a0009998fa180695f3e2071805dc03c9a5f3124"; Sparse = $null },
    @{ Path = "third_party/ced/src"; Url = "https://chromium.googlesource.com/external/github.com/google/compact_enc_det.git";
       Rev = "ba412eaaacd3186085babcd901679a48863c7dd5"; Sparse = $null },
    @{ Path = "third_party/google_benchmark/src"; Url = "https://chromium.googlesource.com/external/github.com/google/benchmark.git";
       Rev = "761305ec3b33abf30e08d50eb829e19a802581cc"; Sparse = $null },
    @{ Path = "third_party/fuzztest/src"; Url = "https://chromium.googlesource.com/external/github.com/google/fuzztest.git";
       Rev = "7bab06ff5fbbf8b8cce05a8661369dc2e11cde66"; Sparse = $null },
    @{ Path = "third_party/re2/src"; Url = "https://chromium.googlesource.com/external/github.com/google/re2.git";
       Rev = "8451125897dd7816a5c118925e8e42309d598ecc"; Sparse = $null },
    @{ Path = "third_party/sqlite/src"; Url = "https://chromium.googlesource.com/chromium/deps/sqlite.git";
       Rev = "cc08c79629643fdd5b592f1391e738815f5577b6"; Sparse = $null },
    @{ Path = "third_party/icu"; Url = "https://chromium.googlesource.com/chromium/deps/icu.git";
       Rev = "1b2e3e8a421efae36141a7b932b41e315b089af8"; Sparse = @("source", "common") },
    @{ Path = "third_party/perfetto"; Url = "https://chromium.googlesource.com/external/github.com/google/perfetto.git";
       Rev = "4ab725613a8ee64e9acd7930eceb8995e24df562";
       Sparse = @("gn", "include", "protos", "python", "src", "tools") },
    @{ Path = "third_party/jsoncpp/source"; Url = "https://chromium.googlesource.com/external/github.com/open-source-parsers/jsoncpp.git";
       Rev = "42e892d96e47b1f6e29844cc705e148ec4856448"; Sparse = $null },
    @{ Path = "third_party/protobuf-javascript/src"; Url = "https://chromium.googlesource.com/external/github.com/protocolbuffers/protobuf-javascript";
       Rev = "28bf5df73ef2f345a936d9cc95d64ba8ed426a53"; Sparse = $null }
    @{ Path = "third_party/zstd/src"; Url = "https://chromium.googlesource.com/external/github.com/facebook/zstd.git";
       Rev = "f9938c217da17ec3e9dcd2a2d99c5cf39536aeb9"; Sparse = $null },
    @{ Path = "third_party/expat/src"; Url = "https://chromium.googlesource.com/external/github.com/libexpat/libexpat.git";
       Rev = "69d6c054c1bd5258c2a13405a7f5628c72c177c2"; Sparse = $null },
    @{ Path = "third_party/libwebp/src"; Url = "https://chromium.googlesource.com/webm/libwebp.git";
       Rev = "4fa21912338357f89e4fd51cf2368325b59e9bd9"; Sparse = $null },
    @{ Path = "third_party/snappy/src"; Url = "https://chromium.googlesource.com/external/github.com/google/snappy.git";
       Rev = "32ded457c0b1fe78ceb8397632c416568d6714a0"; Sparse = $null },
    @{ Path = "third_party/harfbuzz-ng/src"; Url = "https://chromium.googlesource.com/external/github.com/harfbuzz/harfbuzz.git";
       Rev = "9f83bbbe64654b45ba5bb06927ff36c2e7588495"; Sparse = $null },
    @{ Path = "third_party/openxr/src"; Url = "https://chromium.googlesource.com/external/github.com/KhronosGroup/OpenXR-SDK.git";
       Rev = "57572a0e91890fe7183de25a62153aec955d64ba"; Sparse = @("include", "src") },
    @{ Path = "third_party/skia"; Url = "https://skia.googlesource.com/skia.git";
       Rev = "1fdbea293a53b270e3f5e74c92cc6670d68412ff";
       Sparse = @("gn", "include", "modules", "src", "tools", "third_party") },
    @{ Path = "third_party/angle"; Url = "https://chromium.googlesource.com/angle/angle.git";
       Rev = "cbc4153da8d5796b0fbb3cf288e97bee19436191";
       Sparse = @("gni", "include", "src", "util", "third_party") },
    @{ Path = "third_party/dawn"; Url = "https://dawn.googlesource.com/dawn.git";
       Rev = "8550f9c1ff8859d25cc49bdfacef083cff2c5121";
       Sparse = @("generator", "scripts", "include", "src", "third_party", "tools", "webgpu-cts") },
    @{ Path = "third_party/webrtc"; Url = "https://webrtc.googlesource.com/src.git";
       Rev = "847fe7905954f3ae883de2936415ff567aa9039b";
       Sparse = @("api", "rtc_base", "common_audio", "common_video", "media", "modules", "pc", "p2p", "call", "logging",
                 "audio", "experiments", "net", "rtc_tools", "stats", "system_wrappers", "test", "video") },
    # Charges par le graphe GN Ozone/content (gn gen evalue tous les BUILD.gn
    # references, y compris testonly).
    @{ Path = "third_party/ffmpeg"; Url = "https://chromium.googlesource.com/chromium/third_party/ffmpeg.git";
       Rev = "d2d06b12c22d27af58114e779270521074ff1f85"; Sparse = $null },
    @{ Path = "net/third_party/quiche/src"; Url = "https://quiche.googlesource.com/quiche.git";
       Rev = "42832178b3b6ae20f0d1c9634c040c528614f45f"; Sparse = $null },
    @{ Path = "third_party/vulkan-headers/src"; Url = "https://chromium.googlesource.com/external/github.com/KhronosGroup/Vulkan-Headers";
       Rev = "a01329f307fa6067da824de9f587f292d761680b"; Sparse = $null },
    @{ Path = "third_party/devtools-frontend/src"; Url = "https://chromium.googlesource.com/devtools/devtools-frontend";
       Rev = "ab96665ae2cfcc054e0243461cfcb56bb016f71a"; Sparse = $null },
    @{ Path = "third_party/libyuv"; Url = "https://chromium.googlesource.com/libyuv/libyuv.git";
       Rev = "cdd3bae84818e78466fec1ce954eead8f403d10c"; Sparse = $null },
    @{ Path = "third_party/swiftshader"; Url = "https://swiftshader.googlesource.com/SwiftShader.git";
       Rev = "fdb6700ecb04103b658d2e4623d6bc663ba80ea8"; Sparse = $null },
    @{ Path = "third_party/nasm"; Url = "https://chromium.googlesource.com/chromium/deps/nasm.git";
       Rev = "e2c93c34982b286b27ce8b56dd7159e0b90869a2"; Sparse = $null },
    @{ Path = "third_party/libjpeg_turbo"; Url = "https://chromium.googlesource.com/chromium/deps/libjpeg_turbo.git";
       Rev = "e14cbfaa85529d47f9f55b0f104a579c1061f9ad"; Sparse = $null },
    @{ Path = "third_party/highway/src"; Url = "https://chromium.googlesource.com/external/github.com/google/highway.git";
       Rev = "00fe003dac355b979f36157f9407c7c46448958e"; Sparse = $null },
    # Graphe GN complet du probe Ozone/Blink avec Rust (gn-loop).
    @{ Path = "third_party/fp16/src"; Url = "https://chromium.googlesource.com/external/github.com/Maratyszcza/FP16.git";
       Rev = "0a92994d729ff76a58f692d3028ca1b64b145d91"; Sparse = $null },
    # Conversion double -> chaine de V8 (v8/src/numbers/conversions.cc).
    @{ Path = "third_party/dragonbox/src"; Url = "https://chromium.googlesource.com/external/github.com/jk-jeon/dragonbox.git";
       Rev = "6c7c925b571d54486b9ffae8d9d18a822801cbda"; Sparse = $null },
    @{ Path = "third_party/cast_core/public/src"; Url = "https://chromium.googlesource.com/cast_core/public";
       Rev = "f5ee589bdaea60418f670fa176be15ccb9a34942"; Sparse = $null },
    @{ Path = "third_party/catapult"; Url = "https://chromium.googlesource.com/catapult.git";
       Rev = "0fd1415f0cf3219ba097d37336141897fab7c5e9"; Sparse = $null },
    @{ Path = "third_party/cld_3/src"; Url = "https://chromium.googlesource.com/external/github.com/google/cld_3.git";
       Rev = "b48dc46512566f5a2d41118c8c1116c4f96dc661"; Sparse = $null },
    @{ Path = "third_party/flac"; Url = "https://chromium.googlesource.com/chromium/deps/flac.git";
       Rev = "689da3a7ed50af7448c3f1961d1791c7c1d9c85c"; Sparse = $null },
    @{ Path = "third_party/glslang/src"; Url = "https://chromium.googlesource.com/external/github.com/KhronosGroup/glslang";
       Rev = "38f6708b6b6f213010c51ffa8f577a7751e12ce7"; Sparse = $null },
    @{ Path = "third_party/libsrtp"; Url = "https://chromium.googlesource.com/chromium/deps/libsrtp.git";
       Rev = "a52756acb1c5e133089c798736dd171567df11f5"; Sparse = $null },
    @{ Path = "third_party/openscreen/src"; Url = "https://chromium.googlesource.com/openscreen";
       Rev = "f51be2dd676c855bc588a439f002bc941b87db6b"; Sparse = $null },
    @{ Path = "third_party/pdfium"; Url = "https://pdfium.googlesource.com/pdfium.git";
       Rev = "1afaa1a380fcd06cec420f3e5b6ec1d2ccb920dc"; Sparse = $null },
    @{ Path = "third_party/spirv-headers/src"; Url = "https://chromium.googlesource.com/external/github.com/KhronosGroup/SPIRV-Headers";
       Rev = "97e96f9e9defeb4bba3cfbd034dec516671dd7a3"; Sparse = $null },
    @{ Path = "third_party/spirv-tools/src"; Url = "https://chromium.googlesource.com/external/github.com/KhronosGroup/SPIRV-Tools";
       Rev = "3aeaaa088d37b86cff036eee1a9bf452abad7d9d"; Sparse = $null },
    @{ Path = "third_party/vulkan_memory_allocator"; Url = "https://chromium.googlesource.com/external/github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git";
       Rev = "cb0597213b0fcb999caa9ed08c2f88dc45eb7d50"; Sparse = $null },
    @{ Path = "third_party/vulkan-loader/src"; Url = "https://chromium.googlesource.com/external/github.com/KhronosGroup/Vulkan-Loader";
       Rev = "f2389e27734347c1d9f40e03be53f69f969976b1"; Sparse = $null },
    @{ Path = "third_party/vulkan-tools/src"; Url = "https://chromium.googlesource.com/external/github.com/KhronosGroup/Vulkan-Tools";
       Rev = "f766b30b2de3ffe2cf6b656d943720882617ec58"; Sparse = $null },
    @{ Path = "third_party/vulkan-utility-libraries/src"; Url = "https://chromium.googlesource.com/external/github.com/KhronosGroup/Vulkan-Utility-Libraries";
       Rev = "b0a40d2e50310e9f84327061290a390a061125a3"; Sparse = $null },
    @{ Path = "third_party/vulkan-validation-layers/src"; Url = "https://chromium.googlesource.com/external/github.com/KhronosGroup/Vulkan-ValidationLayers";
       Rev = "6b1b8e3d259241a68c0944ca0a7bb5320d086191"; Sparse = $null }
)

function Invoke-Git { & git @args; if ($LASTEXITCODE) { throw "git $args" } }

if (-not (Test-Path (Join-Path $ChromiumDirectory ".git"))) {
    Invoke-Git clone -c core.longpaths=true --depth 1 --filter=blob:none --sparse --branch $tag `
        https://github.com/chromium/chromium.git $ChromiumDirectory
}
$src = (Resolve-Path $ChromiumDirectory).Path
if ($src.StartsWith($repo, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Le checkout Chromium doit rester externe au depot ALOS."
}
if ((git -C $src rev-parse HEAD) -ne $commit) { throw "Chromium $tag ($commit) requis." }
# Git for Windows doit pouvoir materialiser les chemins longs Chromium.
# Configuration locale uniquement, avant toute expansion du checkout.
Invoke-Git -C $src config core.longpaths true
if ($FullCheckout) {
    # Le build "all" traverse des sources/outils vendored bien au-dela du
    # bootstrap //base. Desactiver le sparse checkout evite de courir apres
    # chaque dossier du depot principal manquant.
    Invoke-Git -C $src sparse-checkout disable
    Write-Host "Checkout Chromium principal complet active."
} else {
    Invoke-Git -C $src sparse-checkout set @sparse
}

foreach ($dep in $deps) {
    $dir = Join-Path $src $dep.Path
    if (-not (Test-Path (Join-Path $dir ".git"))) {
        if (Test-Path $dir) {
            if (Get-ChildItem -Force $dir) { throw "$dir existe sans depot git." }
            Remove-Item $dir
        }
        Invoke-Git init -q $dir
        Invoke-Git -C $dir remote add origin $dep.Url
        Invoke-Git -C $dir config remote.origin.promisor true
        Invoke-Git -C $dir config remote.origin.partialclonefilter blob:none
    }
    if ($dep.Sparse) {
        Invoke-Git -C $dir sparse-checkout set @($dep.Sparse)
    } elseif ((git -C $dir config core.sparseCheckout) -eq "true") {
        Invoke-Git -C $dir sparse-checkout disable
    }
    if ((git -C $dir rev-parse HEAD 2>$null) -ne $dep.Rev) {
        Invoke-Git -C $dir fetch -q --depth 1 --filter=blob:none origin $dep.Rev
        Invoke-Git -C $dir checkout -q $dep.Rev
    }
}

if ($WithV8) {
    # V8 14.0.365.4 (DEPS v8_revision) pour la sonde -V8Probe. Sparse non-cone :
    # GN charge les BUILD.gn de test/ depuis //v8:gn_all sans leurs donnees.
    $v8 = @{ Path = "v8"; Url = "https://chromium.googlesource.com/v8/v8.git";
             Rev = "fdb12b460f148895f6af2ff0e0d870ff8889f154" }
    $dir = Join-Path $src $v8.Path
    if (-not (Test-Path (Join-Path $dir ".git"))) {
        if (Test-Path $dir) {
            if (Get-ChildItem -Force $dir) { throw "$dir existe sans depot git." }
            Remove-Item $dir
        }
        Invoke-Git init -q $dir
        Invoke-Git -C $dir remote add origin $v8.Url
        Invoke-Git -C $dir config remote.origin.promisor true
        Invoke-Git -C $dir config remote.origin.partialclonefilter blob:none
        Invoke-Git -C $dir config core.longpaths true
    }
    Invoke-Git -C $dir sparse-checkout set --no-cone "/*" "!/*/" "/include/" "/src/" "/gni/" `
        "/infra/" "/tools/" "/samples/" "/testing/" "/third_party/*.gn" `
        "/third_party/inspector_protocol/" "/third_party/v8/" "/third_party/siphash/" `
        "/third_party/utf8-decoder/" "/third_party/valgrind/" "/third_party/glibc/" `
        "/third_party/rapidhash-v8/" "/third_party/fp16/" "/test/**/BUILD.gn" "/test/**/*.gni" "/test/torque/"
    if ((git -C $dir rev-parse HEAD 2>$null) -ne $v8.Rev) {
        Invoke-Git -C $dir fetch -q --depth 1 --filter=blob:none origin $v8.Rev
        Invoke-Git -C $dir checkout -q $v8.Rev
    }
}

# Fichiers normalement produits par gclient runhooks.
$gclientArgs = "# Generated for the isolated ALOS bootstrap checkout.`nbuild_with_chromium = true`ncheckout_src_internal = false`ncheckout_openxr = true`n"
[IO.File]::WriteAllText((Join-Path $src "build\config\gclient_args.gni"), $gclientArgs)
$commitTime = git -C $src log -1 --format=%ct
[IO.File]::WriteAllText((Join-Path $src "build\util\LASTCHANGE.committime"), "$commitTime")

# Clang epingle par Chromium (tools/clang/scripts/update.py), execute sur
# l'hote Linux du conteneur. Il compile aussi bien les outils host que les
# objets ALOS (--target=x86_64-unknown-none-elf) ; libc/libc++ restent ALOS.
& docker run --rm -v "${src}:/chromium" -w /chromium alos-runtime python3 tools/clang/scripts/update.py
if ($LASTEXITCODE) { throw "Telechargement du clang Chromium echoue." }

# Rust epingle par Chromium. C'est un artefact GCS de DEPS, pas un chemin git :
# le sparse checkout ne peut pas le materialiser.
$rustToolchain = Join-Path $src "third_party\rust-toolchain"
$rustVersion = Join-Path $rustToolchain "VERSION"
if (-not (Test-Path -LiteralPath $rustVersion)) {
    $rustUrl = "https://storage.googleapis.com/chromium-browser-clang/Linux_x64/rust-toolchain-22be76b7e259f27bf3e55eb931f354cd8b69d55f-3-llvmorg-21-init-16348-gbd809ffb.tar.xz"
    $rustSha256 = "5f8e9ad847e5bf586e0de1bb563c9a49e05ad36edfad5037900d7510004fc577"
    $rustArchive = Join-Path (Split-Path $src -Parent) "rust-toolchain-linux.tar.xz"
    if (-not (Test-Path -LiteralPath $rustArchive)) {
        & curl.exe -L -o $rustArchive $rustUrl
        if ($LASTEXITCODE) { throw "Telechargement du toolchain Rust Chromium echoue." }
    }
    $actualSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $rustArchive).Hash.ToLowerInvariant()
    if ($actualSha256 -ne $rustSha256) {
        throw "Archive Rust Chromium invalide : sha256 $actualSha256"
    }
    New-Item -ItemType Directory -Force $rustToolchain | Out-Null
    & tar -xf $rustArchive -C $rustToolchain
    if ($LASTEXITCODE) { throw "Extraction du toolchain Rust Chromium echouee." }
    if (-not (Test-Path -LiteralPath $rustVersion)) {
        throw "Toolchain Rust Chromium extrait sans VERSION."
    }
}

# Artefact GCS DEPS requis par les templates WebUI traverses par le graphe
# Ozone/content. Le tarball est conserve pres du checkout, jamais dans /tmp.
$nodeModules = Join-Path $src "third_party\node\node_modules"
if (-not (Test-Path -LiteralPath (Join-Path $nodeModules "lit-html"))) {
    $nodeArchive = Join-Path $src "third_party\node\node_modules.tar.gz"
    if (-not (Test-Path -LiteralPath $nodeArchive)) {
        & curl.exe -L -o $nodeArchive `
            "https://storage.googleapis.com/chromium-nodejs/c2085eeb07e1db47d75633a23311b7d2b7b5d1ee"
        if ($LASTEXITCODE) { throw "Telechargement des node_modules Chromium echoue." }
    }
    $actualSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $nodeArchive).Hash.ToLowerInvariant()
    if ($actualSha256 -ne "24f08d6ea505a468362b862591bdebdb8ba5f6b3da73b435fcc85bd1e38cfbca") {
        throw "Archive node_modules Chromium invalide : sha256 $actualSha256"
    }
    if (Test-Path -LiteralPath $nodeModules) {
        Remove-Item -LiteralPath $nodeModules -Recurse -Force
    }
    New-Item -ItemType Directory -Force $nodeModules | Out-Null
    & tar -xf $nodeArchive -C $nodeModules
    if ($LASTEXITCODE) { throw "Extraction des node_modules Chromium echouee." }
}

if (-not $SkipPatch) {
    $patch = Join-Path $repo "ports\chromium\patches\chromium-$tag-alos-bootstrap.patch"
    & git -C $src apply --check $patch 2>$null
    if ($LASTEXITCODE -eq 0) {
        Invoke-Git -C $src apply $patch
    } else {
        & git -C $src apply --reverse --check $patch 2>$null
        $reverseExitCode = $LASTEXITCODE
        if ($reverseExitCode) {
            # Un checkout deja bootstrappe peut ensuite recevoir d'autres patches
            # ALOS qui modifient les memes fichiers. Dans ce cas le reverse-check
            # du patch initial n'est plus fiable : verifier les marqueurs
            # fonctionnels du bootstrap avant de conclure a une incompatibilite.
            $buildConfig = Get-Content -Raw (Join-Path $src "build\build_config.h")
            $gnConfig = Get-Content -Raw (Join-Path $src "build\config\BUILDCONFIG.gn")
            $toolchain = Join-Path $src "build\toolchain\alos\BUILD.gn"
            $bootstrapPresent =
                $buildConfig.Contains("#define OS_ALOS 1") -and
                $buildConfig.Contains("defined(OS_ALOS)") -and
                $gnConfig.Contains('target_os == "alos"') -and
                $gnConfig.Contains('current_os == "alos"') -and
                (Test-Path -LiteralPath $toolchain)
            if (-not $bootstrapPresent) {
                throw "Patch ALOS incompatible avec le checkout."
            }
            Write-Host "Bootstrap ALOS deja present dans le checkout enrichi."
        } else {
            Write-Host "Patch ALOS deja applique."
        }
    }
}
Write-Host "Checkout Chromium $tag pret : $src"
