param(
    [Parameter(Mandatory)][string]$LLVMDirectory,
    [string]$OutputDirectory = (Join-Path $LLVMDirectory "..\alos-libcxx-build"),
    [string]$InstallDirectory = (Join-Path $LLVMDirectory "..\alos-libcxx"),
    [string]$ExpectedRevision = "bd809ffb4b5f277a661509fbbbf9ea893a545ab0",
    [string]$PatchPrefix = "llvm-21-bd809ffb",
    [string]$ClangDirectory,
    [switch]$DisableFilesystem
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
$source = (Resolve-Path $LLVMDirectory).Path
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$output = (Resolve-Path $OutputDirectory).Path
New-Item -ItemType Directory -Force -Path $InstallDirectory | Out-Null
$install = (Resolve-Path $InstallDirectory).Path
if ($source -eq $repo -or $output -eq $repo -or $install -eq $repo -or
    $source.StartsWith($repo + "\", [StringComparison]::OrdinalIgnoreCase) -or
    $output.StartsWith($repo + "\", [StringComparison]::OrdinalIgnoreCase) -or
    $install.StartsWith($repo + "\", [StringComparison]::OrdinalIgnoreCase)) {
    throw "Les sources LLVM, les outputs et l'installation doivent rester externes au depot ALOS."
}
$clangMount = @()
$compilerOptions = @()
if ($ClangDirectory) {
    $clang = (Resolve-Path $ClangDirectory).Path
    $clangMount = @("-v", "${clang}:/chromium-clang:ro")
    $compilerOptions = @("-DALOS_LLVM_BINDIR=/chromium-clang/bin")
}
$mounts = @("run", "--rm", "-v", "${repo}:/alos", "-v", "${source}:/llvm:ro",
            "-v", "${output}:/build", "-v", "${install}:/install") + $clangMount + @("alos-runtime")
$revision = & docker @mounts git -c safe.directory=/llvm -C /llvm rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $revision.Trim() -ne $ExpectedRevision) {
    throw "LLVM revision $ExpectedRevision est requise."
}
$writableMounts = @("run", "--rm", "-v", "${repo}:/alos:ro",
                    "-v", "${source}:/llvm", "-v", "${output}:/build") + $clangMount + @("alos-runtime")
foreach ($patchName in @("$PatchPrefix-alos-clock.patch", "$PatchPrefix-musl-narrow-locale.patch")) {
    if (-not (Test-Path (Join-Path $repo "ports\chromium\patches\$patchName"))) { continue }
    & docker @writableMounts sh -c "sed 's/\r`$//' /alos/ports/chromium/patches/$patchName > /build/current-libcxx.patch"
    if ($LASTEXITCODE) { throw "Preparation du patch ALOS echouee : $patchName." }
    $patch = "/build/current-libcxx.patch"
    & docker @writableMounts git -c safe.directory=/llvm -C /llvm apply --unidiff-zero --reverse --check $patch 2>$null
    if ($LASTEXITCODE -eq 0) { continue }
    & docker @writableMounts git -c safe.directory=/llvm -C /llvm apply --unidiff-zero --check $patch 2>$null
    if ($LASTEXITCODE -eq 0) {
        & docker @writableMounts git -c safe.directory=/llvm -C /llvm apply --unidiff-zero $patch
        if ($LASTEXITCODE) { throw "Application du patch ALOS echouee : $patchName." }
    } else {
        & docker @writableMounts git -c safe.directory=/llvm -C /llvm apply --unidiff-zero --reverse --check $patch 2>$null
        if ($LASTEXITCODE) { throw "Patch ALOS incompatible avec les sources LLVM : $patchName." }
    }
}
$filesystem = if ($DisableFilesystem) { "OFF" } else { "ON" }
$options = @(
    "-G", "Ninja", "-S", "/llvm/runtimes", "-B", "/build",
    "-DCMAKE_TOOLCHAIN_FILE=/alos/ports/chromium/build/alos-toolchain.cmake",
    "-DALOS_SOURCE_DIR=/alos", "-DCMAKE_BUILD_TYPE=Debug",
    "-DCMAKE_INSTALL_PREFIX=/install",
    "-DLLVM_ENABLE_RUNTIMES=libcxx;libcxxabi", "-DLLVM_INCLUDE_TESTS=OFF",
    "-DLIBCXX_INCLUDE_TESTS=OFF", "-DLIBCXX_INCLUDE_BENCHMARKS=OFF",
    "-DLIBCXXABI_INCLUDE_TESTS=OFF", "-DLIBCXX_ENABLE_SHARED=OFF",
    "-DLIBCXXABI_ENABLE_SHARED=OFF", "-DLIBCXX_SHARED_OUTPUT_NAME=c++_shared",
    "-DLIBCXXABI_SHARED_OUTPUT_NAME=c++abi_shared", "-DLIBCXX_ENABLE_EXCEPTIONS=OFF",
    "-DLIBCXX_ENABLE_THREADS=ON", "-DLIBCXXABI_ENABLE_THREADS=ON",
    "-DLIBCXXABI_ENABLE_EXCEPTIONS=OFF", "-DLIBCXX_ENABLE_RTTI=OFF",
    "-DLIBCXXABI_ENABLE_RTTI=OFF", "-DLIBCXX_ENABLE_FILESYSTEM=$filesystem",
    "-DLIBCXX_ENABLE_LOCALIZATION=ON", "-DLIBCXX_ENABLE_WIDE_CHARACTERS=ON",
    "-DLIBCXX_ENABLE_TIME_ZONE_DATABASE=OFF", "-DLIBCXX_ENABLE_RANDOM_DEVICE=OFF",
    "-DLIBCXX_ENABLE_UNICODE=ON", "-DLIBCXX_HAS_MUSL_LIBC=ON", "-DLIBCXX_HAS_PTHREAD_API=ON",
    "-DLIBCXXABI_HAS_PTHREAD_API=ON", "-DLIBCXXABI_USE_LLVM_UNWINDER=OFF",
    "-DLIBCXX_ENABLE_ABI_LINKER_SCRIPT=OFF", "-DLIBCXXABI_BAREMETAL=ON",
    "-DLIBCXX_HAS_PTHREAD_LIB=OFF", "-DLIBCXXABI_HAS_PTHREAD_LIB=OFF",
    "-DLIBCXX_HAS_RT_LIB=OFF", "-DLIBCXX_HAS_ATOMIC_LIB=OFF",
    "-DLIBCXX_HAS_GCC_S_LIB=OFF", "-DLIBCXXABI_HAS_GCC_S_LIB=OFF",
    "-DLIBCXXABI_HAS_DL_LIB=OFF"
) + $compilerOptions
& docker @mounts cmake @options *> (Join-Path $output "configure.log")
if ($LASTEXITCODE) { throw "Configuration libc++ echouee : $output\configure.log" }
& docker @mounts cmake --build /build --target cxx cxxabi cxx_experimental -- -j4 `
    *> (Join-Path $output "build.log")
if ($LASTEXITCODE) { throw "Build libc++ echoue : $output\build.log" }
& docker @mounts cmake --install /build *> (Join-Path $output "install.log")
if ($LASTEXITCODE) { throw "Installation libc++ echouee : $output\install.log" }
foreach ($library in @("libc++.a", "libc++abi.a")) {
    if (-not (Test-Path (Join-Path $install "lib\$library"))) {
        throw "Bibliotheque installee absente : $install\lib\$library"
    }
}
if (-not (Test-Path (Join-Path $install "include\c++\v1\fstream"))) {
    throw "En-tete fstream absent de l'installation libc++."
}
Write-Host "libc++ et libc++abi cibles installes : $install"
