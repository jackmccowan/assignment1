// Correctness tests for maze generation (uf/maze.hpp), run with every variant.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <queue>
#include <random>
#include <vector>

#include "check.hpp"
#include "uf/maze.hpp"
#include "uf/variants.hpp"

using uf::maze::index_t;
using uf::maze::Wall;

// Wall count formula for a w x h grid.
static void test_wall_count() {
    g_variant = "grid_walls";
    CHECK(uf::maze::grid_walls(0, 0).empty());
    CHECK(uf::maze::grid_walls(0, 5).empty());  // w == 0 must not underflow
    CHECK(uf::maze::grid_walls(1, 1).empty());
    CHECK(uf::maze::grid_walls(4, 1).size() == 3);
    CHECK(uf::maze::grid_walls(3, 3).size() == 12);
    CHECK(uf::maze::grid_walls(5, 7).size() == 4 * 7 + 5 * 6);
}

// Checks the carved passages form a spanning tree of the w x h grid, using a
// BFS that does not involve union-find at all:
//  * every passage joins two genuinely adjacent cells,
//  * there are exactly w*h - 1 passages,
//  * every cell is reachable from cell 0.
// w*h - 1 edges plus connected on w*h vertices implies no cycles.
static void check_spanning_tree(std::size_t w, std::size_t h, const std::vector<Wall>& passages) {
    const std::size_t cells = w * h;
    if (cells == 0) {
        CHECK(passages.empty());
        return;
    }
    CHECK(passages.size() == cells - 1);

    std::vector<std::vector<index_t>> adj(cells);
    for (const Wall& p : passages) {
        CHECK(p.a < cells && p.b < cells);
        if (p.a >= cells || p.b >= cells) return;
        const std::size_t ax = p.a % w, ay = p.a / w, bx = p.b % w, by = p.b / w;
        const std::size_t dx = ax > bx ? ax - bx : bx - ax;
        const std::size_t dy = ay > by ? ay - by : by - ay;
        CHECK(dx + dy == 1);  // adjacent: one step left/right or up/down
        adj[p.a].push_back(p.b);
        adj[p.b].push_back(p.a);
    }

    std::vector<bool> seen(cells, false);
    std::queue<index_t> q;
    q.push(0);
    seen[0] = true;
    std::size_t visited = 1;
    while (!q.empty()) {
        const index_t u = q.front();
        q.pop();
        for (index_t v : adj[u]) {
            if (!seen[v]) {
                seen[v] = true;
                ++visited;
                q.push(v);
            }
        }
    }
    CHECK(visited == cells);
}

static bool same_walls(const std::vector<Wall>& x, const std::vector<Wall>& y) {
    return std::equal(x.begin(), x.end(), y.begin(), y.end(),
                      [](const Wall& p, const Wall& q) { return p.a == q.a && p.b == q.b; });
}

int main() {
    test_wall_count();

    // Grid shapes, including the edge cases: empty, a single cell, and
    // one-cell-wide corridors, where every wall must be removed.
    const std::size_t shapes[][2] = {{0, 0}, {1, 1}, {1, 6}, {6, 1}, {2, 2}, {7, 5}, {32, 32}};

    for (const auto& shape : shapes) {
        const std::size_t w = shape[0], h = shape[1];
        for (std::uint32_t seed = 1; seed <= 3; ++seed) {
            std::vector<Wall> walls = uf::maze::grid_walls(w, h);
            std::mt19937 rng(seed);
            std::shuffle(walls.begin(), walls.end(), rng);

            // Reference result from the first variant; all others must match it.
            std::vector<Wall> reference;
            bool have_reference = false;

            uf::for_each_variant([&](auto tag, const char* name) {
                using UF = typename decltype(tag)::type;
                g_variant = name;
                const std::vector<Wall> passages = uf::maze::carve<UF>(w, h, walls);
                check_spanning_tree(w, h, passages);
                if (!have_reference) {
                    reference = passages;
                    have_reference = true;
                } else {
                    CHECK(same_walls(passages, reference));  // same order in => same maze out
                }
            });
        }
    }

    if (g_failures) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("all maze tests passed\n");
    return 0;
}
