#!/usr/bin/env python3

import csv
import json
import ssl
import subprocess
import time
import urllib.error
import urllib.request
from datetime import datetime, timezone
from pathlib import Path

import certifi

SSL_CONTEXT = ssl.create_default_context(cafile=certifi.where())

SCRIPT_DIR = Path(__file__).resolve().parent
COLLECT_BIN = SCRIPT_DIR / "collect"
GTFS_JSON = SCRIPT_DIR / "decoded_gtfs.json"
TABLE_CSV = SCRIPT_DIR / "table.csv"

INTERVAL_SECONDS = 5 * 60

WEATHER_URL = (
    "https://api.open-meteo.com/v1/forecast"
    "?latitude=43.87&longitude=-79.44"
    "&current=temperature_2m,precipitation,rain,showers,snowfall"
)

CSV_HEADER = [
    "trip_id",
    "delay",
    "timestamp",
    "temperature",
    "precipitation",
    "rain",
    "showers",
    "snowfall",
]


def run_collect():
    subprocess.run([str(COLLECT_BIN)], cwd=SCRIPT_DIR, check=True)


WEATHER_RETRY_SECONDS = 5


def fetch_weather():
    while True:
        try:
            with urllib.request.urlopen(WEATHER_URL, context=SSL_CONTEXT) as response:
                data = json.loads(response.read())
            break
        except urllib.error.HTTPError as e:
            if e.code != 503:
                raise
            print(f"Weather API 503, retrying in {WEATHER_RETRY_SECONDS}s...")
            time.sleep(WEATHER_RETRY_SECONDS)
    current = data["current"]
    return {
        "temperature": current["temperature_2m"],
        "precipitation": current["precipitation"],
        "rain": current["rain"],
        "showers": current["showers"],
        "snowfall": current["snowfall"],
    }


def extract_trip_rows(weather):
    with open(GTFS_JSON) as f:
        data = json.load(f)

    timestamp = datetime.fromtimestamp(
        int(data["header"]["timestamp"]), tz=timezone.utc
    ).strftime("%Y-%m-%dT%H:%M:%S+00:00")

    rows = []
    for entity in data.get("entity", []):
        trip_update = entity.get("trip_update")
        if not trip_update:
            continue

        trip_id = trip_update.get("trip", {}).get("trip_id")
        stop_time_updates = trip_update.get("stop_time_update", [])
        if not stop_time_updates:
            continue

        first_stop = stop_time_updates[0]
        event = first_stop.get("arrival") or first_stop.get("departure")
        if not event:
            continue

        rows.append({
            "trip_id": trip_id,
            "delay": event.get("delay"),
            "timestamp": timestamp,
            **weather,
        })

    return rows


def append_to_csv(rows):
    file_exists = TABLE_CSV.exists()
    with open(TABLE_CSV, "a", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=CSV_HEADER)
        if not file_exists:
            writer.writeheader()
        writer.writerows(rows)


def run_once():
    run_collect()
    weather = fetch_weather()
    rows = extract_trip_rows(weather)
    append_to_csv(rows)
    timestamp = rows[0]["timestamp"] if rows else "n/a"
    print(f"Appended {len(rows)} rows to {TABLE_CSV.name} (timestamp: {timestamp})")


def seconds_until_next_interval():
    now = time.time()
    return INTERVAL_SECONDS - (now % INTERVAL_SECONDS)


def main():
    while True:
        time.sleep(seconds_until_next_interval())
        try:
            run_once()
        except Exception as e:
            print(f"Error during collection cycle: {e}")


if __name__ == "__main__":
    main()
