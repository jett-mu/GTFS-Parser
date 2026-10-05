//
// Created by Jett Mu on 2026-08-22.
//

#include <algorithm>
#include <atomic>
#include <cstring>
#include <iomanip>
#include <chrono>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include "../static-gtfs/gtfs.hpp"
#include "webservermethods.hpp"
#include "httplib.h"
#include "../config/config.hpp"

using namespace httplib;

vector<pair<string, vector<string>>> triplines;
std::unordered_map<string, int> triprefs;

vector<pair<string, vector<string>>> stoplines;
std::unordered_map<string, int> stoprefs;

vector<pair<string, vector<string>>> stoptimesstoplines;
std::unordered_map<string, int> stoptimesstoprefs;

vector<pair<string, vector<string>>> shapelines;
std::unordered_map<string, int> shaperefs;

vector<pair<string, vector<string>>> stoptimesstopidlines;
std::unordered_map<string, int> stoptimesstopidrefs;

vector<pair<string, vector<string>>> calendarlines;
std::unordered_map<string, int> calendarrefs;

vector<pair<string, vector<string>>> calendardatelines;
std::unordered_map<string, int> calendardaterefs;

vector<pair<string, vector<string>>> routelines;
std::unordered_map<string, int> routerefs;

struct sorted_table {
    const string& source;      // original GTFS file
    const string& sorted;      // sorted copy that gets loaded
    const char* key;           // column the copy is sorted by
    vector<pair<string, vector<string>>>& lines;
    std::unordered_map<string, int>& refs;
};

std::shared_mutex dataMutex; // handlers hold it shared, load() holds it exclusive
std::atomic<bool> loaded{false};
std::atomic<const char*> busyWith{nullptr}; // console command in progress ("sorting", ...); requests get a 503 meanwhile

// marks the server busy for the lifetime of a console command
struct BusyScope {
    explicit BusyScope(const char* what) { busyWith = what; }
    ~BusyScope() { busyWith = nullptr; }
};

const char* RED = "\033[31m";
const char* GREEN = "\033[32m";
const char* RESET = "\033[0m";

using clock_type = std::chrono::steady_clock;
double ms(clock_type::duration d) { return std::chrono::duration<double, std::milli>(d).count(); }
string fileName(const string& p) { return p.substr(p.find_last_of('/') + 1); }

void warn(const string& msg) { std::cout << RED << "WARNING: " << msg << RESET << "\n"; }

vector<sorted_table> tables(const fast_gtfs::fast_data_feed& df) {
    return {
        {df.fast_stop_path, df.fast_stop_stop_id, "stop_id", stoplines, stoprefs},
        {df.fast_stop_times_path, df.fast_stop_times_trip_id, "trip_id", stoptimesstoplines, stoptimesstoprefs},
        {df.fast_stop_times_path, df.fast_stop_times_stop_id, "stop_id", stoptimesstopidlines, stoptimesstopidrefs},
        {df.fast_shape_path, df.fast_shape_shape_id, "shape_id", shapelines, shaperefs},
        {df.fast_trip_path, df.fast_trip_trip_id, "trip_id", triplines, triprefs},
        {df.fast_calendar_path, df.fast_calendar_service_id, "service_id", calendarlines, calendarrefs},
        {df.fast_calendar_dates_path, df.fast_calendar_dates_service_id, "service_id", calendardatelines, calendardaterefs},
        {df.fast_route_path, df.fast_route_route_id, "route_id", routelines, routerefs},
    };
}

// "verify": report whether each sorted file exists and is sorted. Returns true if all are present and sorted.
bool verify(const fast_gtfs::fast_data_feed& df) {
    bool ok = true;
    for (const auto& t : tables(df)) {
        const bool exists = fast_gtfs::bin_search::fileExists(t.sorted);
        const bool sorted = exists && fast_gtfs::bin_search::isSorted(t.sorted, t.key);
        const string name = t.sorted.substr(t.sorted.find_last_of('/') + 1);
        std::cout << name << " (" << t.key << "): "
                  << (exists ? GREEN : RED) << "exists " << (exists ? "yes" : "no") << RESET << ", "
                  << (sorted ? GREEN : RED) << "sorted " << (sorted ? "yes" : "no") << RESET << "\n";
        if (!exists || !sorted) ok = false;
    }
    return ok;
}

