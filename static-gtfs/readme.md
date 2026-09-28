# static-gtfs

C++ header library for parsing GTFS Schedule data. All functionality lives in `gtfs.hpp`. No build system required, just include it.

## Background

Transit agencies publish GTFS Schedule data as a `.zip` of `.csv`-formatted `.txt` files. The spec is [here](https://gtfs.org/documentation/schedule/reference/). For larger agencies (major cities), files like `stop_times.txt` can exceed 200,000 lines.

**Required files** (agency must provide):

| File | Contents |
|---|---|
| `agency.txt` | Agency info and `agency_id` |
| `routes.txt` | Route info and `route_id` |
| `trips.txt` | Trip info and `trip_id` |
| `stop_times.txt` | Scheduled times, references `trip_id` |

**Common optional files:**

| File | Contents |
|---|---|
| `stops.txt` | Stop locations and `stop_id` (required unless `location.geojson` is provided) |
| `calendar.txt` | Weekly service schedules |
| `calendar_dates.txt` | Service exceptions (added/removed service days) |
| `fare_attributes.txt` | Fare pricing and payment info |
| `frequencies.txt` | Headway-based frequency info |
| `feed_info.txt` | Feed validity dates |

## Setup

### 1. Add your GTFS data

Create a `data/` folder at the repo root and extract your agency's `.zip` there:

```
GTFS Parser/
├── static-gtfs/
│   └── gtfs.hpp
├── config/
│   ├── config.hpp
│   └── config.json
└── data/
    └── your_agency/
        ├── agency.txt
        ├── routes.txt
        ├── trips.txt
        ├── stop_times.txt
        └── ...
```

### 2. Point `config/config.json` at it

`config/config.json` is machine-specific (absolute path):

```json
{
  "config_version": 3,
  "absolute_path_to_data": "/path/to/GTFS Parser/data/your_agency/"
}
```

`config/config.hpp` reads it and returns a `gtfs::data_feed`, which every `gtfs::` query takes as its first argument. The config file is found in the current directory or the nearest parent (`config/config.json` or `config.json`), or via the `GTFS_CONFIG` environment variable.

### 3. Verify the setup

Create a `main.cpp` at the repo root:

```cpp
#include <iostream>
#include <ctime>
#include "config/config.hpp"

int main() {
    const gtfs::data_feed feed = config::load();
    std::time_t t = std::time(nullptr);
    std::tm* now = std::localtime(&t);
    std::cout << gtfs::verifyGTFS(feed, now->tm_year + 1900, now->tm_mon + 1, now->tm_mday) << std::endl;
    return 0;
}
```

Compile and run:

```zsh
clang++ -std=c++17 -O3 main.cpp -o main && ./main
```

`verifyGTFS` return values:

| Output | Meaning |
|---|---|
| `0` | Data read and valid |
| `1` | Data read but feed is expired |
| `-1` | Data read but feed has not started yet |
| `10` | `feed_info.txt` not found |
| `11` | `feed_info.txt` missing `feed_end_date` |
| `12` | `feed_info.txt` missing `feed_start_date` |

### 4. Build the CLI

Once `config/config.json` is set up, the repo-root `Makefile` builds `gtfs_cli` (and the webserver tools that depend on `gtfs.hpp`) for you:

```zsh
cd ..
make static
```

`static-gtfs/gtfs_cli` is the compiled CLI checked into git — after editing `gtfs.hpp` or `gtfs_cli.cpp`, rerun `make static` (or compile manually, see repo-root README) and commit the updated binary.
