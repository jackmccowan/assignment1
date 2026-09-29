"""Plot benchmark results. Reads results/bench*.csv; writes report/figures/*.png.

One figure per workload: median ns/op vs n, log-log.
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

    # Median over repetitions is robust to one-off OS noise.
    med = df.groupby(["workload", "variant", "n"], as_index=False)["ns_per_op"].median()

    FIGURES.mkdir(parents=True, exist_ok=True)
    for workload, g in med.groupby("workload"):
        fig, ax = plt.subplots(figsize=(7.5, 4.8))
        for variant, gv in g.groupby("variant"):
            gv = gv.sort_values("n")
            colour, ls, marker = style_for(variant)
            ax.plot(gv["n"], gv["ns_per_op"], color=colour, linestyle=ls, marker=marker,
                    markersize=6, linewidth=2, label=variant)
        ax.set_xscale("log", base=2)
        ax.set_yscale("log")
        ax.set_xlabel("n (elements)")
        ax.set_ylabel("median time per operation (ns)")
        ax.set_title(f"Workload: {workload}")
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
