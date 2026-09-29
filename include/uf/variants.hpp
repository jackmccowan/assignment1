#pragma once

// The single list of implementations under study. Tests and benchmarks both
// iterate over this, so adding a variant here makes it tested and benchmarked.

#include <type_traits>

#include "uf/quick_find.hpp"
#include "uf/union_find.hpp"

namespace uf {

template <class T>
struct type_tag {
    using type = T;
};

template <class T>
struct is_union_find : std::false_type {};
template <Link L, Path P, bool C>
struct is_union_find<UnionFind<L, P, C>> : std::true_type {};

// The instrumented version of a UnionFind type: same algorithm, with counters.
// Deliberately undefined for QuickFind, which has no find path to count.
template <class UF>
struct counting_twin;
template <Link L, Path P, bool C>
struct counting_twin<UnionFind<L, P, C>> {
    using type = UnionFind<L, P, true>;
};

// Calls f(type_tag<Impl>{}, "name") for every implementation.
// Names are CSV-friendly: <link>_<path>.
template <class F>
void for_each_variant(F&& f) {
    f(type_tag<QuickFind>{}, "quickfind");
    f(type_tag<UnionFind<Link::Naive, Path::None>>{}, "naive_none");
    f(type_tag<UnionFind<Link::Naive, Path::Compression>>{}, "naive_compress");
    f(type_tag<UnionFind<Link::Naive, Path::Halving>>{}, "naive_halve");
    f(type_tag<UnionFind<Link::ByRank, Path::None>>{}, "rank_none");
    f(type_tag<UnionFind<Link::ByRank, Path::Compression>>{}, "rank_compress");
    f(type_tag<UnionFind<Link::ByRank, Path::Halving>>{}, "rank_halve");
    f(type_tag<UnionFind<Link::BySize, Path::None>>{}, "size_none");
    f(type_tag<UnionFind<Link::BySize, Path::Compression>>{}, "size_compress");
    f(type_tag<UnionFind<Link::BySize, Path::Halving>>{}, "size_halve");
}

// Variants whose total cost can be quadratic in n on some workloads.
// The benchmark caps n for these so a run finishes in reasonable time.
template <class UF>
constexpr bool may_be_quadratic() {
    if constexpr (std::is_same_v<UF, QuickFind>) {
        return true;
    } else {
        return UF::link == Link::Naive && UF::path == Path::None;
    }
}

}  // namespace uf
