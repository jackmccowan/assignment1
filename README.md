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

**Windows (MSVC):** run these commands from a *Developer PowerShell for VS 2022* (Start menu), which puts `cl`, `cmake`, and `ninja` on PATH. Add `-G Ninja` to the configure commands for single-config builds. MSVC ignores `-march=native`; its Release flags are `/O2`.
If you use a multi-config generator (Visual Studio), add `--config Debug` to the build and ctest commands.

## Benchmark (Release: `-O2 -march=native`)

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
./build-release/bench --out results/bench.csv --reps 5 --min-log 10 --max-log 20
python scripts/plot.py
```

Options: `--reps`, `--min-log`/`--max-log` (n goes from 2^min to 2^max), `--quadratic-max-log`
(cap on n for quick-find and naive-no-compression, which can be quadratic), `--seed`.

Methodology notes:
- Operation sequences and the union-find object are built before the timer starts. Only the operation loop is timed.
- Each variant produces an order-sensitive checksum of its answers. If two variants disagree, the run aborts.
