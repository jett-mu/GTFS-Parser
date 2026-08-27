import argparse
import json
import os
import pickle

import numpy as np

MODEL_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "model.pkl")

WEEK_SECONDS = 7 * 24 * 3600
DAY_SECONDS = 24 * 3600

_model = None


def get_model(model_path=MODEL_PATH):
    global _model
    if _model is None:
        with open(model_path, "rb") as f:
            _model = pickle.load(f)
    return _model


def encode_time(secs_since_last_monday_0000):
    t = secs_since_last_monday_0000 % WEEK_SECONDS
    week_angle = 2 * np.pi * t / WEEK_SECONDS
    day_angle = 2 * np.pi * (t % DAY_SECONDS) / DAY_SECONDS
    return [np.sin(week_angle), np.cos(week_angle), np.sin(day_angle), np.cos(day_angle)]


def predict_delay(secs_since_last_monday_0000, delay_lag_10m, delay_lag_30m, is_weekend, model_path=MODEL_PATH):
    values = encode_time(float(secs_since_last_monday_0000)) + [
        float(delay_lag_10m) if delay_lag_10m is not None else np.nan,
        float(delay_lag_30m) if delay_lag_30m is not None else np.nan,
        float(is_weekend),
    ]
    model = get_model(model_path)
    return float(model.predict([values])[0])


def parse_args():
    parser = argparse.ArgumentParser(description="Predict delay (seconds) for a single set of feature values.")
    parser.add_argument("--secs", type=float, required=True, help="secs_since_last_monday_0000 (0-604800)")
    parser.add_argument("--lag10", type=float, default=None, help="delay_lag_10m in seconds (omit if unknown)")
    parser.add_argument("--lag30", type=float, default=None, help="delay_lag_30m in seconds (omit if unknown)")
    parser.add_argument("--weekend", type=int, choices=[0, 1], required=True, help="is_weekend (0 or 1)")
    parser.add_argument("--model", default=MODEL_PATH, help="path to model.pkl")
    return parser.parse_args()


def main():
    args = parse_args()
    delay = predict_delay(args.secs, args.lag10, args.lag30, args.weekend, args.model)
    print(json.dumps({"delay": delay}))


if __name__ == "__main__":
    main()
