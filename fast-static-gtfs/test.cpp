#include "fast-gtfs.hpp"
#include <iostream>
#include <fstream>
#include <chrono>
#include <vector>

using namespace std;

int main() {
    fast_gtfs::bin_search::sortFile(fast_config::fast_stop_path, "stop_id", );
    cout << "done sorting\n";

    auto starta = std::chrono::steady_clock::now();

    const std::unordered_map<string, int> nn = fast_gtfs::bin_search::generateHeaderMap(fast_gtfs::fast_trip_path);
    const std::vector<pair<string, vector<string>>> na = fast_gtfs::bin_search::createMap(fast_gtfs::fast_trip_path, "trip_id");
    auto enda = std::chrono::steady_clock::now();
    auto elapsed = enda - starta;
    std::cout << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()
<< " ms\n" << std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count()
<< " µs\n";
    while (true) {
        string a;
        cin >> a;

        auto start = std::chrono::steady_clock::now();
        gtfs::trip x = fast_gtfs::bin_search::getTripInfo(a, na, nn);
        cout << x.trip_headsign << endl;
        auto end = std::chrono::steady_clock::now();

        auto elapsed = end - start;
        // 4. Convert and print the duration in various units
        std::cout << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()
        << " ms\n" << std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count()
        << " µs\n";


    }






    return 0;
}