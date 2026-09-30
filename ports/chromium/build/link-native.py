"""Translate GN's compiler-driver link flags to pinned native ELF LLD flags."""
import subprocess
import sys


def main():
    if len(sys.argv) < 3:
        raise SystemExit("usage: link-native.py <pinned-ld.lld> <link arguments>")
    linker, arguments = sys.argv[1], sys.argv[2:]
    if "-nostdlib" not in arguments or "-static" not in arguments:
        raise SystemExit("ALOS requires an explicit no-host-runtime static link")
    output = ["-m", "elf_x86_64"]
    for argument in arguments:
        if argument.startswith("-Wl,"):
            output.extend(argument[4:].split(","))
        elif argument in ("-nostdlib", "-no-canonical-prefixes", "-Werror"):
            continue
        elif argument.startswith("--target="):
            if argument != "--target=x86_64-unknown-none-elf":
                raise SystemExit("Unexpected target in native ALOS link")
        elif argument.startswith("-fuse-ld="):
            if argument != "-fuse-ld=lld":
                raise SystemExit("ALOS requires its pinned LLD")
        elif argument.startswith("-W"):
            raise SystemExit("Unrecognized compiler-driver link flag: " + argument)
        else:
            output.append(argument)
    raise SystemExit(subprocess.call([linker, *output]))


if __name__ == "__main__":
    main()
