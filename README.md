# Union-Find: implementation and empirical study

Programming Assignment 1, Track A. The spec is in [`docs/assignment.md`](docs/assignment.md).

A header-only C++17 disjoint-set forest where the **linking rule** (naive / by rank / by size)
and the **path rule** (none / full compression / halving) are chosen at compile time. There is also a
**quick-find** baseline. A benchmark harness compares all 10 variants on generated workloads.

## Layout

| Path | Contents |
|---|---|
| `include/uf/union_find.hpp` | `UnionFind<Link, Path>`, the main data structure |
| `include/uf/quick_find.hpp` | Quick-find baseline |
| `include/uf/variants.hpp` | The list of variants that tests and benchmarks iterate over |
| `tests/test_union_find.cpp` | Unit, randomised-oracle, and invariant tests |
| `bench/bench_main.cpp` | Benchmark harness, writes CSV to `results/` |
| `demo/demo_main.cpp` | Prints the parent array step by step, for learning and the video |
| `scripts/plot.py` | Reads `results/*.csv`, writes `report/figures/*.png` |
| `report/report.docx` | Written report (Word template) |
| `PLAN.md` | Task plan and current status |
| `journal.md`, `ai_log.md` | Learning journal and AI usage log |

## Requirements

- CMake 3.16 or later, and a C++17 compiler (GCC, Clang, or MSVC)
- Python 3 with `pandas` and `matplotlib` (`pip install -r requirements.txt`)

## Build and test (Debug, with sanitizers)

```sh
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug
ctest --test-dir build-debug --output-on-failure
```

GCC/Clang use ASan and UBSan. MSVC only supports ASan.

**Windows (MSVC):** first put the **64-bit** VS 2022 toolchain on PATH. The Start-menu
"Developer PowerShell" defaults to 32-bit, which fails to link (LNK4272, x86 vs x64):

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64
```

Then add `-G Ninja` to the configure commands. If a configure has already failed, delete the build
folder and reconfigure: CMake caches the compiler path. MSVC ignores `-march=native`; its Release flags are `/O2`.
If you use a multi-config generator (Visual Studio), add `--config Debug` to the build and ctest commands.


## Demo: watch the forest change

```powershell
.\build-debug\demo.exe                                                  # built-in script: a chain, then finds
.\build-debug\demo.exe u 0 1 u 2 3 u 0 2 u 4 5 u 6 7 u 4 6 u 0 4 f 7    # binomial tree: rank 3, depth 3
```

Prints the parent array (and rank or size) after every operation for six variants, on 8 elements.
`u a b` is unite(a, b) and `f x` is find(x). Run it from the 64-bit developer shell: the Debug build
needs the AddressSanitizer DLL, which is only on PATH there.

## Benchmark (Release: `-O2 -march=native`)

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
./build-release/bench --out results/bench.csv --reps 5 --min-log 10 --max-log 20
python scripts/plot.py
```

Options: `--counts-out results/counts.csv` (extra untimed run collecting path length and write counts), `--reps`, `--min-log`/`--max-log` (n goes from 2^min to 2^max), `--quadratic-max-log`
(cap on n for quick-find and naive-no-compression, which can be quadratic), `--seed`.

Methodology notes:
- Operation sequences and the union-find object are built before the timer starts. Only the operation loop is timed.
- Each variant produces an order-sensitive checksum of its answers. If two variants disagree, the run aborts.
