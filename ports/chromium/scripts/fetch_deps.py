#!/usr/bin/env python3
"""Recupere les dependances git de DEPS (Chromium 140) applicables a Linux.

S'execute DANS un conteneur (ou sur Linux) contre un checkout chromium/src :
    python3 fetch_deps.py /chromium [--only third_party/search_engines_data/resources ...]
Ne supprime rien ; idempotent (une dependance deja a la bonne revision est
ignoree). Clone partiel + profondeur 1 comme fetch-chromium.ps1. Ignore les
paquets CIPD/GCS (autres dep_type) et les dependances dont la condition exclut
Linux (android, ios, win, mac, chromeos, fuchsia).
"""
import argparse
import concurrent.futures
import os
import subprocess
import sys

HOST = {
    "checkout_linux": True, "checkout_android": False, "checkout_ios": False,
    "checkout_win": False, "checkout_mac": False, "checkout_chromeos": False,
    "checkout_fuchsia": False, "checkout_arm": False, "checkout_arm64": False,
    "checkout_x86": False, "checkout_x64": True, "checkout_mips": False,
    "checkout_mips64": False, "checkout_ppc": False, "checkout_s390": False,
    "checkout_riscv64": False, "checkout_loong64": False,
    "host_os": "linux", "host_cpu": "x64", "checkout_nacl": False,
    "checkout_openxr": True, "checkout_google_benchmark": False,
    "checkout_pgo_profiles": False, "checkout_src_internal": False,
    "checkout_mutter": False, "checkout_clang_coverage_tools": False,
    "checkout_copybara": False, "checkout_clusterfuzz_data": False,
    "checkout_chromium_password_manager_test_dependencies": False,
    "checkout_chromium_autofill_test_dependencies": False,
    "checkout_js_coverage_modules": False, "checkout_glic_e2e_tests": False,
    "checkout_telemetry_dependencies": False, "checkout_oculus_sdk": False,
    "checkout_traffic_annotation_tools": False, "checkout_vs_toolchain": False,
    "checkout_configuration": "default", "non_git_source": False,
    "checkout_reclient": False, "checkout_rust_toolchain_win_x64": False,
    "checkout_rust_toolchain_win_arm64": False, "checkout_clang_tidy": False,
    "checkout_src_internal_infra": False, "checkout_legacy_src_internal": False,
    "checkout_ios_webkit": False, "checkout_android_prebuilts_build_tools": False,
}


class Env(dict):
    def __missing__(self, key):
        return False


def load_deps(src):
    namespace = {}

    class Str(str):
        pass

    def var(name):
        return namespace["vars"][name]

    namespace.update({"Var": var, "Str": Str, "deps": {}, "vars": {}})
    with open(os.path.join(src, "DEPS"), encoding="utf-8") as stream:
        exec(compile(stream.read(), "DEPS", "exec"), namespace)
    return namespace


def wanted(entry):
    if isinstance(entry, str):
        return True
    if entry.get("dep_type", "git") != "git":
        return False
    condition = entry.get("condition")
    if not condition:
        return True
    try:
        return bool(eval(condition, {"__builtins__": {}}, Env(HOST)))
    except Exception:
        return False


def fetch(src, path, url, rev):
    target = os.path.join(src, path)
    git = ["git", "-c", "core.longpaths=true"]
    if os.path.isdir(os.path.join(target, ".git")):
        head = subprocess.run(git + ["-C", target, "rev-parse", "HEAD"],
                              capture_output=True, text=True).stdout.strip()
        if head == rev:
            return path, "ok (deja a la revision)"
    else:
        if os.path.isdir(target) and os.listdir(target):
            # Repertoire existant (fichiers du depot principal) : on y clone
            # seulement s'il ne contient que des fichiers de suivi (BUILD.gn...).
            pass
        os.makedirs(target, exist_ok=True)
        subprocess.run(git + ["init", "-q", target], check=True)
        subprocess.run(git + ["-C", target, "remote", "add", "origin", url], check=True)
        subprocess.run(git + ["-C", target, "config", "remote.origin.promisor", "true"], check=True)
        subprocess.run(git + ["-C", target, "config", "remote.origin.partialclonefilter", "blob:none"], check=True)
    result = subprocess.run(git + ["-C", target, "fetch", "-q", "--depth", "1",
                                   "--filter=blob:none", "origin", rev],
                            capture_output=True, text=True)
    if result.returncode:
        return path, "ECHEC fetch: " + result.stderr.strip()[-200:]
    result = subprocess.run(git + ["-C", target, "checkout", "-q", "-f", rev],
                            capture_output=True, text=True)
    if result.returncode:
        return path, "ECHEC checkout: " + result.stderr.strip()[-200:]
    return path, "ok"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("src")
    parser.add_argument("--only", nargs="*", default=[],
                        help="chemins relatifs (sans 'src/') a restreindre")
    parser.add_argument("--exclude", nargs="*", default=[],
                        help="prefixes de chemins a ignorer")
    parser.add_argument("--jobs", type=int, default=8)
    args = parser.parse_args()
    namespace = load_deps(args.src)
    jobs = []
    for key, entry in namespace["deps"].items():
        if not key.startswith("src/") or not wanted(entry):
            continue
        path = key[len("src/"):]
        if args.only and not any(path == o or path.startswith(o + "/") for o in args.only):
            continue
        if any(path == x or path.startswith(x + "/") for x in args.exclude):
            continue
        url = entry if isinstance(entry, str) else entry.get("url")
        if not url or "@" not in url:
            continue
        try:
            url = url.format(**namespace["vars"])  # '{chromium_git}/...'
        except (KeyError, IndexError):
            pass
        url, rev = url.rsplit("@", 1)
        jobs.append((path, url, rev))
    print(f"{len(jobs)} dependances git applicables", flush=True)
    failures = 0
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        futures = [pool.submit(fetch, args.src, *job) for job in jobs]
        for future in concurrent.futures.as_completed(futures):
            path, status = future.result()
            print(f"{status:30s} {path}", flush=True)
            failures += status.startswith("ECHEC")
    print(f"termine : {failures} echec(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
