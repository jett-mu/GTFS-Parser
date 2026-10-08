# webserver

Flask server that bridges the C++ GTFS parsers to an HTML/JavaScript frontend. Runs on `http://localhost:5015`.

## Setup (Mac)

### Step 1 — Create a virtual environment

```zsh
python3 -m venv .venv
source .venv/bin/activate
```

### Step 2 — Install dependencies

```zsh
python3 -m pip install flask
```

### Step 3 — Compile C++ backends

The easiest way is the `Makefile` at the repo root, which builds the webserver tools (`static` target) and, once protobuf is set up (see [gtfs-rt README](../gtfs-rt/readme.md)), the GTFS-RT decoders (`rt` target) in one go:

```zsh
cd ..
make static   # webserver/tools/* (no protobuf needed)
make rt       # decodeTrip, decodeStop, decodeAlerts, ... (needs protobuf + pkg-config)
# or just `make` / `make all` to build both
```

To compile manually instead, from the `webserver/tools/` directory:

```zsh
find . -name "*.cpp" | xargs -I{} bash -c 'clang++ -std=c++17 -O3 -o "${0%.cpp}" "$0"' {}
```

### Step 4 — Compile GTFS-RT decoders

If you didn't use `make rt` above, follow the [gtfs-rt README](../gtfs-rt/readme.md) to build `decodeTrip`, `decodeStop`, and `decodeAlerts` manually.

### Step 5 — Configure and start the server

Make sure `config/config.json` exists (copy `config/config.example.json`) with your data path and GTFS-RT URLs. Then, from inside `webserver/` (tool paths are relative):

```zsh
python3 server.py
```

Open `http://localhost:5015` in your browser. A rebuilt C++ tool is picked up on the next request; no restart needed.

### Pages

| Path | Page |
|---|---|
| `/` | Main map + sidebar app |
| `/depbd` | Nearby departure board |
| `/rtebd` | Nearby route board |

For production (gunicorn + nginx) see [../NGINX.md](../NGINX.md).

## API Endpoints

| Method | Endpoint | Description |
|---|---|---|
| GET | `/api/trip/<trip_id>` | Schedule data for a trip |
| GET | `/api/stop/<stop_id>/<YYYY-MM-DD>` | Arrivals at a stop on a given date |
| GET | `/api/stopinfo/<stop_id>` | Static info for a stop |
| GET | `/api/searchstop/<query>` | Fuzzy stop search (name, ID or code) |
| GET | `/api/searchroute/<query>` | Fuzzy route search |
| GET | `/api/nearest/<lat>/<lon>` | Nearest stops to a coordinate |
| GET | `/api/route/<route_id>/<year>/<month>/<day>` | All trips for a route on a date |
| GET | `/api/rt/location/<trip_id>` | Live vehicle location for a trip |
| GET | `/api/rt/stop/<stop_id>` | Live arrivals at a stop |
| GET | `/api/rt/alerts` | All active service alerts |

Notes on responses:

- `/api/stop/...` returns departures sorted by arrival time. Each has `route_id` (the route's short name), `arrival_time`, `trip_id`, `trip_headsign`, `direction_id` and `route_color`.
- `/api/trip/...` returns `trip_id` and `route_id` as strings, and `route_color` as `#RRGGBB` (just `#` for routes without a colour; the frontend falls back to its accent colour).
- `/api/rt/alerts` is polled by the main page to fill the Alerts list in the sidebar and mobile tab.
- `/api/rt/*` routes need the GTFS-RT decoders built and the `rt_*_url` keys set in `config/config.json`.

The experimental server in [`../fast-static-gtfs`](../fast-static-gtfs/README.md) exposes a faster `/api/trip` and `/api/stop` on port 5016; the frontend does not use it.

For a list of what the web UI does, see [features.md](features.md).

## Token Authentication (optional)

`tokenserver.py` and `tksweb/` are generated from `server.py` and `web/`; never edit them by hand. After changing either source, from `webserver/` run:

```zsh
python3 tokenserver_converter.py
python3 tokenindex_converter.py
```

Create `t_confidental_info.py` (gitignored, never commit it) with your token:

```py
token = "your_secret_token"
```

Then start it (binds `0.0.0.0:5015`, `debug=False`):

```zsh
python3 tokenserver.py
```

Generate a secure token:

```zsh
python3 -c "import secrets; print(secrets.token_hex(32))"
```

Access the server with: `http://localhost:5015/?token=your_secret_token`

## Offline Map Tiles (optional)

Place map tiles in `tiles/<z>/<x>/<y>.png`. The server serves them at `/tiles/<z>/<x>/<y>.png` for use with a Leaflet or similar map frontend.
