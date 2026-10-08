# GTFS Parser

A C++ library and Python webserver for parsing and serving **GTFS Schedule** and **GTFS Realtime** transit data.

**GTFS** (General Transit Feed Specification) is the worldwide standard for transit agency schedule and location data. See the [official reference](https://gtfs.org/documentation/schedule/reference/) and the [`google/transit`](https://github.com/google/transit) repository.

## Components

| Directory | Description |
|---|---|
| [`static-gtfs/`](static-gtfs/) | C++ header (`gtfs.hpp`) for parsing GTFS Schedule `.txt` files |
| [`gtfs-rt/`](gtfs-rt/) | C++ tools for decoding GTFS Realtime `.pb` protobuf files |
| [`webserver/`](webserver/) | Flask server + HTML frontend that exposes both as a REST API |
| [`fast-static-gtfs/`](fast-static-gtfs/README.md) | Experimental indexed/hashed query layer (`fast-gtfs.hpp`) and a standalone C++ HTTP server on port 5016 |
| [`config/`](config/) | `config.hpp` + `config.json`: data folder path and GTFS-RT feed URLs shared by every tool |
| `data-collection/`, `prediction-model/`, `other-testing/` | Scratch/experimental work (RT data logging, ML delay prediction, C++ demos) — not part of the core library |

Deploying the Flask app behind nginx + gunicorn is covered in [NGINX.md](NGINX.md). The web UI's feature list is in [webserver/features.md](webserver/features.md), and [webserver/to_tokenserver.md](webserver/to_tokenserver.md) explains the token-gated variant.

## Quick Start

```bash
git clone https://github.com/JettM9104/GTFS-Parser/
```

Create your local config (gitignored, machine-specific) and put your GTFS feed in `data/`:

```bash
cp config/config.example.json config/config.json
# edit absolute_path_to_data (and the rt_*_url fields if you use GTFS-RT)
```

Then follow the README in whichever component you need:

1. [static-gtfs](static-gtfs/readme.md) — parse schedule data
2. [gtfs-rt](gtfs-rt/readme.md) — decode realtime feeds
3. [webserver](webserver/README.md) — run the full web UI
4. [fast-static-gtfs](fast-static-gtfs/README.md) — optional indexed server on port 5016

## Build via Makefile

Once `config/config.json` points at your GTFS data folder (see [static-gtfs](static-gtfs/readme.md)) and, if you need GTFS-RT, protobuf is installed (see [gtfs-rt](gtfs-rt/readme.md)), a `Makefile` at the repo root builds the compiled tools for you instead of compiling each one by hand:

```bash
make              # same as `make all`
make all          # everything: static + rt + fast (needs protobuf + pkg-config)
make static       # gtfs_cli + webserver tools only — no protobuf needed
make rt           # decodeTrip/Stop/Alerts, routeVehicles, routeMedianDelay — needs protobuf + pkg-config
make fast         # fast-static-gtfs/webserver (port 5016) — no protobuf needed
make clean        # remove the binaries make builds
```

It only rebuilds a binary when its source is newer than the compiled binary. It excludes pure scratch/demo binaries (`static-gtfs/testing`, `static-gtfs/flag_finder`, `gtfs-rt/testing/*`, `other-testing/varadic`) — compile those manually if needed.

## Requirements

- C++17 compiler (`clang++` or `g++`)
- Python 3 with `flask` (webserver)
- `wget` (GTFS-RT decoders download feeds with it)
- `protobuf` (latest, currently 35.x) + `pkg-config` (for GTFS-RT only)
- `git`

## Versions

| Release | Notes |
|---|---|
| v1.8.0 | Stable — reorganized into `gtfs.hpp`, improved GTFS-RT (JSON output) |
| v3.0 beta | HTML GUI via Flask + JavaScript frontend |
