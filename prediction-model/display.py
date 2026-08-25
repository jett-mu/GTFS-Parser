import matplotlib.pyplot as plt
import pandas as pd

DATA_FILE = "combined_processed.csv"

df = pd.read_csv(DATA_FILE)

df = df.sort_values("secs_since_last_monday_0000")

DAY_SECONDS = 24 * 3600
DAYS = ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"]
RUSH_HOURS = {"6am": 6, "12pm": 12, "6pm": 18, "12am": 0}

day_ticks = [d * DAY_SECONDS for d in range(len(DAYS) + 1)]
day_labels = DAYS + [""]

hour_ticks = [
    d * DAY_SECONDS + h * 3600
    for d in range(len(DAYS))
    for h in RUSH_HOURS.values()
]
hour_labels = [label for _ in range(len(DAYS)) for label in RUSH_HOURS.keys()]

fig, ax = plt.subplots(figsize=(14, 5))
ax.scatter(df["secs_since_last_monday_0000"], df["delay"], s=8, alpha=0.5)

ax.set_xticks(day_ticks)
ax.set_xticklabels(day_labels)
ax.grid(axis="x", which="major", color="black", linewidth=1.0, alpha=0.4)

ax.set_xticks(hour_ticks, minor=True)
ax.set_xticklabels(hour_labels, minor=True, fontsize=7, rotation=90)
ax.grid(axis="x", which="minor", color="gray", linewidth=0.5, linestyle="--", alpha=0.3)
ax.tick_params(axis="x", which="minor", pad=15)

ax.set_xlim(0, 7 * DAY_SECONDS)
ax.set_xlabel("time of week")
ax.set_ylabel("median delay (s)")
ax.set_title("median delay vs time of week route 601 yrt")
ax.axhline(0, color="gray", linewidth=0.8)
plt.tight_layout()
plt.savefig("delay_vs_unix_timestamp.png")
plt.show()
