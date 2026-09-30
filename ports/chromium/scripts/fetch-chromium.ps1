param(
    [Parameter(Mandatory)][string]$ChromiumDirectory,
    [switch]$SkipPatch
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
    "crypto", "mojo", "third_party/ipcz",
    "testing", "tools/clang", "tools/gn", "tools/grit", "tools/gritsettings", "tools/protoc_wrapper",
    "third_party/abseil-cpp", "third_party/boringssl", "third_party/ced",
    "third_party/closure_compiler", "third_party/fuzztest",
    "third_party/google_benchmark", "third_party/googletest",
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
    @{ Path = "third_party/icu"; Url = "https://chromium.googlesource.com/chromium/deps/icu.git";
       Rev = "1b2e3e8a421efae36141a7b932b41e315b089af8"; Sparse = @("source", "common") },
    @{ Path = "third_party/perfetto"; Url = "https://chromium.googlesource.com/external/github.com/google/perfetto.git";
       Rev = "4ab725613a8ee64e9acd7930eceb8995e24df562";
       Sparse = @("gn", "include", "protos", "python", "src", "tools") },
    @{ Path = "third_party/jsoncpp/source"; Url = "https://chromium.googlesource.com/external/github.com/open-source-parsers/jsoncpp.git";
       Rev = "42e892d96e47b1f6e29844cc705e148ec4856448"; Sparse = $null },
    @{ Path = "third_party/protobuf-javascript/src"; Url = "https://chromium.googlesource.com/external/github.com/protocolbuffers/protobuf-javascript";
       Rev = "28bf5df73ef2f345a936d9cc95d64ba8ed426a53"; Sparse = $null }
)

function Invoke-Git { & git @args; if ($LASTEXITCODE) { throw "git $args" } }

if (-not (Test-Path (Join-Path $ChromiumDirectory ".git"))) {
    Invoke-Git clone --depth 1 --filter=blob:none --sparse --branch $tag `
        https://github.com/chromium/chromium.git $ChromiumDirectory
}
$src = (Resolve-Path $ChromiumDirectory).Path
if ($src.StartsWith($repo, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Le checkout Chromium doit rester externe au depot ALOS."
}
if ((git -C $src rev-parse HEAD) -ne $commit) { throw "Chromium $tag ($commit) requis." }
Invoke-Git -C $src sparse-checkout set @sparse

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

# Fichiers normalement produits par gclient runhooks.
$gclientArgs = "# Generated for the isolated ALOS bootstrap checkout.`nbuild_with_chromium = true`ncheckout_src_internal = false`n"
[IO.File]::WriteAllText((Join-Path $src "build\config\gclient_args.gni"), $gclientArgs)
$commitTime = git -C $src log -1 --format=%ct
[IO.File]::WriteAllText((Join-Path $src "build\util\LASTCHANGE.committime"), "$commitTime")

# Clang epingle par Chromium (tools/clang/scripts/update.py), execute sur
# l'hote Linux du conteneur. Il compile aussi bien les outils host que les
# objets ALOS (--target=x86_64-unknown-none-elf) ; libc/libc++ restent ALOS.
& docker run --rm -v "${src}:/chromium" -w /chromium alos-runtime python3 tools/clang/scripts/update.py
if ($LASTEXITCODE) { throw "Telechargement du clang Chromium echoue." }

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
