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

int main() {
    auto inits = std::chrono::steady_clock::now();
    fast_gtfs::bin_search::sortFile(fast_config::fast_stop_path, "stop_id", fast_config::fast_stop_stop_id);
    fast_gtfs::bin_search::sortFile(fast_config::fast_stop_times_path, "trip_id", fast_config::fast_stop_times_trip_id);
    fast_gtfs::bin_search::sortFile(fast_config::fast_shape_path, "shape_id", fast_config::fast_shape_shape_id);

    triplines = fast_gtfs::bin_search::createMap(fast_config::fast_trip_path, "trip_id");
    triprefs =  fast_gtfs::bin_search::generateHeaderMap(fast_config::fast_trip_path);

    stoplines = fast_gtfs::bin_search::createMap(fast_config::fast_stop_stop_id, "stop_id");
    stoprefs = fast_gtfs::bin_search::generateHeaderMap(fast_config::fast_stop_stop_id);

    stoptimesstoplines = fast_gtfs::bin_search::createMap(fast_config::fast_stop_times_trip_id, "trip_id");
    stoptimesstoprefs = fast_gtfs::bin_search::generateHeaderMap(fast_config::fast_stop_times_trip_id);

    shapelines = fast_gtfs::bin_search::createMap(fast_config::fast_shape_shape_id, "shape_id");
    shaperefs = fast_gtfs::bin_search::generateHeaderMap(fast_config::fast_shape_shape_id);

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
                               stoptimesstoplines, stoptimesstoprefs, shapelines, shaperefs);
        auto elapsed = std::chrono::steady_clock::now() - start;

        std::cout << "GET /api/trip/" << trip_id << " -> "
                << std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count() << " µs\n";

        res.set_content(json, "application/json");
    });

    const int port = 5016;
    std::cout << "listening on http://localhost:" << port << "\n";
    svr.listen("0.0.0.0", port);

    return 0;
}
