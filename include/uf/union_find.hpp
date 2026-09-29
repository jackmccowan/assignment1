#pragma once

// Disjoint-set forest (union-find) with a choice of linking rule and
// find-path rule, picked at compile time so the benchmark can compare
// every combination without runtime dispatch in the timed loop.
//
// Complexity summary for m operations on n elements (see PLAN.md for sources):
//
//   Link       Path          worst-case single find   amortized per operation
//   ---------  ------------  -----------------------  ------------------------------
//   Naive      None          O(n)                     O(n)  (a chain forces Theta(n))
//   Naive      Compression   O(n)                     O(log_{1+m/n} n)  [TvL 1984]
//   Naive      Halving       O(n)                     O(log_{1+m/n} n)  [TvL 1984]
//   Rank/Size  None          O(log n)                 O(log n)  (height <= log2 n)
//   Rank/Size  Compression   O(log n)                 O(alpha(n))       [Tarjan 1975; TvL 1984]
//   Rank/Size  Halving       O(log n)                 O(alpha(n))       [TvL 1984]
//
// TvL = Tarjan & van Leeuwen, "Worst-case analysis of set union algorithms", JACM 1984.
// "Worst-case single find" and "amortized" are different claims: with path
// compression one find can still be expensive, but it pays for later ones.
// With compression, rank is only an upper bound on height, not the height.

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <utility>
#include <vector>

namespace uf {

enum class Link { Naive, ByRank, BySize };
enum class Path { None, Compression, Halving };

// Operation counters, only collected when Counting == true.
struct Stats {
    std::uint64_t finds = 0;        // calls to find (unite and connected each make two)
    std::uint64_t path_length = 0;  // sum over finds of depth(x) when the find started
    std::uint64_t writes = 0;       // stores to parent_ made by find (not by linking)
    std::uint64_t max_path = 0;     // largest single depth(x) seen at the start of a find
};

// Counting is a compile-time switch: with Counting == false every counter
// update is removed by `if constexpr`, so timed builds contain no counter code.
template <Link L, Path P, bool Counting = false>
class UnionFind {
public:
    using index_t = std::uint32_t;
    static constexpr Link link = L;
    static constexpr Path path = P;
    static constexpr bool counting = Counting;

    // Every element starts as its own root. aux_ holds rank (ByRank, starts 0)
    // or size (BySize, starts 1). Naive linking does not need it, so we do not
    // allocate it: that keeps the memory footprint honest in the benchmarks.
    explicit UnionFind(std::size_t n)
        : parent_(n),
          aux_(L == Link::Naive ? 0 : n, L == Link::BySize ? 1u : 0u),
          components_(n) {
        assert(n <= std::numeric_limits<index_t>::max());
        std::iota(parent_.begin(), parent_.end(), index_t{0});
    }

    // Returns the root of x's tree. All variants are iterative, so a tree of
    // depth n (possible with naive linking) cannot overflow the call stack.
    index_t find(index_t x) {
        assert(x < parent_.size());
        // Counters (Counting only): `levels` is the depth of the original x,
        // i.e. the number of edges climbed from x to the root.
        [[maybe_unused]] std::uint64_t levels = 0;
        [[maybe_unused]] std::uint64_t writes = 0;
        index_t root;

        if constexpr (P == Path::None) {
            while (parent_[x] != x) {
                x = parent_[x];
                if constexpr (Counting) ++levels;
            }
            root = x;
        } else if constexpr (P == Path::Compression) {
            // Pass 1: locate the root.
            root = x;
            while (parent_[root] != root) {
                root = parent_[root];
                if constexpr (Counting) ++levels;
            }
            // Pass 2: point every node on the path directly at the root.
            // Stops when x is the root or already a child of the root.
            while (parent_[x] != root) {
                index_t next = parent_[x];
                parent_[x] = root;
                x = next;
                if constexpr (Counting) ++writes;
            }
        } else {  // Path::Halving
            // Make every other node on the path point to its grandparent.
            // One pass, no second walk; the root is a fixed point
            // (parent_[root] == root), so this never moves a node off its tree.
            while (parent_[x] != x) {
                if constexpr (Counting) {
                    // Jumping to the grandparent climbs 2 levels, or 1 if the parent is the root.
                    const index_t p = parent_[x];
                    levels += (parent_[p] == p) ? 1 : 2;
                    ++writes;
                }
                parent_[x] = parent_[parent_[x]];
                x = parent_[x];
            }
            root = x;
        }

        if constexpr (Counting) {
            ++stats_.finds;
            stats_.path_length += levels;
            stats_.writes += writes;
            if (levels > stats_.max_path) stats_.max_path = levels;
        }
        return root;
    }

    // Merges the sets containing a and b. Returns false if they were already
    // in the same set (nothing changes in that case).
    bool unite(index_t a, index_t b) {
        index_t ra = find(a);
        index_t rb = find(b);
        if (ra == rb) return false;

        if constexpr (L == Link::Naive) {
            parent_[ra] = rb;  // always hang a's root under b's root
        } else if constexpr (L == Link::ByRank) {
            // Invariant: for every non-root x, rank[parent[x]] > rank[x].
            // Attaching the lower-rank root under the higher one keeps it;
            // on a tie the new root's rank grows by one.
            if (aux_[ra] < aux_[rb]) std::swap(ra, rb);
            parent_[rb] = ra;
            if (aux_[ra] == aux_[rb]) ++aux_[ra];
        } else {  // Link::BySize
            // Invariant: for every root r, size[r] == number of elements in r's set.
            // (Sizes stored at non-roots are stale and never read.)
            if (aux_[ra] < aux_[rb]) std::swap(ra, rb);
            parent_[rb] = ra;
            aux_[ra] += aux_[rb];
        }
        --components_;
        return true;
    }

    bool connected(index_t a, index_t b) { return find(a) == find(b); }

    std::size_t size() const { return parent_.size(); }
    std::size_t components() const { return components_; }

    // --- Read-only inspection, used by tests and diagnostics (not timed). ---

    index_t parent_of(index_t x) const { return parent_[x]; }

    // Rank (ByRank) or size (BySize) stored at x. Not meaningful for Naive.
    index_t aux_of(index_t x) const {
        static_assert(L != Link::Naive, "Naive linking stores no rank/size");
        return aux_[x];
    }

    // Number of parent hops from x to its root, without modifying the tree.
    std::size_t depth(index_t x) const {
        std::size_t d = 0;
        while (parent_[x] != x) {
            x = parent_[x];
            ++d;
        }
        return d;
    }

    const Stats& stats() const {
        static_assert(Counting, "stats() needs UnionFind<L, P, true>");
        return stats_;
    }
    void reset_stats() {
        static_assert(Counting, "reset_stats() needs UnionFind<L, P, true>");
        stats_ = Stats{};
    }

private:
    std::vector<index_t> parent_;
    std::vector<index_t> aux_;
    std::size_t components_;
    Stats stats_;  // unused (never touched) when Counting == false
};

}  // namespace uf
