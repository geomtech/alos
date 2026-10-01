param(
    [Parameter(Mandatory)][string]$LibcxxBuildDirectory,
    [string]$ClangDirectory
)
$ErrorActionPreference = "Stop"
$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
$build = (Resolve-Path $LibcxxBuildDirectory).Path
$clangMount = @()
$compiler = "clang++"
if ($ClangDirectory) {
    $clang = (Resolve-Path $ClangDirectory).Path
    $clangMount = @("-v", "${clang}:/chromium-clang:ro")
    $compiler = "/chromium-clang/bin/clang++"
}
foreach ($library in @("libc++.a", "libc++abi.a")) {
    if (-not (Test-Path (Join-Path $build "lib\$library"))) {
        throw "Bibliotheque absente : $library"
    }
}
& docker run --rm -v "${repo}:/alos" -v "${build}:/libcxx:ro" @clangMount `
    -w /alos/src/userland alos-runtime sh -c `
    "make -s -C libc && make -s -C libm && $compiler --target=x86_64-unknown-none-elf -std=c++20 -fhosted -fno-builtin -O2 -m64 -fno-pic -fno-pie -fno-stack-protector -fno-exceptions -fno-rtti -ftls-model=local-exec -nostdinc++ -I/libcxx/include/c++/v1 -Ilibc/include -c complex-cpp-test.cc -o complex-cpp-test.o && ld -T linker.ld -o complex-cpp-test libc/crt0.o complex-cpp-test.o --start-group /libcxx/lib/libc++.a /libcxx/lib/libc++abi.a libc/libc.a libm/libm.a --end-group"
if ($LASTEXITCODE) { throw "Build complex-cpp-test echoue." }
Write-Host "complex-cpp-test compile et lie pour ALOS"
