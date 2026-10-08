# fast-static-gtfs

Experimental indexed query layer for GTFS Schedule data, plus a standalone C++ HTTP server built on it. It exists to avoid the per-call linear scans that `static-gtfs/gtfs.hpp` does: each needed `.txt` file is copied into a sorted variant (`*_trip_id.txt`, `*_stop_id.txt`, ...) keyed on the column that gets looked up, and queries binary-search those copies. Still a work in progress.

| File | Purpose |
|---|---|
| `fast-gtfs.hpp` | `fast_gtfs` namespace: `fast_data_feed`, file sorting/verification, binary-search line maps. Avoid `using namespace fast_gtfs` together with `using namespace gtfs`, since names can collide |
| `webservermethods.hpp` | JSON builders for the HTTP handlers (`getTrip`, `getStopDayTimes`) |
| `webserver.cpp` | Server using the vendored `httplib.h`, listening on `0.0.0.0:5016` |
| `test-data/` | Small sample feed with its sorted variants, used for ad-hoc testing |

## Build

From the repo root (needs only a C++17 compiler, no protobuf):

```zsh
make fast
```

This builds `fast-static-gtfs/webserver` and rebuilds it when `webserver.cpp`, `fast-gtfs.hpp`, `webservermethods.hpp`, `httplib.h`, `gtfs.hpp` or `config.hpp` changes.

## Run

The server reads the feed at `absolute_path_to_data` in `config/config.json` (see [static-gtfs](../static-gtfs/readme.md)).

```zsh
./fast-static-gtfs/webserver
```

On start it tries to load the sorted files. If they are missing or stale it warns and starts without data; requests then return `503 {"error":"data not loaded"}`. Type a command into the server's console:

| Command | Effect |
|---|---|
| `sort` | Rebuild every sorted file from the originals, then load them |
| `verify` | Report whether each sorted file exists and is actually sorted |
| `load` | Load the sorted files (does nothing if any is missing or unsorted) |

While a command runs, requests get `503 {"error":"server busy: ..."}`. `calendar.txt` and `calendar_dates.txt` are optional, so a feed without them is skipped rather than rejected.

Re-run `sort` whenever the source feed changes. Before starting it, check that nothing is already on port 5016 (`lsof -i :5016`), because stale instances can stack up and serve old binaries.

## API

| Method | Endpoint | Description |
|---|---|---|
| GET | `/api/trip/<trip_id>` | Trip with its stops and shape |
| GET | `/api/stop/<stop_id>/<year>/<month>/<day>` | Departures at a stop on a date (each entry includes `direction_id`) |

Each request logs its handling time in microseconds to the console.
