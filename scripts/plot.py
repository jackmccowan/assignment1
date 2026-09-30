"""Plot benchmark results. Reads results/bench*.csv; writes report/figures/*.png.

One figure per workload: median ns/op vs n, log-log, with error bars showing
the interquartile range over repetitions. Also writes report/figures/spread.csv
(median, Q1, Q3 and IQR/median for every point) so claims like "within 10%"
can be checked against the measurement noise.
Colour encodes the linking rule, line style + marker encode the path rule,
so 10 variants never need more than 4 colours.

Usage: python scripts/plot.py
"""

from pathlib import Path

import matplotlib.pyplot as plt
import pandas as pd
from matplotlib.ticker import FuncFormatter

ROOT = Path(__file__).resolve().parent.parent
RESULTS = ROOT / "results"
FIGURES = ROOT / "report" / "figures"

LINK_COLOUR = {
    "quickfind": "#6b6a64",  # neutral grey: the baseline
    "naive": "#2a78d6",
    "rank": "#eb6834",
    "size": "#1baf7a",
}
PATH_STYLE = {  # (line style, marker)
    "none": ("-", "o"),
    "compress": ("--", "s"),
    "halve": (":", "^"),
}


def style_for(variant: str):
    if variant == "quickfind":
        return LINK_COLOUR["quickfind"], "-", "D"
    link, path = variant.split("_")
    ls, marker = PATH_STYLE[path]
    return LINK_COLOUR[link], ls, marker


def main() -> None:
    files = sorted(RESULTS.glob("bench*.csv"))  # timing CSVs only; counts*.csv have other columns
    if not files:
        raise SystemExit(f"no CSV files in {RESULTS}")
    df = pd.concat((pd.read_csv(f) for f in files), ignore_index=True)

    # Per (workload, variant, n): the median over repetitions (robust to one-off
    # OS noise) and the interquartile range, which shows how much repeated runs
    # vary. rel_iqr = IQR / median: a difference between two variants that is
    # smaller than their rel_iqr cannot be distinguished from noise.
    keys = ["workload", "variant", "n"]
    stats = df.groupby(keys)["ns_per_op"].agg(
        median="median",
        q1=lambda s: s.quantile(0.25),
        q3=lambda s: s.quantile(0.75),
        reps="count",
    ).reset_index()
    stats["rel_iqr"] = (stats["q3"] - stats["q1"]) / stats["median"]

    FIGURES.mkdir(parents=True, exist_ok=True)
    spread_out = FIGURES / "spread.csv"
    stats.sort_values(keys).to_csv(spread_out, index=False)
    print(f"wrote {spread_out}")

    min_reps = int(stats["reps"].min())
    if min_reps < 5:
        print(f"note: some points have only {min_reps} repetition(s); error bars need at least 5 to mean much")

    for workload, g in stats.groupby("workload"):
        worst = g.loc[g["rel_iqr"].idxmax()]
        print(f"{workload}: largest spread {worst['rel_iqr']:.1%} of the median "
              f"({worst['variant']} at n={int(worst['n'])})")

        fig, ax = plt.subplots(figsize=(7.5, 4.8))
        for variant, gv in g.groupby("variant"):
            gv = gv.sort_values("n")
            colour, ls, marker = style_for(variant)
            # Asymmetric error bars: from Q1 up to Q3 around the median.
            yerr = [gv["median"] - gv["q1"], gv["q3"] - gv["median"]]
            ax.errorbar(gv["n"], gv["median"], yerr=yerr, color=colour, linestyle=ls, marker=marker,
                        markersize=6, linewidth=2, elinewidth=1, capsize=3, label=variant)
        ax.set_xscale("log", base=2)
        ax.set_yscale("log")
        ax.set_xlabel("n (elements)")
        ax.set_ylabel("time per operation (ns)")
        ax.set_title(f"Workload: {workload}  (median; bars = interquartile range, {min_reps}+ reps)")
        ax.grid(True, which="major", color="#e0dfd8", linewidth=0.8)
        for side in ("top", "right"):
            ax.spines[side].set_visible(False)
        ax.legend(fontsize=8, ncol=2, frameon=False)
        fig.tight_layout()
        out = FIGURES / f"{workload}.png"
        fig.savefig(out, dpi=150)
        plt.close(fig)
        print(f"wrote {out}")

    plot_locality_ratio(stats)
    plot_path_per_find()
    plot_alpha_zoom(stats)


# Where the parent + rank arrays (8 bytes per element) outgrow a cache level
# on the test machine: 1.25 MB L2 per P-core, 24 MB shared L3.
CACHE_LIMITS = {"L2 (1.25 MB)": 1.25 * 2**20 / 8, "L3 (24 MB)": 24 * 2**20 / 8}


def mark_cache_limits(ax) -> None:
    for label, n in CACHE_LIMITS.items():
        ax.axvline(n, color="#6b6a64", linestyle=":", linewidth=1)
        ax.text(n, 1.0, f" {label}", transform=ax.get_xaxis_transform(), fontsize=8,
                color="#6b6a64", va="top")


