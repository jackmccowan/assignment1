// Benchmark harness: times every variant on pre-generated operation sequences
// and writes one CSV row per (workload, variant, n, repetition).
//
// Rules this file follows (see CLAUDE.md):
//  * All random numbers and operation sequences are generated BEFORE timing.
//  * The union-find object is constructed BEFORE timing (allocation not timed).
//  * CSV output happens AFTER timing.
//  * Every variant must produce the same checksum on the same ops; a mismatch
//    means a correctness bug, and the harness aborts.
//  * With --counts-out, each union-find variant is also run ONCE more, untimed,
//    as its counting twin (UnionFind<L, P, true>), and the counters go to a
//    separate CSV. Its checksum must match the timed run's.
//
// Noise control:
//  * The process is pinned to one CPU (a P-core on hybrid Windows machines)
//    at raised priority; see platform.hpp.
//  * Each variant gets one discarded warm-up run, which also calibrates how
//    many runs ("inner") one sample needs to last at least --min-time-ms.
//  * A sample = `inner` runs; each run constructs a fresh object untimed and
//    times only apply_ops, and the timed parts are summed. So short runs at
//    small n are no longer dominated by timer granularity and OS interruptions.
//
// Usage: bench [--out results/bench.csv] [--counts-out results/counts.csv]
//              [--reps 5] [--min-log 10] [--max-log 20]
//              [--quadratic-max-log 14] [--seed 12345]
//              [--min-time-ms 5] [--cpu N | --no-pin]

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <numeric>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "platform.hpp"
#include "uf/maze.hpp"
#include "uf/variants.hpp"

using index_t = std::uint32_t;
using Clock = std::chrono::steady_clock;

enum class OpKind : std::uint8_t { Unite, Connected };

struct Op {
    OpKind kind;
    index_t a;
    index_t b;
};

// ---------------------------------------------------------------- workloads

// n random unions interleaved with n random connectivity queries.
static std::vector<Op> make_random_mixed(std::size_t n, std::mt19937_64& rng) {
    std::uniform_int_distribution<index_t> pick(0, static_cast<index_t>(n - 1));
    std::vector<Op> ops;
    ops.reserve(2 * n);
    for (std::size_t i = 0; i < n; ++i) {
        ops.push_back({OpKind::Unite, pick(rng), pick(rng)});
        ops.push_back({OpKind::Connected, pick(rng), pick(rng)});
    }
    return ops;
}

// unite(i, i+1) for all i, then n queries from element 0. With naive linking
// this builds a path of length n-1, so every query from 0 walks the whole path
// unless path compression/halving shortens it.
static std::vector<Op> make_chain(std::size_t n, std::mt19937_64& rng) {
    std::uniform_int_distribution<index_t> pick(0, static_cast<index_t>(n - 1));
    std::vector<Op> ops;
    ops.reserve(2 * n);
    for (index_t i = 0; i + 1 < n; ++i) ops.push_back({OpKind::Unite, i, i + 1});
    for (std::size_t i = 0; i < n; ++i) ops.push_back({OpKind::Connected, 0, pick(rng)});
    return ops;
}

// Maze generation by randomised Kruskal (see uf/maze.hpp) on a w x h grid
// with w * h == n: one unite per interior wall, in shuffled order.
// w and h are powers of two, so w * h is exactly n for n = 2^lg.
//
// With scramble == false, cell (x, y) is element y * w + x, so the two cells
// of a wall are 1 or w apart in memory. With scramble == true, cells get a
// random relabelling first: the maze and the sequence of unite results are
// identical, but neighbouring cells are scattered across memory. Comparing
// the two isolates the effect of memory locality.
//
// Unlike maze::carve there is no early exit: every wall is processed (plain
// Kruskal over all edges). Walls examined after the maze is complete are
// failed unites, which still cost two finds each.
static std::vector<Op> make_maze_ops(std::size_t n, std::mt19937_64& rng, bool scramble) {
    std::size_t w = 1;
    while (w * w < n) w *= 2;  // w = 2^ceil(lg/2)
    const std::size_t h = n / w;

    std::vector<uf::maze::Wall> walls = uf::maze::grid_walls(w, h);
    std::shuffle(walls.begin(), walls.end(), rng);

    std::vector<index_t> label(n);
    std::iota(label.begin(), label.end(), index_t{0});
    if (scramble) std::shuffle(label.begin(), label.end(), rng);

    std::vector<Op> ops;
    ops.reserve(walls.size());
    for (const uf::maze::Wall& wall : walls) ops.push_back({OpKind::Unite, label[wall.a], label[wall.b]});
    return ops;
}

