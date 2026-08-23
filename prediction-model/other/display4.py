import csv
import numpy as np
import matplotlib.pyplot as plt

INPUT_FILE = "data.csv"
OUTPUT_FILE = "delay_vs_time_of_week_heatmap.png"

XBINS = 168
YBINS = 120
TZ_OFFSET_HOURS = -4

TEXT_COLOR = "#333333"
GRID_COLOR = "#D9D9D9"
CMAP = "RdYlGn_r"


def load(path):
    xs, ys = [], []
    with open(path, newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            xs.append((int(row["unix_timestamp"]) + TZ_OFFSET_HOURS * 3600) % 604800)
            ys.append(int(row["delay"]))
    return np.array(xs), np.array(ys)


def main():
    xs, ys = load(INPUT_FILE)

    fig, ax = plt.subplots(figsize=(12, 6), dpi=150)

    y_lo, y_hi = np.percentile(ys, [0.5, 99.5])
    hist, xedges, yedges = np.histogram2d(
        xs, ys, bins=[XBINS, YBINS], range=[[0, 604800], [y_lo, y_hi]]
    )

    mesh = ax.pcolormesh(
        xedges, yedges, hist.T, cmap=CMAP, norm="log", shading="flat"
    )
    cbar = fig.colorbar(mesh, ax=ax, pad=0.01)
    cbar.set_label("Count", color=TEXT_COLOR)
    cbar.ax.tick_params(colors=TEXT_COLOR)

    ax.set_xlim(0, 604800)
    ax.set_xlabel("Seconds since Monday 00:00 ET", color=TEXT_COLOR)
    ax.set_ylabel("Delay (s)", color=TEXT_COLOR)
    ax.set_title(f"Delay vs. time of week heatmap ({len(xs):,} points)", color=TEXT_COLOR)

    days = ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"]
    hour_step = 6
    ticks = [i * hour_step * 3600 for i in range(int(7 * 24 / hour_step) + 1)]
    labels = []
    for t in ticks:
        day = days[(t // 86400) % 7]
        hour = (t % 86400) // 3600
        labels.append(f"{day}\n{hour:02d}:00" if hour == 0 else f"{hour:02d}:00")
    ax.set_xticks(ticks)
    ax.set_xticklabels(labels, fontsize=8)

    ax.grid(True, color="black", linewidth=0.7, alpha=0.9, zorder=3)
    ax.set_axisbelow(False)

    for spine in ("top", "right"):
        ax.spines[spine].set_visible(False)
    for spine in ("left", "bottom"):
        ax.spines[spine].set_color(GRID_COLOR)

    ax.tick_params(colors=TEXT_COLOR)

    fig.tight_layout()
    fig.savefig(OUTPUT_FILE)
    print(f"Wrote {OUTPUT_FILE} ({len(xs):,} points)")


if __name__ == "__main__":
    main()
