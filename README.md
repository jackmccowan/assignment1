# Union-Find: implementation and empirical study

Programming Assignment 1, Track A. The spec is in [`docs/assignment.md`](docs/assignment.md).

A header-only C++17 disjoint-set forest where the **linking rule** (naive / by rank / by size)
and the **path rule** (none / full compression / halving) are chosen at compile time. There is also a
**quick-find** baseline. The application is **maze generation by randomised Kruskal**. A benchmark
harness compares all 10 variants on mazes and on synthetic workloads.

## Layout

| Path | Contents |
|---|---|
| `include/uf/union_find.hpp` | `UnionFind<Link, Path>`, the main data structure |
| `include/uf/quick_find.hpp` | Quick-find baseline |
| `include/uf/variants.hpp` | The list of variants that tests and benchmarks iterate over |
| `include/uf/maze.hpp` | Maze generation by randomised Kruskal (`grid_walls`, `carve<UF>`) |
| `tests/test_union_find.cpp` | Unit, randomised-oracle, and invariant tests |
| `tests/test_maze.cpp` | Checks every variant carves a spanning tree, and all carve the same maze |
| `tests/check.hpp` | The `CHECK` macro shared by the test programs |
| `bench/bench_main.cpp` | Benchmark harness, writes CSV to `results/` |
| `demo/demo_main.cpp` | Prints the parent array step by step, or draws a maze |
| `scripts/plot.py` | Reads `results/bench*.csv`, writes `report/figures/*.png` |
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
.\build-debug\demo.exe maze 12 8 1                                      # draw a 12x8 maze, seed 1
```

The first two print the parent array (and rank or size) after every operation for six variants, on
8 elements. `u a b` is unite(a, b) and `f x` is find(x). `maze [w h [seed]]` generates a maze with
union by rank plus compression and draws it in ASCII. Run the Debug build from the 64-bit developer
shell: it needs the AddressSanitizer DLL, which is only on PATH there.

## Benchmark (Release: `-O2 -march=native`)

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
./build-release/bench --out results/bench.csv --reps 5 --min-log 10 --max-log 20
python scripts/plot.py
```

Options:

| Option | Meaning |
|---|---|
| `--counts-out FILE` | Extra untimed run per case, collecting path length and write counts |
| `--reps K` | Samples per point (default 5) |
| `--min-log A`, `--max-log B` | n goes from 2^A to 2^B |
| `--quadratic-max-log C` | Cap on n for quick-find and naive-no-compression, which can be quadratic (default 14) |
| `--seed S` | Seed for the generated workloads |
| `--min-time-ms T` | Each sample repeats the run until its timed total is at least T ms (default 5) |
| `--cpu N` / `--no-pin` | Pin to CPU N / don't pin. By default, Windows picks a P-core automatically. |

Workloads (`n` = number of elements, a power of two):

| Workload | Operations |
|---|---|
| `maze` | Randomised Kruskal on a w x h grid (w*h = n): one unite per wall, in shuffled order |
| `maze_scrambled` | The same maze with cells randomly relabelled, so neighbours are far apart in memory |
| `random_mixed` | n random unions interleaved with n random connectivity queries |
| `chain` | unite(i, i+1) for all i, then n queries from element 0 (worst case for naive linking) |

Methodology notes:
- Operation sequences and the union-find object are built before the timer starts. Only the operation loop is timed.
- Noise control: the benchmark is pinned to one CPU at raised priority, each variant gets a discarded
  warm-up run, and each sample sums `inner` runs (a CSV column) so that it lasts at least
  `--min-time-ms`. This took the median run-to-run spread (IQR / median, 5 reps, n = 2^10 to 2^12)
  from 16% to 1%. `plot.py` draws the IQR as error bars and writes it to `report/figures/spread.csv`.
- Each variant produces an order-sensitive checksum of its answers. If two variants disagree, the run aborts.
- `std::shuffle` and the `<random>` distributions are implementation-defined, so a given `--seed`
  produces the same operations on the same compiler, but not across MSVC, GCC and Clang.
