import numpy as np
import pandas as pd

DATA_FILE = "combined_processed.csv"
OUTPUT_COLUMN = "delay"
LAG_COLUMNS = ["delay_lag_10m", "delay_lag_30m"]
BINARY_COLUMNS = ["is_weekend"]

WEEK_SECONDS = 7 * 24 * 3600
DAY_SECONDS = 24 * 3600


def encode_time_batch(secs_since_last_monday_0000):
    t = secs_since_last_monday_0000 % WEEK_SECONDS
    week_angle = 2 * np.pi * t / WEEK_SECONDS
    day_angle = 2 * np.pi * (t % DAY_SECONDS) / DAY_SECONDS
    return np.stack(
        [np.sin(week_angle), np.cos(week_angle), np.sin(day_angle), np.cos(day_angle)],
        axis=1,
    )


def build_feature_matrix(df):
    time_features = encode_time_batch(df["secs_since_last_monday_0000"].astype(np.float64).values)
    lags = df[LAG_COLUMNS].apply(pd.to_numeric, errors="coerce").astype(np.float32).values
    binary = df[BINARY_COLUMNS].astype(np.float32).values
    return np.hstack([time_features, lags, binary]).astype(np.float32)


FEATURE_NAMES = ["week_sin", "week_cos", "day_sin", "day_cos"] + LAG_COLUMNS + BINARY_COLUMNS

df = pd.read_csv(DATA_FILE, dtype=str, keep_default_na=False)
X = build_feature_matrix(df)
y = df[OUTPUT_COLUMN].astype(np.float32).values

print(f"correlation of each model feature with '{OUTPUT_COLUMN}' ({DATA_FILE}, n={len(y)}):\n")

rows = []
for i, name in enumerate(FEATURE_NAMES):
    col = X[:, i]
    mask = ~np.isnan(col)
    r = np.corrcoef(col[mask], y[mask])[0, 1]
    rows.append((name, r, mask.sum()))

rows.sort(key=lambda r: -abs(r[1]))
for name, r, n in rows:
    print(f"{name:<16} r = {r:+.4f}   (n={n})")
