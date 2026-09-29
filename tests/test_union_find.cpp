// Correctness tests for every variant in uf/variants.hpp.
// No external framework: CHECK records failures, main returns non-zero if any.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <queue>
#include <random>
#include <vector>

#include "uf/variants.hpp"

static int g_failures = 0;
static const char* g_variant = "";

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "[%s] %s:%d: CHECK failed: %s\n", g_variant,    \
                         __FILE__, __LINE__, #cond);                             \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)

using index_t = std::uint32_t;

// Independent oracle: recompute component labels by BFS over the edges
// passed to unite so far. Slow, but obviously correct.
static std::vector<int> bfs_labels(std::size_t n, const std::vector<std::vector<index_t>>& adj) {
    std::vector<int> label(n, -1);
    int next = 0;
    for (std::size_t s = 0; s < n; ++s) {
        if (label[s] != -1) continue;
        std::queue<index_t> q;
        q.push(static_cast<index_t>(s));
        label[s] = next;
        while (!q.empty()) {
            index_t u = q.front();
            q.pop();
            for (index_t v : adj[u]) {
                if (label[v] == -1) {
                    label[v] = next;
                    q.push(v);
                }
            }
        }
        ++next;
    }
    return label;
}

template <class UF>
void test_empty_and_singleton() {
    UF empty(0);
    CHECK(empty.size() == 0);
    CHECK(empty.components() == 0);

    UF one(1);
    CHECK(one.find(0) == 0);
    CHECK(one.connected(0, 0));
    CHECK(!one.unite(0, 0));  // self-union is a no-op
    CHECK(one.components() == 1);
}

template <class UF>
void test_basic() {
    UF u(10);
    for (index_t i = 0; i < 10; ++i) CHECK(u.find(i) == i);
    CHECK(u.components() == 10);

    CHECK(u.unite(0, 1));
    CHECK(u.unite(2, 3));
    CHECK(u.connected(0, 1));
    CHECK(u.connected(3, 2));
    CHECK(!u.connected(1, 2));
    CHECK(u.components() == 8);

    CHECK(u.unite(1, 3));
    CHECK(u.connected(0, 2));
    CHECK(!u.unite(0, 3));  // already together: returns false, count unchanged
    CHECK(u.components() == 7);
    CHECK(!u.connected(0, 9));
}

// Random unions and queries, checked against the BFS oracle.
template <class UF>
void test_random_against_oracle(std::uint32_t seed) {
    const std::size_t n = 64;
    std::mt19937 rng(seed);
    std::uniform_int_distribution<index_t> pick(0, static_cast<index_t>(n - 1));

    UF u(n);
    std::vector<std::vector<index_t>> adj(n);
    std::size_t expected_components = n;

    for (int step = 0; step < 300; ++step) {
        index_t a = pick(rng), b = pick(rng);
        std::vector<int> before = bfs_labels(n, adj);
        bool expect_merge = before[a] != before[b];

        CHECK(u.unite(a, b) == expect_merge);
        adj[a].push_back(b);
        adj[b].push_back(a);
        if (expect_merge) --expected_components;
        CHECK(u.components() == expected_components);

        std::vector<int> after = bfs_labels(n, adj);
        for (int q = 0; q < 8; ++q) {
            index_t x = pick(rng), y = pick(rng);
            CHECK(u.connected(x, y) == (after[x] == after[y]));
        }
    }
}

// Structural invariants of the forest, only for UnionFind<L, P>.
template <class UF>
void test_invariants(std::uint32_t seed) {
    const std::size_t n = 1000;
    std::mt19937 rng(seed);
    std::uniform_int_distribution<index_t> pick(0, static_cast<index_t>(n - 1));
    UF u(n);

    for (int step = 0; step < 2000; ++step) {
        if (step % 2 == 0) u.unite(pick(rng), pick(rng));
        else u.find(pick(rng));
    }

    const double log2n = std::log2(static_cast<double>(n));
    std::vector<std::size_t> members(n, 0);
    for (index_t x = 0; x < n; ++x) {
        index_t p = u.parent_of(x);
        CHECK(p < n);
        // Every element reaches a root (parent_of(root) == root).
        index_t r = u.find(x);
        CHECK(u.parent_of(r) == r);
        ++members[r];

        if constexpr (UF::link == uf::Link::ByRank) {
            if (p != x) CHECK(u.aux_of(p) > u.aux_of(x));  // rank strictly increases upward
            CHECK(u.aux_of(x) <= log2n);                    // rank <= floor(log2 n)
        }
        if constexpr (UF::link != uf::Link::Naive) {
            CHECK(u.depth(x) <= log2n);  // height bound for rank/size linking
        }
    }
    if constexpr (UF::link == uf::Link::BySize) {
        for (index_t r = 0; r < n; ++r) {
            if (u.parent_of(r) == r) CHECK(u.aux_of(r) == members[r]);
        }
    }
}

