#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include "../transit-files/gtfs-realtime.pb.h"
#include "../../../static-gtfs/gtfs.hpp"
#include "../../../static-gtfs/config.hpp"
#include <libgen.h>
#include <sys/stat.h>
#include <ctime>
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>

/* build command
clang++ -std=c++17 -O3 routeMedianDelay.cpp ../transit-files/gtfs-realtime.pb.cc $(pkg-config --cflags --libs protobuf) -o routeMedianDelay
*/
using namespace std;
using namespace transit_realtime;

// Same refresh scheme as decodeStop.cpp -- shares downloaded_stop.pb (TripUpdates
// feed) with decodeStop, since both just need up-to-date trip updates.
static void refreshIfStale(const std::string& outputPath, const std::string& url, int maxAgeSeconds) {
    struct stat st;
    bool stale = true;
    if (stat(outputPath.c_str(), &st) == 0) {
        stale = (time(nullptr) - st.st_mtime) > maxAgeSeconds;
    }
    if (!stale) return;

    std::string lockPath = outputPath + ".lock";
    int lockFd = open(lockPath.c_str(), O_CREAT | O_RDWR, 0644);
    if (lockFd < 0) return;

    if (flock(lockFd, LOCK_EX | LOCK_NB) == 0) {
        if (stat(outputPath.c_str(), &st) == 0) {
            stale = (time(nullptr) - st.st_mtime) > maxAgeSeconds;
        } else {
            stale = true;
        }

        if (stale) {
            std::string tmpPath = outputPath + ".tmp." + std::to_string(getpid());
            std::string cmd = "wget -q --timeout=5 --tries=1 -O " + tmpPath + " " + url;
            int rc = system(cmd.c_str());

            struct stat tmpSt;
            if (rc == 0 && stat(tmpPath.c_str(), &tmpSt) == 0 && tmpSt.st_size > 0) {
                rename(tmpPath.c_str(), outputPath.c_str());
            } else {
                unlink(tmpPath.c_str());
            }
        }
        flock(lockFd, LOCK_UN);
    }
    close(lockFd);
}

// Picks the delay (seconds) a StopTimeUpdate reports for "how late is this trip
// right now": prefer arrival delay, fall back to departure delay.
static bool stopTimeUpdateDelay(const TripUpdate::StopTimeUpdate& stu, int32_t& outDelay) {
    if (stu.has_arrival() && stu.arrival().has_delay()) {
        outDelay = stu.arrival().delay();
        return true;
    }
    if (stu.has_departure() && stu.departure().has_delay()) {
        outDelay = stu.departure().delay();
        return true;
    }
    return false;
}

static double median(std::vector<int32_t> values) {
    std::sort(values.begin(), values.end());
    size_t n = values.size();
    if (n % 2 == 1) return static_cast<double>(values[n / 2]);
    return (values[n / 2 - 1] + values[n / 2]) / 2.0;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        cerr << "Usage: " << argv[0] << " <routeShortNameOrId>" << endl;
        return 1;
    }

    std::string exeDir = dirname(argv[0]);
    std::string outputPath = exeDir + "/downloaded_stop.pb";
    const int MAX_AGE_SECONDS = 15;
    refreshIfStale(outputPath, "https://rtu.york.ca/gtfsrealtime/TripUpdates", MAX_AGE_SECONDS);

    // Resolve whatever the caller passed (e.g. "601") to a real route_id via
    // the same fuzzy search the webserver's route search box uses.
    auto matches = gtfs::searchRoute(argv[1]);
    if (matches.empty()) {
        cerr << "Error: no route matches \"" << argv[1] << "\"" << endl;
        return 1;
    }
    const gtfs::routematch& route = matches.front();

    GOOGLE_PROTOBUF_VERIFY_VERSION;

    fstream input(outputPath, ios::in | ios::binary);
    if (!input) {
        cerr << "Error: could not open " << outputPath << endl;
        return 1;
    }

    FeedMessage feed;
    if (!feed.ParseFromIstream(&input)) {
        cerr << "Error: failed to parse GTFS-realtime data" << endl;
        return 1;
    }

    std::vector<int32_t> delays;

    for (const auto& entity : feed.entity()) {
        if (!entity.has_trip_update() || !entity.trip_update().has_trip()) continue;

        const std::string& tripId = entity.trip_update().trip().trip_id();
        if (tripId.empty()) continue;

        // Per-trip lookup against trips.txt, same idiom as the rest of the
        // codebase (a linear scan per call -- fine at this feed's scale).
        gtfs::trip tripInfo = gtfs::getTripInfo(tripId);
        if (tripInfo.route_id != route.route_id) continue;

        for (const auto& stu : entity.trip_update().stop_time_update()) {
            int32_t delay;
            if (stopTimeUpdateDelay(stu, delay)) {
                delays.push_back(delay);
                break; // one representative sample per trip
            }
        }
    }

    cout << "{"
         << "\"route_id\":\"" << route.route_id << "\","
         << "\"route_short_name\":\"" << route.route_short_name << "\","
         << "\"sample_count\":" << delays.size() << ",";

    if (delays.empty()) {
        cout << "\"median_delay\":null";
    } else {
        cout << "\"median_delay\":" << median(delays);
    }
    cout << "}" << endl;

    google::protobuf::ShutdownProtobufLibrary();
    return 0;
}