static std::vector<Op> make_maze(std::size_t n, std::mt19937_64& rng) {
    return make_maze_ops(n, rng, false);
}

static std::vector<Op> make_maze_scrambled(std::size_t n, std::mt19937_64& rng) {
    return make_maze_ops(n, rng, true);
}

struct Workload {
    const char* name;
    std::vector<Op> (*make)(std::size_t, std::mt19937_64&);
};

static const Workload kWorkloads[] = {
    {"random_mixed", make_random_mixed},
    {"chain", make_chain},
    {"maze", make_maze},
    {"maze_scrambled", make_maze_scrambled},
};

// ---------------------------------------------------------------- timing

struct Measurement {
    std::int64_t ns;
    std::uint64_t checksum;
};

// Applies every op in order. Shared by the timed and the counting runs, so
// both execute exactly the same sequence of calls.
// The checksum depends on every result in order, so the compiler cannot drop
// the loop, and two variants only agree if (almost surely) every individual
// answer agrees.
template <class UF>
static std::uint64_t apply_ops(UF& uf, const std::vector<Op>& ops) {
    std::uint64_t checksum = 0;
    for (const Op& op : ops) {
        const bool r = (op.kind == OpKind::Unite) ? uf.unite(op.a, op.b) : uf.connected(op.a, op.b);
        checksum = checksum * 1000003u + r;  // order-sensitive, wraps mod 2^64
    }
    return checksum;
}

template <class UF>
static Measurement run_once(std::size_t n, const std::vector<Op>& ops) {
    UF uf(n);  // constructed outside the timed region

    const auto t0 = Clock::now();
    const std::uint64_t checksum = apply_ops(uf, ops);
    const auto t1 = Clock::now();

    return {std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count(), checksum};
}

// One sample: `inner` runs back to back, each with a fresh object built
// untimed, summing only the timed parts. Every run applies the same ops, so
// every run must produce the same checksum.
template <class UF>
static Measurement run_sample(std::size_t n, const std::vector<Op>& ops, int inner) {
    Measurement total{0, 0};
    for (int i = 0; i < inner; ++i) {
        const Measurement m = run_once<UF>(n, ops);
        if (i > 0 && m.checksum != total.checksum) {
            std::fprintf(stderr, "checksum changed between identical runs\n");
            std::exit(1);
        }
        total.ns += m.ns;
        total.checksum = m.checksum;
    }
    return total;
}

// How many runs one sample needs so that its timed total is at least
// min_ns, estimated from the warm-up run. Capped so tiny runs cannot
// produce absurd repeat counts.
static int inner_repeats(std::int64_t warmup_ns, std::int64_t min_ns) {
    if (warmup_ns >= min_ns) return 1;
    const std::int64_t per_run = warmup_ns > 0 ? warmup_ns : 1;
    const std::int64_t k = (min_ns + per_run - 1) / per_run;  // ceiling division
    return static_cast<int>(std::min<std::int64_t>(k, 100000));
}

// ---------------------------------------------------------------- main

struct Options {
    std::string out = "results/bench.csv";
    std::string counts_out;  // empty = don't collect counters
    int reps = 5;
    int min_log = 10;
    int max_log = 20;
    int quadratic_max_log = 14;
    std::uint64_t seed = 12345;
    int min_time_ms = 5;  // minimum timed duration of one sample
    int cpu = -1;         // -1 = choose automatically (Windows) / don't pin (elsewhere)
    bool pin = true;
};

static Options parse_args(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        auto next = [&]() -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "missing value for %s\n", argv[i]);
                std::exit(2);
            }
            return argv[++i];
        };
        if (!std::strcmp(argv[i], "--out")) o.out = next();
        else if (!std::strcmp(argv[i], "--counts-out")) o.counts_out = next();
        else if (!std::strcmp(argv[i], "--reps")) o.reps = std::atoi(next());
        else if (!std::strcmp(argv[i], "--min-log")) o.min_log = std::atoi(next());
        else if (!std::strcmp(argv[i], "--max-log")) o.max_log = std::atoi(next());
        else if (!std::strcmp(argv[i], "--quadratic-max-log")) o.quadratic_max_log = std::atoi(next());
        else if (!std::strcmp(argv[i], "--seed")) o.seed = std::strtoull(next(), nullptr, 10);
        else if (!std::strcmp(argv[i], "--min-time-ms")) o.min_time_ms = std::atoi(next());
        else if (!std::strcmp(argv[i], "--cpu")) o.cpu = std::atoi(next());
        else if (!std::strcmp(argv[i], "--no-pin")) o.pin = false;
        else {
            std::fprintf(stderr, "unknown argument: %s\n", argv[i]);
            std::exit(2);
        }
    }
    return o;
}

