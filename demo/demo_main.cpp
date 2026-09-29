// Demo: prints the parent array after every operation, so you can watch how
// each variant shapes the forest. Not a test and not a benchmark.
//
// Usage: demo                        runs the built-in script
//        demo u 0 1 u 1 2 f 0        your own script: "u a b" = unite(a, b), "f x" = find(x)
//        demo maze [w h [seed]]      draws a maze made by randomised Kruskal (uf/maze.hpp)
//
// Reading the output: parent[i] == i means i is a root. depth(0) is the number
// of hops from element 0 to its root. "rank"/"size" is only meaningful at
// roots (size at a non-root is stale; it is never read again).
// A "*" after unite means the two elements were already connected.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

#include "uf/maze.hpp"
#include "uf/union_find.hpp"

using index_t = std::uint32_t;
constexpr index_t kN = 8;

struct Step {
    char kind;  // 'u' = unite(a, b), 'f' = find(a)
    index_t a;
    index_t b;
};

// Default script: unite(i, i+1) builds a chain under naive linking, then two
// finds from the bottom of it show what each path rule does to that chain.
static const std::vector<Step> kDefaultScript = {
    {'u', 0, 1}, {'u', 1, 2}, {'u', 2, 3}, {'u', 3, 4}, {'f', 0, 0}, {'f', 0, 0},
};

template <class UF>
static void print_state(const UF& u, const char* label) {
    std::printf("  %-14s parent: [", label);
    for (index_t i = 0; i < kN; ++i) std::printf(i ? " %u" : "%u", u.parent_of(i));
    std::printf("]");
    if constexpr (UF::link != uf::Link::Naive) {
        std::printf("  %s: [", UF::link == uf::Link::ByRank ? "rank" : "size");
        for (index_t i = 0; i < kN; ++i) std::printf(i ? " %u" : "%u", u.aux_of(i));
        std::printf("]");
    }
    std::printf("  depth(0)=%zu\n", u.depth(0));
}

template <class UF>
static void run(const char* name, const std::vector<Step>& script) {
    std::printf("\n%s\n", name);
    UF u(kN);
    print_state(u, "start");
    char label[32];
    for (const Step& s : script) {
        if (s.kind == 'u') {
            const bool merged = u.unite(s.a, s.b);
            std::snprintf(label, sizeof label, "unite(%u,%u)%s", s.a, s.b, merged ? "" : "*");
        } else {
            const index_t root = u.find(s.a);
            std::snprintf(label, sizeof label, "find(%u)=%u", s.a, root);
        }
        print_state(u, label);
    }
}

[[noreturn]] static void usage() {
    std::fprintf(stderr, "usage: demo [u a b | f x]...   (elements are 0..%u)\n", kN - 1);
    std::fprintf(stderr, "       demo maze [w h [seed]]\n");
    std::exit(2);
}

static std::vector<Step> parse_script(int argc, char** argv) {
    auto element = [&](int i) -> index_t {
        if (i >= argc) usage();
        char* end = nullptr;
        const long v = std::strtol(argv[i], &end, 10);
        if (*end != '\0' || v < 0 || v >= static_cast<long>(kN)) usage();
        return static_cast<index_t>(v);
    };
    std::vector<Step> script;
    for (int i = 1; i < argc;) {
        if (!std::strcmp(argv[i], "u")) {
            const index_t a = element(i + 1);
            const index_t b = element(i + 2);
            script.push_back({'u', a, b});
            i += 3;
        } else if (!std::strcmp(argv[i], "f")) {
            script.push_back({'f', element(i + 1), 0});
            i += 2;
        } else {
            usage();
        }
    }
    return script;
}

// ---------------------------------------------------------------- maze mode

// Draws the maze as ASCII. Each cell is "  "; a closed wall to the east is
// "|" and to the south is "--". The entrance is the top of cell (0, 0) and
// the exit is the bottom of cell (w-1, h-1).
static void print_maze(std::size_t w, std::size_t h, const std::vector<uf::maze::Wall>& passages) {
    // grid_walls always stores a wall as (a, b) with a < b, so b == a + 1
    // means an east-west passage and b == a + w a north-south one.
    std::vector<bool> open_east(w * h, false), open_south(w * h, false);
    for (const uf::maze::Wall& p : passages) {
        if (p.b == p.a + 1) open_east[p.a] = true;
        else open_south[p.a] = true;
    }

    std::printf("+");
    for (std::size_t x = 0; x < w; ++x) std::printf(x == 0 ? "  +" : "--+");  // entrance at (0, 0)
    std::printf("\n");
    for (std::size_t y = 0; y < h; ++y) {
        std::printf("|");
        for (std::size_t x = 0; x < w; ++x) std::printf(open_east[y * w + x] ? "   " : "  |");
        std::printf("\n+");
        for (std::size_t x = 0; x < w; ++x) {
            const bool exit_here = (y == h - 1 && x == w - 1);
            std::printf(open_south[y * w + x] || exit_here ? "  +" : "--+");
        }
        std::printf("\n");
    }
}

static std::size_t parse_count(const char* s, std::size_t lo, std::size_t hi) {
    char* end = nullptr;
    const long v = std::strtol(s, &end, 10);
    if (*end != '\0' || v < static_cast<long>(lo) || v > static_cast<long>(hi)) {
        std::fprintf(stderr, "usage: demo maze [w h [seed]]   (1 <= w, h <= 40)\n");
        std::exit(2);
    }
    return static_cast<std::size_t>(v);
}

static int run_maze(int argc, char** argv) {
    // argv[1] is "maze"; optional w, h, seed follow.
    const std::size_t w = argc > 2 ? parse_count(argv[2], 1, 40) : 12;
    const std::size_t h = argc > 3 ? parse_count(argv[3], 1, 40) : 8;
    const std::size_t seed = argc > 4 ? parse_count(argv[4], 0, 1000000) : 1;

    std::vector<uf::maze::Wall> walls = uf::maze::grid_walls(w, h);
    std::mt19937 rng(static_cast<std::uint32_t>(seed));
    std::shuffle(walls.begin(), walls.end(), rng);

    using UF = uf::UnionFind<uf::Link::ByRank, uf::Path::Compression>;
    const std::vector<uf::maze::Wall> passages = uf::maze::carve<UF>(w, h, walls);

    std::printf("%zux%zu maze, seed %zu: %zu of %zu walls removed (always w*h - 1 = %zu)\n\n", w, h,
                seed, passages.size(), walls.size(), w * h - 1);
    print_maze(w, h, passages);
    return 0;
}

int main(int argc, char** argv) {
    if (argc > 1 && !std::strcmp(argv[1], "maze")) return run_maze(argc, argv);

    const std::vector<Step> script = argc > 1 ? parse_script(argc, argv) : kDefaultScript;

    using uf::Link;
    using uf::Path;
    run<uf::UnionFind<Link::Naive, Path::None>>("naive linking, no path rule", script);
    run<uf::UnionFind<Link::Naive, Path::Compression>>("naive linking, full compression", script);
    run<uf::UnionFind<Link::Naive, Path::Halving>>("naive linking, halving", script);
    run<uf::UnionFind<Link::ByRank, Path::None>>("union by rank, no path rule", script);
    run<uf::UnionFind<Link::ByRank, Path::Compression>>("union by rank, full compression", script);
    run<uf::UnionFind<Link::BySize, Path::None>>("union by size, no path rule", script);
    return 0;
}