def finish(ax, fig, name: str) -> None:
    ax.set_xscale("log", base=2)
    ax.grid(True, which="major", color="#e0dfd8", linewidth=0.8)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    ax.legend(fontsize=8, frameon=False)
    fig.tight_layout()
    out = FIGURES / name
    fig.savefig(out, dpi=150)
    plt.close(fig)
    print(f"wrote {out}")


def plot_locality_ratio(stats: pd.DataFrame) -> None:
    """H3b: median ns/op of maze_scrambled divided by maze, same variant and n."""
    m = stats[stats["workload"] == "maze"].set_index(["variant", "n"])["median"]
    s = stats[stats["workload"] == "maze_scrambled"].set_index(["variant", "n"])["median"]
    ratio = (s / m).dropna().reset_index(name="ratio")
    if ratio.empty:
        return
    fig, ax = plt.subplots(figsize=(7.5, 4.8))
    for variant in ["rank_compress", "size_compress", "rank_halve", "size_halve"]:
        g = ratio[ratio["variant"] == variant].sort_values("n")
        if g.empty:
            continue
        colour, ls, marker = style_for(variant)
        ax.plot(g["n"], g["ratio"], color=colour, linestyle=ls, marker=marker, markersize=6,
                linewidth=2, label=variant)
    ax.axhline(1.0, color="#6b6a64", linewidth=1)
    mark_cache_limits(ax)
    ax.set_xlabel("n (elements)")
    ax.set_ylabel("maze_scrambled / maze (median ns/op)")
    ax.set_title("Same maze, scrambled memory layout")
    finish(ax, fig, "maze_locality_ratio.png")


def plot_alpha_zoom(stats: pd.DataFrame) -> None:
    """H1 and H2: only the four alpha(n) variants, so their differences are visible.
    One panel per workload with a shared y-axis; error bars are the IQR."""
    variants = ["rank_compress", "size_compress", "rank_halve", "size_halve"]
    workloads = [w for w in ("maze", "random_mixed") if w in set(stats["workload"])]
    if not workloads:
        return
    fig, axes = plt.subplots(1, len(workloads), figsize=(11, 4.6), sharey=True, squeeze=False)
    for ax, workload in zip(axes[0], workloads):
        g = stats[(stats["workload"] == workload) & (stats["variant"].isin(variants))]
        for variant in variants:
            gv = g[g["variant"] == variant].sort_values("n")
            colour, ls, marker = style_for(variant)
            yerr = [gv["median"] - gv["q1"], gv["q3"] - gv["median"]]
            ax.errorbar(gv["n"], gv["median"], yerr=yerr, color=colour, linestyle=ls, marker=marker,
                        markersize=6, linewidth=2, elinewidth=1, capsize=3, label=variant)
        ax.set_yscale("log")
        # Plain numbers (2, 3, 4, 6, 10, 20 ...) instead of "6 x 10^1" on a narrow log axis.
        plain = FuncFormatter(lambda v, _: f"{v:g}")
        ax.yaxis.set_major_formatter(plain)
        ax.yaxis.set_minor_formatter(plain)
        mark_cache_limits(ax)
        ax.set_xscale("log", base=2)
        ax.set_xlabel("n (elements)")
        ax.set_title(workload)
        ax.grid(True, which="both", color="#e0dfd8", linewidth=0.6)
        for side in ("top", "right"):
            ax.spines[side].set_visible(False)
    axes[0][0].set_ylabel("time per operation (ns)")
    axes[0][0].legend(fontsize=8, frameon=False)
    fig.suptitle("The four α(n) variants only (median; bars = IQR)")
    fig.tight_layout()
    out = FIGURES / "alpha_variants.png"
    fig.savefig(out, dpi=150)
    plt.close(fig)
    print(f"wrote {out}")


def plot_path_per_find() -> None:
    """H3a and H5: average path length per find, from the untimed counting run."""
    files = sorted(RESULTS.glob("counts*.csv"))
    if not files:
        return
    counts = pd.concat((pd.read_csv(f) for f in files), ignore_index=True)
    for workload, g in counts.groupby("workload"):
        fig, ax = plt.subplots(figsize=(7.5, 4.8))
        for variant, gv in g.groupby("variant"):
            gv = gv.sort_values("n")
            colour, ls, marker = style_for(variant)
            ax.plot(gv["n"], gv["path_per_find"], color=colour, linestyle=ls, marker=marker,
                    markersize=6, linewidth=2, label=variant)
        # Plain log scale: every value is > 0 (the smallest is about 0.5), and a
        # symlog axis would draw values below 1 on a linear scale, which reads wrongly.
        ax.set_yscale("log")
        mark_cache_limits(ax)
        ax.set_xlabel("n (elements)")
        ax.set_ylabel("average path length per find")
        ax.set_title(f"Work per find: {workload} (untimed counting run)")
        finish(ax, fig, f"path_per_find_{workload}.png")


if __name__ == "__main__":
    main()