int main(int argc, char** argv) {
#ifndef NDEBUG
    std::fprintf(stderr, "WARNING: assertions enabled; build with -DCMAKE_BUILD_TYPE=Release for real numbers\n");
#endif
    const Options opt = parse_args(argc, argv);
    if (opt.min_time_ms < 0) {
        std::fprintf(stderr, "--min-time-ms must be >= 0\n");
        return 2;
    }
    const std::int64_t min_ns = std::int64_t{opt.min_time_ms} * 1000000;

    if (opt.pin) {
        const int cpu = opt.cpu >= 0 ? opt.cpu : bench::fastest_cpu();
        if (cpu >= 0 && bench::pin_to_cpu(cpu)) {
            std::fprintf(stderr, "pinned to CPU %d\n", cpu);
        } else {
            std::fprintf(stderr, "WARNING: not pinned to a CPU%s\n",
                         cpu < 0 ? " (pass --cpu N to choose one)" : "");
        }
    }

    std::ofstream csv(opt.out);
    if (!csv) {
        std::fprintf(stderr, "cannot open %s\n", opt.out.c_str());
        return 1;
    }
    // ops = operations in ONE run; a sample is `inner` runs, so
    // ns_per_op = ns_total / (inner * ops).
    csv << "workload,variant,n,ops,rep,inner,ns_total,ns_per_op,checksum\n";

    std::ofstream counts;
    if (!opt.counts_out.empty()) {
        counts.open(opt.counts_out);
        if (!counts) {
            std::fprintf(stderr, "cannot open %s\n", opt.counts_out.c_str());
            return 1;
        }
        counts << "workload,variant,n,ops,finds,path_length,writes,max_path,"
                  "path_per_find,writes_per_find,checksum\n";
    }

    for (const Workload& wl : kWorkloads) {
        for (int lg = opt.min_log; lg <= opt.max_log; ++lg) {
            const std::size_t n = std::size_t{1} << lg;

            // Same seed for every variant => identical op sequence for all of them.
            std::mt19937_64 rng(opt.seed + static_cast<std::uint64_t>(lg));
            const std::vector<Op> ops = wl.make(n, rng);

            std::map<std::string, std::uint64_t> checksums;

            uf::for_each_variant([&](auto tag, const char* name) {
                using UF = typename decltype(tag)::type;
                if (uf::may_be_quadratic<UF>() && lg > opt.quadratic_max_log) return;

                // Warm-up: discarded, but used to choose `inner`.
                const Measurement warm = run_once<UF>(n, ops);
                const int inner = inner_repeats(warm.ns, min_ns);

                for (int rep = 0; rep < opt.reps; ++rep) {
                    const Measurement m = run_sample<UF>(n, ops, inner);
                    checksums[name] = m.checksum;
                    const double per_op = static_cast<double>(m.ns) / (static_cast<double>(inner) * ops.size());
                    csv << wl.name << ',' << name << ',' << n << ',' << ops.size() << ',' << rep << ','
                        << inner << ',' << m.ns << ',' << per_op << ',' << m.checksum << '\n';
                }

                // Untimed counting run. Its checksum joins the cross-check below
                // under its own key, so a counting twin that behaves differently
                // from the timed variant aborts the run.
                if constexpr (uf::is_union_find<UF>::value) {
                    if (counts.is_open()) {
                        typename uf::counting_twin<UF>::type counted(n);
                        const std::uint64_t checksum = apply_ops(counted, ops);
                        checksums[std::string(name) + "(counting)"] = checksum;
                        const uf::Stats& s = counted.stats();
                        const double finds = s.finds ? static_cast<double>(s.finds) : 1.0;
                        counts << wl.name << ',' << name << ',' << n << ',' << ops.size() << ','
                               << s.finds << ',' << s.path_length << ',' << s.writes << ','
                               << s.max_path << ',' << s.path_length / finds << ','
                               << s.writes / finds << ',' << checksum << '\n';
                    }
                }
                std::fprintf(stderr, "%-12s n=2^%-2d %-16s done\n", wl.name, lg, name);
            });

            // Cross-check: all variants must agree on every answer.
            for (const auto& [name, sum] : checksums) {
                if (sum != checksums.begin()->second) {
                    std::fprintf(stderr, "CHECKSUM MISMATCH: %s on %s n=%zu\n", name.c_str(), wl.name, n);
                    return 1;
                }
            }
        }
    }
    std::fprintf(stderr, "wrote %s\n", opt.out.c_str());
    if (counts.is_open()) std::fprintf(stderr, "wrote %s\n", opt.counts_out.c_str());
    return 0;
}
