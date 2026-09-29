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


if __name__ == "__main__":
    main()
