#include <iostream>
#include "fast-config.hpp"
#include "fast-gtfs.hpp"

vector<pair<string, vector<string>>> stoptimesstoplines;
std::unordered_map<string, int> stoptimesstoprefs;

vector<pair<string, vector<string>>> triptriplines;
std::unordered_map<string, int> triptriprefs;

int main() {
    fast_gtfs::bin_search::sortFile(fast_config::fast_stop_times_path, "stop_id",fast_config::fast_stop_times_stop_id);

    stoptimesstoplines = fast_gtfs::bin_search::createMap(fast_config::fast_stop_times_stop_id, "stop_id");
    stoptimesstoprefs = fast_gtfs::bin_search::generateHeaderMap(fast_config::fast_stop_times_stop_id);

    fast_gtfs::bin_search::sortFile(fast_config::fast_trip_path, "trip_id",fast_config::fast_trip_trip_id);

    triptriplines = fast_gtfs::bin_search::createMap(fast_config::fast_trip_trip_id, "trip_id");
    triptriprefs = fast_gtfs::bin_search::generateHeaderMap(fast_config::fast_trip_trip_id);

    auto start = std::chrono::steady_clock::now();

    vector<gtfs::trip_segment> a = fast_gtfs::bin_search::getDayTimesAtStop("9841", 2026, 8, 25,stoptimesstoplines, stoptimesstoprefs,  triptriplines, triptriprefs);

    auto elapsed = std::chrono::steady_clock::now() - start;

    std::cout << " -> "
            << std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count() << " µs\n";

    for (gtfs::trip_segment s : a) {
        cout << s.route_id << endl;
    }
}