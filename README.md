# GTFS Parser

A C++ library and Python webserver for parsing and serving **GTFS Schedule** and **GTFS Realtime** transit data.

**GTFS** (General Transit Feed Specification) is the worldwide standard for transit agency schedule and location data. See the [official reference](https://gtfs.org/documentation/schedule/reference/) and the [`google/transit`](https://github.com/google/transit) repository.

<img width="1914" height="956" alt="image" src="https://github.com/user-attachments/assets/08733e08-7a5b-4e54-ba88-a782d231d065" />

## Components

| Directory | Description |
|---|---|
| [`static-gtfs/`](static-gtfs/) | C++ header (`gtfs.hpp`) for parsing GTFS Schedule `.txt` files |
| [`gtfs-rt/`](gtfs-rt/) | C++ tools for decoding GTFS Realtime `.pb` protobuf files |
| [`webserver/`](webserver/) | Flask server + HTML frontend that exposes both as a REST API |

## Quick Start

```bash
git clone https://github.com/JettM9104/GTFS-Parser/
```

Then follow the README in whichever component you need:

1. [static-gtfs](static-gtfs/readme.md) — parse schedule data
2. [gtfs-rt](gtfs-rt/readme.md) — decode realtime feeds
3. [webserver](webserver/README.md) — run the full web UI

## Build via Makefile

Once `static-gtfs/config.hpp` is set up from its template (see [static-gtfs](static-gtfs/readme.md)) and, if you need GTFS-RT, protobuf is installed (see [gtfs-rt](gtfs-rt/readme.md)), a `Makefile` at the repo root builds the compiled tools for you instead of compiling each one by hand:

```bash
make              # same as `make all`
make all          # everything: static-gtfs, webserver tools, and GTFS-RT decoders
make static       # static-gtfs + webserver tools only — no protobuf needed
make rt           # GTFS-RT decoders only — needs protobuf + pkg-config
make clean        # remove the binaries make builds
```

It only rebuilds a binary when its source is newer than the compiled binary. It excludes pure scratch/demo binaries (e.g. `static-gtfs/testing`) — compile those manually if needed.

## Requirements

- C++17 compiler (`clang++` or `g++`)
- Python 3 with `flask`
- `protobuf` (latest, currently 35.x) + `pkg-config` (for GTFS-RT only)
- `git`

## Versions

| Release | Notes |
|---|---|
| v1.8.0 | Stable — reorganized into `gtfs.hpp`, improved GTFS-RT (JSON output) |
| v3.0 beta | HTML GUI via Flask + JavaScript frontend |
