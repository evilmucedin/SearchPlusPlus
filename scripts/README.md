# Developer setup scripts

One-shot scripts to install dependencies and build SearchPlusPlus from a clean
machine. They mirror what GitHub Actions does in `.github/workflows/ci.yml` so
local "works on my box" results match CI.

## Linux and macOS

```bash
scripts/install.sh       # install compiler, cmake, ninja, clang-format, vcpkg
scripts/build.sh         # configure + build + test (default preset)
```

`install.sh` auto-detects the platform from `/etc/os-release` and uses:

| Distro family                     | Package manager |
|-----------------------------------|-----------------|
| Ubuntu / Debian                   | `apt-get`       |
| Fedora / RHEL / Rocky / Alma      | `dnf` (or `yum`) |
| Arch / Manjaro                    | `pacman`        |
| openSUSE                          | `zypper`        |
| macOS                             | `brew` (+ Xcode CLT) |

vcpkg is cloned to `$VCPKG_ROOT` (default `$HOME/vcpkg`) and bootstrapped. Add
the two `export` lines the script prints to your shell rc file so future
sessions pick it up.

### Flags

- `install.sh --no-vcpkg` — skip the vcpkg clone (use a pre-existing checkout).
- `install.sh --vcpkg-root <dir>` — clone vcpkg somewhere other than `$HOME/vcpkg`.
- `build.sh --preset <name>` — choose a CMake preset (default: `default`).
  Other options: `release`, `asan`.
- `build.sh --no-test` — configure + build only.
- `build.sh -j <N>` — cap build parallelism.
- `build.sh -- <args>` — forward remaining args to `cmake --build` (e.g.
  `-- --target spp_core`).

## Windows

```powershell
# From a regular PowerShell prompt (admin not required for winget/choco):
powershell -ExecutionPolicy Bypass -File scripts\install.ps1

# Then open "Developer PowerShell for VS 2022" from the Start Menu:
powershell -ExecutionPolicy Bypass -File scripts\build.ps1
```

`install.ps1` requires Visual Studio 2022 with the **"Desktop development with
C++" workload** installed (the script does *not* install MSVC because the
download is large and its EULA must be accepted interactively). If the workload
is missing, the script prints the download URL and exits.

CMake, Ninja, and Git are installed via **winget** (preferred) with a Chocolatey
fallback. vcpkg is cloned to `$env:VCPKG_ROOT` (default
`$env:USERPROFILE\vcpkg`) and bootstrapped.

`build.ps1` must be run from a shell where `cl.exe` is on `PATH` — the easiest
way is the "Developer PowerShell for VS 2022" shortcut in the Start Menu. The
script will refuse to run otherwise and tells you what to do.

### Flags

- `install.ps1 -NoVcpkg` — skip the vcpkg clone.
- `install.ps1 -VcpkgRoot <dir>` — install vcpkg to a custom path.
- `install.ps1 -PackageManager winget|choco|auto` — force a package manager.
- `build.ps1 -Preset release` — choose a CMake preset (default:
  `default-windows`).
- `build.ps1 -NoTest` — configure + build only.
- `build.ps1 -Jobs <N>` — cap build parallelism.

## What these scripts do *not* do

- They do not install the CatBoost Python package needed for the LTR training
  workflow — see `docs/ltr_training.md` for that.
- They do not configure clang-tidy. Run `clang-tidy -p build/default <file>`
  directly as documented in `CLAUDE.md`.
- They do not run benchmarks. Build with `--preset release` and invoke the
  binaries under `build/release/bench/` directly (see CI's
  `benchmarks-smoke` job for the expected invocation).

## Workload scripts (not part of the install/build flow)

- `train_ltr.py` / `demo_train_ltr.sh` — LTR pipeline. See
  `docs/ltr_training.md`.
- `bench_search.py` / `demo_bench_search.sh` — search-throughput load test
  against a running `spp_serve`. Reports max searches/minute (both the
  per-window peak and the sustained mean) plus latency percentiles. Run
  `./scripts/demo_bench_search.sh` for the smoke test, or point
  `bench_search.py` at your own running instance:

  ```bash
  scripts/bench_search.py --url http://127.0.0.1:9200 \
      --index wiki --duration 60 --concurrency 32
  ```