// "load": load the sorted files if they all exist and are sorted, otherwise warn and change nothing.
bool load(const fast_gtfs::fast_data_feed& df) {
    const auto all = tables(df);
    auto start = clock_type::now();
    for (const auto& t : all) {
        if (!fast_gtfs::bin_search::fileExists(t.sorted)) {
            warn(t.sorted + " not found, not loading (run \"sort\")");
            return false;
        }
        if (!fast_gtfs::bin_search::isSorted(t.sorted, t.key)) {
            warn(t.sorted + " is not sorted, not loading (run \"sort\")");
            return false;
        }
    }
    std::cout << "checked " << all.size() << " files  " << std::fixed << std::setprecision(1)
              << ms(clock_type::now() - start) << " ms\n";

    size_t width = 0;
    for (const auto& t : all) width = std::max(width, fileName(t.sorted).size() + strlen(t.key) + 3);

    std::cout << "loading " << all.size() << " files\n";
    {
        std::unique_lock lock(dataMutex);
        for (const auto& t : all) {
            const string label = fileName(t.sorted) + " (" + t.key + ")";
            std::cout << "  " << std::left << std::setw(width) << label << " " << std::flush;
            auto fileStart = clock_type::now();
            t.lines = fast_gtfs::bin_search::createMap(t.sorted, t.key);
            t.refs = fast_gtfs::bin_search::generateHeaderMap(t.sorted);
            std::cout << GREEN << "done" << RESET << "  " << std::right << std::setw(8)
                      << std::fixed << std::setprecision(1) << ms(clock_type::now() - fileStart) << " ms\n";
        }
        loaded = true;
    }
    std::cout << GREEN << "loaded all in " << std::fixed << std::setprecision(1)
              << ms(clock_type::now() - start) << " ms" << RESET << "\n";
    return true;
}

// "sort": rebuild every sorted file from the originals, then load them.
void sortAll(const fast_gtfs::fast_data_feed& df) {
    const auto all = tables(df);
    for (const auto& t : all) {
        if (!fast_gtfs::bin_search::fileExists(t.source)) {
            warn(t.source + " not found, not sorting");
            return;
        }
    }
    size_t width = 0;
    for (const auto& t : all) width = std::max(width, fileName(t.sorted).size() + strlen(t.key) + 3);

    std::cout << "sorting " << all.size() << " files\n";
    auto start = clock_type::now();
    for (const auto& t : all) {
        const string label = fileName(t.sorted) + " (" + t.key + ")";
        std::cout << "  " << std::left << std::setw(width) << label << " " << std::flush;
        auto fileStart = clock_type::now();
        fast_gtfs::bin_search::sortFile(t.source, t.key, t.sorted);
        std::cout << GREEN << "done" << RESET << "  " << std::right << std::setw(8)
                  << std::fixed << std::setprecision(1) << ms(clock_type::now() - fileStart) << " ms\n";
    }
    std::cout << GREEN << "sorted all in " << std::fixed << std::setprecision(1)
              << ms(clock_type::now() - start) << " ms" << RESET << "\n";
    load(df);
}

void consoleLoop(const fast_gtfs::fast_data_feed& df) {
    string line;
    while (std::getline(std::cin, line)) {
        if (line == "sort") { BusyScope busy("sorting"); sortAll(df); }
        else if (line == "verify") { BusyScope busy("verifying"); verify(df); }
        else if (line == "load") { BusyScope busy("loading"); load(df); }
        else if (!line.empty()) std::cout << "commands: sort, verify, load\n";
    }
}

// handlers call this first; false means the 503 has already been written
bool requireLoaded(Response& res) {
    if (const char* what = busyWith.load()) {
        res.status = 503;
        res.set_content(string("{\"error\":\"server busy: ") + what + "\"}", "application/json");
        return false;
    }
    if (loaded) return true;
    res.status = 503;
    res.set_content("{\"error\":\"data not loaded\"}", "application/json");
    return false;
}

int main() {
    static const fast_gtfs::fast_data_feed df(config::loadDataPath());

    if (!load(df)) warn("starting without data; type \"sort\" or \"load\"");
    std::cout << "commands: sort, verify, load\n";
    std::thread(consoleLoop, std::cref(df)).detach();

    Server svr;

    svr.Get("/api/trip/:trip_id", [](const Request& req, Response& res) {
        if (!requireLoaded(res)) return;
        std::shared_lock lock(dataMutex);
        const string trip_id = req.path_params.at("trip_id");

        auto start = std::chrono::steady_clock::now();
        string json = getTrip(trip_id, triplines, triprefs, stoplines, stoprefs,
                               stoptimesstoplines, stoptimesstoprefs, shapelines, shaperefs,
                               routelines, routerefs);
        auto elapsed = std::chrono::steady_clock::now() - start;

        std::cout << "GET /api/trip/" << trip_id << " -> "
                << std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count() << " µs\n";

        res.set_content(json, "application/json");
    });

    svr.Get("/api/stop/:stop_id/:year/:month/:day", [](const Request& req, Response& res) {
        if (!requireLoaded(res)) return;
        std::shared_lock lock(dataMutex);
        const string stop_id = req.path_params.at("stop_id");
        const int year = std::stoi(req.path_params.at("year"));
        const int month = std::stoi(req.path_params.at("month"));
        const int day = std::stoi(req.path_params.at("day"));

        auto start = std::chrono::steady_clock::now();
        string json = getStopDayTimes(stop_id, year, month, day,
                               stoptimesstopidlines, stoptimesstopidrefs, triplines, triprefs,
                               calendarlines, calendarrefs, calendardatelines, calendardaterefs,
                               stoplines, stoprefs, routelines, routerefs);
        auto elapsed = std::chrono::steady_clock::now() - start;

        std::cout << "GET /api/stop/" << stop_id << "/" << year << "/" << month << "/" << day << " -> "
                << std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count() << " µs\n";

        res.set_content(json, "application/json");
    });

    const int port = 5016;
    std::cout << "listening on http://localhost:" << port << "\n";
    svr.listen("0.0.0.0", port);

    return 0;
}
