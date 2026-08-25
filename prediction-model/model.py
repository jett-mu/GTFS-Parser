import os
import pickle
import numpy as np
import pandas as pd
from sklearn.ensemble import HistGradientBoostingRegressor
from sklearn.model_selection import KFold, cross_val_score
from sklearn.metrics import mean_absolute_error

DATA_FILE = "combined_processed.csv"
WEIGHTS_PATH = "model.pkl"

# Weather columns (temperature/precipitation/rain/etc.) were tested and dropped:
# cross-validated MAE got WORSE with them included (51.2) than a plain "predict the
# mean" baseline (48.9). route_id/service_id/is_holiday are constant or redundant with
# day_of_week/is_weekend in this dataset, so they're excluded too.
# delay_lag_10m/30m (same block_id, i.e. same physical vehicle) correlate with delay
# (0.18 / 0.10) far more than any weather column did, so they're kept as real features.
# HistGradientBoostingRegressor handles their NaNs (no lag available yet in a block)
# natively via missing-value-aware splits, so they're passed through unfilled.
LAG_COLUMNS = ["delay_lag_10m", "delay_lag_30m"]
BINARY_COLUMNS = ["is_weekend"]
OUTPUT_COLUMN = "delay"

WEEK_SECONDS = 7 * 24 * 3600
DAY_SECONDS = 24 * 3600

BEST_PARAMS = dict(
    max_iter=100,
    learning_rate=0.05,
    max_depth=None,
    max_leaf_nodes=63,
    min_samples_leaf=5,
    l2_regularization=1e-2,
    random_state=0,
)

def load_data(data_file):
    df = pd.read_csv(data_file, dtype=str, keep_default_na=False)
    return df

def encode_time_batch(secs_since_last_monday_0000):
    t = secs_since_last_monday_0000 % WEEK_SECONDS
    week_angle = 2 * np.pi * t / WEEK_SECONDS
    day_angle = 2 * np.pi * (t % DAY_SECONDS) / DAY_SECONDS
    return np.stack(
        [np.sin(week_angle), np.cos(week_angle), np.sin(day_angle), np.cos(day_angle)],
        axis=1,
    )

def encode_time(secs_since_last_monday_0000):
    return encode_time_batch(np.array([float(secs_since_last_monday_0000)]))[0].tolist()

def build_feature_matrix(df):
    time_features = encode_time_batch(df["secs_since_last_monday_0000"].astype(np.float64).values)
    lags = df[LAG_COLUMNS].apply(pd.to_numeric, errors="coerce").astype(np.float32).values
    binary = df[BINARY_COLUMNS].astype(np.float32).values
    return np.hstack([time_features, lags, binary]).astype(np.float32)

FEATURE_NAMES = ["week_sin", "week_cos", "day_sin", "day_cos"] + LAG_COLUMNS + BINARY_COLUMNS

df = load_data(DATA_FILE)

X = build_feature_matrix(df)
y = df[OUTPUT_COLUMN].astype(np.float32).values

model = None
if os.path.exists(WEIGHTS_PATH):
    with open(WEIGHTS_PATH, "rb") as f:
        model = pickle.load(f)
    print(f"loaded model from {WEIGHTS_PATH}")

train = input("train a new model? [y/N]: ").strip().lower() == "y"

if train or model is None:
    kf = KFold(n_splits=5, shuffle=True, random_state=0)
    baseline_mae = np.mean([
        mean_absolute_error(y[va], np.full(len(va), y[tr].mean()))
        for tr, va in kf.split(X)
    ])
    cv_mae = -cross_val_score(
        HistGradientBoostingRegressor(**BEST_PARAMS), X, y,
        scoring="neg_mean_absolute_error", cv=kf,
    ).mean()
    print(f"baseline (predict mean) 5-fold MAE: {baseline_mae:.2f}")
    print(f"model 5-fold CV MAE: {cv_mae:.2f}")

    model = HistGradientBoostingRegressor(**BEST_PARAMS)
    model.fit(X, y)

    with open(WEIGHTS_PATH, "wb") as f:
        pickle.dump(model, f)
    print(f"model saved to {WEIGHTS_PATH}")

print("enter values for inference:")
secs_since_last_monday_0000 = input("secs_since_last_monday_0000: ")
lag_values = [float(input(f"{col} (blank if unknown): ") or "nan") for col in LAG_COLUMNS]
binary_values = [float(input(f"{col}: ")) for col in BINARY_COLUMNS]

values = encode_time(secs_since_last_monday_0000) + lag_values + binary_values

predicted_delay = model.predict([values])[0]
print(f"Predicted delay: {predicted_delay:.2f}")