// Naive linking on unite(i, i+1) builds a chain of depth n-1. find must be
// iterative so this does not overflow the stack.
template <class UF>
void test_long_chain() {
    const std::size_t n = 200000;
    UF u(n);
    for (index_t i = 0; i + 1 < n; ++i) u.unite(i, i + 1);
    CHECK(u.components() == 1);
    CHECK(u.connected(0, static_cast<index_t>(n - 1)));
}

// Counters on a hand-checked case. Naive linking with unite(i, i+1) builds
// the chain 0 -> 1 -> 2 -> 3 -> 4, so find(0) starts at depth 4.
template <uf::Path P>
void test_counters_on_chain() {
    uf::UnionFind<uf::Link::Naive, P, true> u(5);
    for (index_t i = 0; i < 4; ++i) u.unite(i, i + 1);
    // Each unite(i, i+1) found two roots directly: 8 finds, no hops, no writes.
    CHECK(u.stats().finds == 8);
    CHECK(u.stats().path_length == 0);
    CHECK(u.stats().writes == 0);

    u.reset_stats();
    CHECK(u.find(0) == 4);
    CHECK(u.stats().finds == 1);
    CHECK(u.stats().path_length == 4);
    CHECK(u.stats().max_path == 4);

    if constexpr (P == uf::Path::None) {
        CHECK(u.stats().writes == 0);
        CHECK(u.depth(0) == 4);  // tree unchanged
    } else if constexpr (P == uf::Path::Compression) {
        CHECK(u.stats().writes == 3);  // 0, 1, 2 repointed; 3 already points at the root
        CHECK(u.depth(0) == 1);
    } else {
        CHECK(u.stats().writes == 2);  // 0 -> 2, then 2 -> 4
        CHECK(u.depth(0) == 2);        // 0 -> 2 -> 4
    }
}

// On random operations: each find must add exactly depth(x) (measured just
// before it) to path_length, and the counting version must give the same
// answers as the plain one, i.e. counting does not change the algorithm.
template <class UF>
void test_counters_match_depth(std::uint32_t seed) {
    using Counted = typename uf::counting_twin<UF>::type;
    const std::size_t n = 500;
    std::mt19937 rng(seed);
    std::uniform_int_distribution<index_t> pick(0, static_cast<index_t>(n - 1));
    UF plain(n);
    Counted counted(n);

    for (int step = 0; step < 3000; ++step) {
        const index_t a = pick(rng), b = pick(rng);
        if (step % 3 == 0) {
            CHECK(plain.unite(a, b) == counted.unite(a, b));
        } else {
            const std::size_t d = counted.depth(a);
            const std::uint64_t before = counted.stats().path_length;
            CHECK(plain.find(a) == counted.find(a));
            CHECK(counted.stats().path_length - before == d);
        }
    }
}

int main() {
    uf::for_each_variant([](auto tag, const char* name) {
        using UF = typename decltype(tag)::type;
        g_variant = name;
        const int before = g_failures;

        test_empty_and_singleton<UF>();
        test_basic<UF>();
        for (std::uint32_t seed = 1; seed <= 5; ++seed) test_random_against_oracle<UF>(seed);
        if constexpr (uf::is_union_find<UF>::value) {
            for (std::uint32_t seed = 1; seed <= 5; ++seed) test_invariants<UF>(seed);
            for (std::uint32_t seed = 1; seed <= 3; ++seed) test_counters_match_depth<UF>(seed);
            test_long_chain<UF>();
        }

        std::printf("%-16s %s\n", name, g_failures == before ? "ok" : "FAILED");
    });

    g_variant = "counters_on_chain";
    test_counters_on_chain<uf::Path::None>();
    test_counters_on_chain<uf::Path::Compression>();
    test_counters_on_chain<uf::Path::Halving>();

    if (g_failures) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("all tests passed\n");
    return 0;
}
