#pragma once

// Quick-find baseline: every element stores its set label directly.
// find is O(1) worst case; unite is Theta(n) worst case because it relabels
// the whole array. m unions therefore cost Theta(m * n).

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <vector>

namespace uf {

class QuickFind {
public:
    using index_t = std::uint32_t;

    explicit QuickFind(std::size_t n) : id_(n), components_(n) {
        assert(n <= std::numeric_limits<index_t>::max());
        std::iota(id_.begin(), id_.end(), index_t{0});
    }

    // Invariant: a and b are in the same set  <=>  id_[a] == id_[b].
    index_t find(index_t x) const {
        assert(x < id_.size());
        return id_[x];
    }

    bool unite(index_t a, index_t b) {
        // Copy the labels first: the loop overwrites id_[a], so reading
        // id_[a] inside the loop would stop relabelling part-way through.
        const index_t ia = find(a);
        const index_t ib = find(b);
        if (ia == ib) return false;
        for (index_t& label : id_) {
            if (label == ia) label = ib;
        }
        --components_;
        return true;
    }

    bool connected(index_t a, index_t b) const { return find(a) == find(b); }

    std::size_t size() const { return id_.size(); }
    std::size_t components() const { return components_; }

private:
    std::vector<index_t> id_;
    std::size_t components_;
};

}  // namespace uf
