#pragma once

// Maze generation by randomised Kruskal: an application of union-find.
//
// A w x h grid has one cell per position; cell (x, y) has index y * w + x.
// Every pair of horizontally or vertically adjacent cells is separated by a
// wall. Visiting the walls in random order and knocking a wall down whenever
// its two cells are not yet connected (i.e. whenever unite succeeds) yields a
// spanning tree of the grid: a "perfect" maze, with exactly one path between
// any two cells.
//
// Why it is a spanning tree:
//  * No cycles: a wall is only removed if its cells were in different sets,
//    so each removal joins two separate trees and can never close a loop.
//  * Connected: the full grid is connected, so while two components remain
//    some wall between them has not been examined yet; Kruskal therefore
//    keeps removing walls until one component is left.
//  Together: exactly w*h - 1 walls are removed (for a non-empty grid).
//
// Randomness is deliberately NOT in this file: the caller shuffles the wall
// list, so benchmarks can do that before starting the timer.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace uf::maze {

using index_t = std::uint32_t;

struct Wall {
    index_t a;  // the two cells this wall separates
    index_t b;
};

// All interior walls of a w x h grid, in a fixed order: horizontal
// neighbours first, then vertical. There are (w-1)*h + w*(h-1) of them.
// The loops use x + 1 < w rather than x < w - 1 so that w == 0 cannot
// underflow the unsigned subtraction.
inline std::vector<Wall> grid_walls(std::size_t w, std::size_t h) {
    std::vector<Wall> walls;
    if (w == 0 || h == 0) return walls;
    walls.reserve((w - 1) * h + w * (h - 1));
    for (std::size_t y = 0; y < h; ++y)
        for (std::size_t x = 0; x + 1 < w; ++x)
            walls.push_back({static_cast<index_t>(y * w + x), static_cast<index_t>(y * w + x + 1)});
    for (std::size_t y = 0; y + 1 < h; ++y)
        for (std::size_t x = 0; x < w; ++x)
            walls.push_back({static_cast<index_t>(y * w + x), static_cast<index_t>((y + 1) * w + x)});
    return walls;
}

// Randomised Kruskal. Visits `walls` in the given order and returns the walls
// that were knocked down (the maze's passages), in the order they were removed.
// Stops early once w*h - 1 walls are down, because every later unite would
// fail anyway. Every correct UF variant removes exactly the same walls for the
// same input order, since unite's return value depends only on connectivity.
template <class UF>
std::vector<Wall> carve(std::size_t w, std::size_t h, const std::vector<Wall>& walls) {
    const std::size_t cells = w * h;
    std::vector<Wall> removed;
    if (cells == 0) return removed;
    removed.reserve(cells - 1);

    UF sets(cells);
    for (const Wall& wall : walls) {
        if (sets.unite(wall.a, wall.b)) {
            removed.push_back(wall);
            if (removed.size() == cells - 1) break;
        }
    }
    return removed;
}

}  // namespace uf::maze
