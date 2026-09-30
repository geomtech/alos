param(
    [Parameter(Mandatory)][string]$LLVMDirectory,
    [string]$OutputDirectory = (Join-Path $LLVMDirectory "..\alos-libcxx-build")
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
$source = (Resolve-Path $LLVMDirectory).Path
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$output = (Resolve-Path $OutputDirectory).Path
if ($source -eq $repo -or $output -eq $repo -or
    $source.StartsWith($repo + "\", [StringComparison]::OrdinalIgnoreCase) -or
    $output.StartsWith($repo + "\", [StringComparison]::OrdinalIgnoreCase)) {
    throw "Les sources LLVM et les outputs doivent rester externes au depot ALOS."
}
$mounts = @("run", "--rm", "-v", "${repo}:/alos", "-v", "${source}:/llvm:ro",
            "-v", "${output}:/build", "alos-runtime")
$revision = & docker @mounts git -c safe.directory=/llvm -C /llvm rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $revision.Trim() -ne "3b5b5c1ec4a3095ab096dd780e84d7ab81f3d7ff") {
    throw "LLVM llvmorg-18.1.8 (3b5b5c1ec4a3095ab096dd780e84d7ab81f3d7ff) est requis."
}
$writableMounts = @("run", "--rm", "-v", "${repo}:/alos:ro",
                    "-v", "${source}:/llvm", "-v", "${output}:/build",
                    "alos-runtime")
foreach ($patchName in @("llvm-18.1.8-alos-clock.patch", "llvm-18.1.8-musl-narrow-locale.patch")) {
& docker @writableMounts sh -c "sed 's/\r`$//' /alos/ports/chromium/patches/$patchName > /build/alos-clock.patch"
if ($LASTEXITCODE) { throw "Preparation du patch horloges ALOS echouee." }
$patch = "/build/alos-clock.patch"
& docker @writableMounts git -c safe.directory=/llvm -C /llvm apply --unidiff-zero --reverse --check $patch 2>$null
if ($LASTEXITCODE -eq 0) { continue }
& docker @writableMounts git -c safe.directory=/llvm -C /llvm apply --unidiff-zero --check $patch 2>$null
if ($LASTEXITCODE -eq 0) {
    & docker @writableMounts git -c safe.directory=/llvm -C /llvm apply --unidiff-zero $patch
    if ($LASTEXITCODE) { throw "Application du patch horloges ALOS echouee." }
} else {
    & docker @writableMounts git -c safe.directory=/llvm -C /llvm apply --unidiff-zero --reverse --check $patch 2>$null
    if ($LASTEXITCODE) { throw "Patch horloges ALOS incompatible avec les sources LLVM." }
}
}
$options = @(
    "-G", "Ninja", "-S", "/llvm/runtimes", "-B", "/build",
    "-DCMAKE_TOOLCHAIN_FILE=/alos/ports/chromium/build/alos-toolchain.cmake",
    "-DALOS_SOURCE_DIR=/alos", "-DCMAKE_BUILD_TYPE=Debug",
    "-DLLVM_ENABLE_RUNTIMES=libcxx;libcxxabi", "-DLLVM_INCLUDE_TESTS=OFF",
    "-DLIBCXX_INCLUDE_TESTS=OFF", "-DLIBCXX_INCLUDE_BENCHMARKS=OFF",
    "-DLIBCXXABI_INCLUDE_TESTS=OFF", "-DLIBCXX_ENABLE_SHARED=OFF",
    "-DLIBCXXABI_ENABLE_SHARED=OFF", "-DLIBCXX_ENABLE_EXCEPTIONS=OFF",
    "-DLIBCXX_ENABLE_THREADS=ON", "-DLIBCXXABI_ENABLE_THREADS=ON",
    "-DLIBCXXABI_ENABLE_EXCEPTIONS=OFF", "-DLIBCXX_ENABLE_RTTI=OFF",
    "-DLIBCXXABI_ENABLE_RTTI=OFF", "-DLIBCXX_ENABLE_FILESYSTEM=OFF",
    "-DLIBCXX_ENABLE_LOCALIZATION=ON", "-DLIBCXX_ENABLE_WIDE_CHARACTERS=ON",
    "-DLIBCXX_ENABLE_TIME_ZONE_DATABASE=OFF", "-DLIBCXX_ENABLE_RANDOM_DEVICE=OFF",
    "-DLIBCXX_ENABLE_UNICODE=ON", "-DLIBCXX_HAS_MUSL_LIBC=ON", "-DLIBCXX_HAS_PTHREAD_API=ON",
    "-DLIBCXXABI_HAS_PTHREAD_API=ON", "-DLIBCXXABI_USE_LLVM_UNWINDER=OFF",
    "-DLIBCXX_ENABLE_ABI_LINKER_SCRIPT=OFF", "-DLIBCXXABI_BAREMETAL=ON",
    "-DLIBCXX_HAS_PTHREAD_LIB=OFF", "-DLIBCXXABI_HAS_PTHREAD_LIB=OFF",
    "-DLIBCXX_HAS_RT_LIB=OFF", "-DLIBCXX_HAS_ATOMIC_LIB=OFF",
    "-DLIBCXX_HAS_GCC_S_LIB=OFF", "-DLIBCXXABI_HAS_GCC_S_LIB=OFF",
    "-DLIBCXXABI_HAS_DL_LIB=OFF"
)
& docker @mounts cmake @options *> (Join-Path $output "configure.log")
if ($LASTEXITCODE) { throw "Configuration libc++ echouee : $output\configure.log" }
& docker @mounts cmake --build /build --target cxx cxxabi -- -j4 `
    *> (Join-Path $output "build.log")
if ($LASTEXITCODE) { throw "Build libc++ echoue : $output\build.log" }
Write-Host "libc++ et libc++abi cibles compiles : $output"
