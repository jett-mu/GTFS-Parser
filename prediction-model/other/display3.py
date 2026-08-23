import csv
import numpy as np
import matplotlib.pyplot as plt

INPUT_FILE = "data.csv"
OUTPUT_FILE = "delay_heatmap_day_hour.png"
AGG = "mean"
BIN_MINUTES = 5
TZ_OFFSET_HOURS = -4

TEXT_COLOR = "#333333"
GRID_COLOR = "#D9D9D9"

DAYS = ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"]


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

    bin_seconds = BIN_MINUTES * 60
    bins_per_day = 24 * 60 * 60 // bin_seconds

    day_idx = np.clip(xs // 86400, 0, 6)
    slot_idx = np.clip((xs % 86400) // bin_seconds, 0, bins_per_day - 1)

    grid = np.full((7, bins_per_day), np.nan)
    for d in range(7):
        for s in range(bins_per_day):
            vals = ys[(day_idx == d) & (slot_idx == s)]
            if len(vals) == 0:
                continue
            grid[d, s] = np.median(vals) if AGG == "median" else vals.mean()

    limit = np.nanmax(np.abs(grid))

    fig, ax = plt.subplots(figsize=(14, 5), dpi=150)
    im = ax.imshow(grid, cmap="RdBu_r", vmin=-limit, vmax=limit, aspect="auto")

    tick_step = max(1, 60 // BIN_MINUTES)
    ticks = list(range(0, bins_per_day, tick_step))
    labels = [f"{(t * BIN_MINUTES) // 60:02d}:{(t * BIN_MINUTES) % 60:02d}" for t in ticks]
    ax.set_xticks(ticks)
    ax.set_xticklabels(labels, fontsize=7, rotation=90)
    ax.set_yticks(range(7))
    ax.set_yticklabels(DAYS)
    ax.set_xlabel("Time of day", color=TEXT_COLOR)
    ax.set_ylabel("Day of week", color=TEXT_COLOR)
    ax.set_title(f"{AGG.capitalize()} delay by day-of-week x time-of-day ({BIN_MINUTES}-min bins)", color=TEXT_COLOR)

    cbar = fig.colorbar(im, ax=ax, pad=0.02)
    cbar.set_label("Delay (s)", color=TEXT_COLOR)
    cbar.ax.tick_params(colors=TEXT_COLOR)

    ax.tick_params(colors=TEXT_COLOR)
    for spine in ax.spines.values():
        spine.set_visible(False)

    fig.tight_layout()
    fig.savefig(OUTPUT_FILE)
    print(f"Wrote {OUTPUT_FILE}")


if __name__ == "__main__":
    main()
