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
// Usage: bench [--out results/bench.csv] [--counts-out results/counts.csv]
//              [--reps 5] [--min-log 10] [--max-log 20]
//              [--quadratic-max-log 14] [--seed 12345]

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <random>
#include <string>
#include <utility>
#include <vector>

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

struct Workload {
    const char* name;
    std::vector<Op> (*make)(std::size_t, std::mt19937_64&);
};

static const Workload kWorkloads[] = {
    {"random_mixed", make_random_mixed},
    {"chain", make_chain},
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

// ---------------------------------------------------------------- main

struct Options {
    std::string out = "results/bench.csv";
    std::string counts_out;  // empty = don't collect counters
    int reps = 5;
    int min_log = 10;
    int max_log = 20;
    int quadratic_max_log = 14;
    std::uint64_t seed = 12345;
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

    std::ofstream csv(opt.out);
    if (!csv) {
        std::fprintf(stderr, "cannot open %s\n", opt.out.c_str());
        return 1;
    }
    csv << "workload,variant,n,ops,rep,ns_total,ns_per_op,checksum\n";

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

                for (int rep = 0; rep < opt.reps; ++rep) {
                    const Measurement m = run_once<UF>(n, ops);
                    checksums[name] = m.checksum;
                    csv << wl.name << ',' << name << ',' << n << ',' << ops.size() << ',' << rep << ','
                        << m.ns << ',' << static_cast<double>(m.ns) / ops.size() << ',' << m.checksum
                        << '\n';
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
