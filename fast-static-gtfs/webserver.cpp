//
// Created by Jett Mu on 2026-08-22.
//

#include <chrono>
#include "../static-gtfs/gtfs.hpp"
#include "fast-gtfs.hpp"
#include "webservermethods.hpp"
#include "httplib.h"

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

int main() {
    auto inits = std::chrono::steady_clock::now();
    fast_gtfs::bin_search::sortFile(fast_config::fast_stop_path, "stop_id", fast_config::fast_stop_stop_id);
    fast_gtfs::bin_search::sortFile(fast_config::fast_stop_times_path, "trip_id", fast_config::fast_stop_times_trip_id);
    fast_gtfs::bin_search::sortFile(fast_config::fast_stop_times_path, "stop_id", fast_config::fast_stop_times_stop_id);
    fast_gtfs::bin_search::sortFile(fast_config::fast_shape_path, "shape_id", fast_config::fast_shape_shape_id);
    fast_gtfs::bin_search::sortFile(fast_config::fast_trip_path, "trip_id", fast_config::fast_trip_trip_id);
    fast_gtfs::bin_search::sortFile(fast_config::fast_calendar_path, "service_id", fast_config::fast_calendar_service_id);
    fast_gtfs::bin_search::sortFile(fast_config::fast_calendar_dates_path, "service_id", fast_config::fast_calendar_dates_service_id);
    fast_gtfs::bin_search::sortFile(fast_config::fast_route_path, "route_id", fast_config::fast_route_route_id);

    triplines = fast_gtfs::bin_search::createMap(fast_config::fast_trip_trip_id, "trip_id");
    triprefs =  fast_gtfs::bin_search::generateHeaderMap(fast_config::fast_trip_trip_id);

    stoplines = fast_gtfs::bin_search::createMap(fast_config::fast_stop_stop_id, "stop_id");
    stoprefs = fast_gtfs::bin_search::generateHeaderMap(fast_config::fast_stop_stop_id);

    stoptimesstoplines = fast_gtfs::bin_search::createMap(fast_config::fast_stop_times_trip_id, "trip_id");
    stoptimesstoprefs = fast_gtfs::bin_search::generateHeaderMap(fast_config::fast_stop_times_trip_id);

    stoptimesstopidlines = fast_gtfs::bin_search::createMap(fast_config::fast_stop_times_stop_id, "stop_id");
    stoptimesstopidrefs = fast_gtfs::bin_search::generateHeaderMap(fast_config::fast_stop_times_stop_id);

    shapelines = fast_gtfs::bin_search::createMap(fast_config::fast_shape_shape_id, "shape_id");
    shaperefs = fast_gtfs::bin_search::generateHeaderMap(fast_config::fast_shape_shape_id);

    calendarlines = fast_gtfs::bin_search::createMap(fast_config::fast_calendar_service_id, "service_id");
    calendarrefs = fast_gtfs::bin_search::generateHeaderMap(fast_config::fast_calendar_service_id);

    calendardatelines = fast_gtfs::bin_search::createMap(fast_config::fast_calendar_dates_service_id, "service_id");
    calendardaterefs = fast_gtfs::bin_search::generateHeaderMap(fast_config::fast_calendar_dates_service_id);

    routelines = fast_gtfs::bin_search::createMap(fast_config::fast_route_route_id, "route_id");
    routerefs = fast_gtfs::bin_search::generateHeaderMap(fast_config::fast_route_route_id);

    cout << "done init\n";
    auto ends = std::chrono::steady_clock::now();
    auto elapsed = ends - inits;

    std::cout << "Init time:\n" << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()
    << " ms\n" << std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count()
    << " µs\n";

    Server svr;

    svr.Get("/api/trip/:trip_id", [](const Request& req, Response& res) {
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
