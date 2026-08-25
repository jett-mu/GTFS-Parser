import csv

import pandas as pd

INPUT_FILE = "combined.csv"
OUTPUT_FILE = "combined_processed.csv"


EXPECTED_COLS = ["trip_id", "delay", "timestamp", "temperature", "precipitation", "rain", "showers", "snowfall"]


def load_data(path):
    with open(path, newline="") as f:
        reader = csv.reader(f)
        header = next(reader)
        assert header[: len(EXPECTED_COLS)] == EXPECTED_COLS, f"unexpected header: {header}"
        rows = [row[: len(EXPECTED_COLS)] for row in reader]
    df = pd.DataFrame(rows, columns=EXPECTED_COLS)
    df["delay"] = df["delay"].astype(int)
    for col in ["temperature", "precipitation", "rain", "showers", "snowfall"]:
        df[col] = df[col].astype(float)
    return df

GTFS_DIR = "/Users/jettmu/Documents/VSCode/GTFS Parser/static-gtfs/data/yrt_archive"
TRIPS_FILE = f"{GTFS_DIR}/trips.txt"
CALENDAR_DATES_FILE = f"{GTFS_DIR}/calendar_dates.txt"
AGENCY_TIMEZONE = "America/Toronto"
TARGET_ROUTE_ID = "601"

DELAY_MAX_ABS = 600


ROLLING_WINDOWS = {
    "1hr": 4,
    "3hr": 12,
    "7hr": 28,
}
WEATHER_COLS = ["rain", "snowfall", "precipitation"]

# Lagged delay keyed by block_id, not trip_id: a block is the sequence of trips one
# physical vehicle runs in a day, and pings land on a ~5min grid. Median observations
# per group in this dataset are ~54/trip_id vs ~782/block_id, so trip_id-level lookback
# is mostly missing data while block_id also captures a vehicle's lateness carrying over
# between trips (which trip_id can't, since it resets at each trip boundary).
LAG_WINDOWS = {"10m": 10, "30m": 30}
LAG_TOLERANCE = pd.Timedelta("3min")


def load_trips(trips_file):
    return pd.read_csv(trips_file, dtype=str)


def load_holiday_exceptions(calendar_dates_file):
    cal_dates = pd.read_csv(calendar_dates_file, dtype=str)
    return set(zip(cal_dates["service_id"], cal_dates["date"]))


def add_calendar_fields(df, trips, holiday_exceptions):
    trip_to_service = dict(zip(trips["trip_id"], trips["service_id"]))
    df["service_id"] = df["trip_id"].map(trip_to_service)

    local_ts = pd.to_datetime(df["timestamp"], utc=True).dt.tz_convert(AGENCY_TIMEZONE)
    df["day_of_week"] = local_ts.dt.weekday
    df["is_weekend"] = df["day_of_week"].isin([5, 6]).astype(int)

    local_date = local_ts.dt.strftime("%Y%m%d")
    df["is_holiday"] = [
        int(pair in holiday_exceptions) for pair in zip(df["service_id"], local_date)
    ]
    return df


def add_unix_timestamp(df):
    local_ts = pd.to_datetime(df["timestamp"], utc=True).dt.tz_convert(AGENCY_TIMEZONE)
    last_monday = (local_ts - pd.to_timedelta(local_ts.dt.weekday, unit="D")).dt.normalize()
    df["secs_since_last_monday_0000"] = (local_ts - last_monday).dt.total_seconds().astype("int64")
    return df


def add_rolling_weather(df):
    weather = (
        df[["timestamp"] + WEATHER_COLS]
        .drop_duplicates(subset="timestamp")
        .sort_values("timestamp")
        .set_index("timestamp")
    )

    for col in WEATHER_COLS:
        for label, window in ROLLING_WINDOWS.items():
            weather[f"{col}_sum_{label}"] = (
                weather[col].rolling(window=window, min_periods=1).sum()
            )

    rolling_cols = [f"{col}_sum_{label}" for col in WEATHER_COLS for label in ROLLING_WINDOWS]
    return df.merge(weather[rolling_cols], left_on="timestamp", right_index=True, how="left")


def add_delay_lags(df):
    base = df[["block_id", "timestamp", "delay"]].copy()
    base["ts"] = pd.to_datetime(base["timestamp"], utc=True)

    query = pd.DataFrame({
        "block_id": df["block_id"],
        "ts": pd.to_datetime(df["timestamp"], utc=True),
        "_row": df.index,
    })

    for label, minutes in LAG_WINDOWS.items():
        left = query.copy()
        left["target_ts"] = left["ts"] - pd.Timedelta(minutes=minutes)
        left = left.sort_values("target_ts")

        right = base.rename(columns={"ts": "target_ts"})[["block_id", "target_ts", "delay"]]
        right = right.sort_values("target_ts")

        merged = pd.merge_asof(
            left, right,
            on="target_ts", by="block_id",
            direction="backward", tolerance=LAG_TOLERANCE,
        )
        df[f"delay_lag_{label}"] = merged.set_index("_row")["delay"].reindex(df.index).values

    return df


def collapse_to_median_delay(df):
    other_cols = [col for col in df.columns if col not in ("timestamp", "delay", "trip_id")]
    agg = {"delay": "median", **{col: "first" for col in other_cols}}
    return df.groupby("timestamp", as_index=False).agg(agg)


def main():
    df = load_data(INPUT_FILE)

    trips = load_trips(TRIPS_FILE)
    trip_to_route = dict(zip(trips["trip_id"], trips["route_id"]))
    df["route_id"] = df["trip_id"].map(trip_to_route)
    df = df[df["route_id"] == TARGET_ROUTE_ID].copy()

    df = df[df["delay"].abs() <= DELAY_MAX_ABS].copy()

    df["snowfall"] = df["snowfall"] * 10

    df = df.sort_values("timestamp").reset_index(drop=True)
    df = add_unix_timestamp(df)
    df = add_rolling_weather(df)

    holiday_exceptions = load_holiday_exceptions(CALENDAR_DATES_FILE)
    df = add_calendar_fields(df, trips, holiday_exceptions)

    trip_to_block = dict(zip(trips["trip_id"], trips["block_id"]))
    df["block_id"] = df["trip_id"].map(trip_to_block)
    df = add_delay_lags(df)
    df = df.drop(columns=["block_id"])

    df = collapse_to_median_delay(df)

    float_cols = df.select_dtypes(include="float").columns
    df[float_cols] = df[float_cols].round(4)

    df.to_csv(OUTPUT_FILE, index=False)
    print(f"Wrote {len(df)} rows for route {TARGET_ROUTE_ID} to {OUTPUT_FILE}.")


if __name__ == "__main__":
    main()
