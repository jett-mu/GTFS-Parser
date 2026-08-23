import csv
import numpy as np
import matplotlib.pyplot as plt

INPUT_FILE = "data.csv"
OUTPUT_FILE = "delay_vs_time_of_week_mean.png"
BIN_MINUTES = 5
TZ_OFFSET_HOURS = -4

LINE_COLOR = "#4C72B0"
BAND_COLOR = "#4C72B0"
GRID_COLOR = "#D9D9D9"
TEXT_COLOR = "#333333"


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
    n_bins = 7 * 24 * 60 * 60 // bin_seconds
    bin_idx = np.clip(xs // bin_seconds, 0, n_bins - 1)

    bin_centers = (np.arange(n_bins) + 0.5) * bin_seconds
    mean = np.full(n_bins, np.nan)
    std = np.full(n_bins, np.nan)

    for b in range(n_bins):
        vals = ys[bin_idx == b]
        if len(vals) == 0:
            continue
        mean[b] = vals.mean()
        std[b] = vals.std()

    fig, ax = plt.subplots(figsize=(12, 6), dpi=150)
    ax.fill_between(bin_centers, mean - std, mean + std, color=BAND_COLOR, alpha=0.2, linewidth=0, label="Mean +/- 1 std")
    ax.plot(bin_centers, mean, color=LINE_COLOR, linewidth=1.5, label="Mean delay")

    ax.axhline(0, color=GRID_COLOR, linewidth=0.8)
    ax.set_xlim(0, 604800)
    ax.set_xlabel("Seconds since Monday 00:00 ET", color=TEXT_COLOR)
    ax.set_ylabel("Delay (s)", color=TEXT_COLOR)
    ax.set_title(f"Mean delay vs. time of week ({BIN_MINUTES}-min bins)", color=TEXT_COLOR)
    ax.legend(frameon=False, labelcolor=TEXT_COLOR)


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

    ax.grid(True, color=GRID_COLOR, linewidth=0.6)
    ax.set_axisbelow(True)
    for spine in ("top", "right"):
        ax.spines[spine].set_visible(False)
    for spine in ("left", "bottom"):
        ax.spines[spine].set_color(GRID_COLOR)

    ax.tick_params(colors=TEXT_COLOR)

    fig.tight_layout()
    fig.savefig(OUTPUT_FILE)
    print(f"Wrote {OUTPUT_FILE}")


if __name__ == "__main__":
    main()
