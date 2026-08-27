#include <iostream>
#include <fstream>
#include <string>
#include <unordered_map>
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
clang++ -std=c++17 -O3 routeVehicles.cpp ../transit-files/gtfs-realtime.pb.cc $(pkg-config --cflags --libs protobuf) -o routeVehicles
*/
using namespace std;
using namespace transit_realtime;

// Same refresh scheme as decodeTrip.cpp / routeMedianDelay.cpp.
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
            // Quoted: outputPath can contain spaces (e.g. a repo checked out
            // under a directory with a space in its name), which would
            // otherwise get word-split by the shell system() hands this to.
            std::string cmd = "wget -q --timeout=5 --tries=1 -O '" + tmpPath + "' '" + url + "'";
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

// Same idiom as routeMedianDelay.cpp: prefer arrival delay, fall back to departure delay.
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

static std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        cerr << "Usage: " << argv[0] << " <routeShortNameOrId>" << endl;
        return 1;
    }

    std::string exeDir = dirname(argv[0]);
    std::string vehiclesPath = exeDir + "/downloaded_file.pb";
    std::string tripUpdatesPath = exeDir + "/downloaded_stop.pb";
    const int MAX_AGE_SECONDS = 15;
    refreshIfStale(vehiclesPath, "https://rtu.york.ca/gtfsrealtime/VehiclePositions", MAX_AGE_SECONDS);
    refreshIfStale(tripUpdatesPath, "https://rtu.york.ca/gtfsrealtime/TripUpdates", MAX_AGE_SECONDS);

    // Resolve whatever the caller passed (e.g. "601") to a real route_id, same
    // fuzzy search the webserver's route search box and routeMedianDelay use.
    auto matches = gtfs::searchRoute(argv[1]);
    if (matches.empty()) {
        cerr << "Error: no route matches \"" << argv[1] << "\"" << endl;
        return 1;
    }
    const gtfs::routematch& route = matches.front();

    GOOGLE_PROTOBUF_VERIFY_VERSION;

    // Build trip_id -> delay from the TripUpdates feed first (one representative
    // sample per trip, same as routeMedianDelay.cpp).
    std::unordered_map<std::string, int32_t> delayByTrip;
    {
        fstream input(tripUpdatesPath, ios::in | ios::binary);
        if (input) {
            FeedMessage feed;
            if (feed.ParseFromIstream(&input)) {
                for (const auto& entity : feed.entity()) {
                    if (!entity.has_trip_update() || !entity.trip_update().has_trip()) continue;
                    const std::string& tripId = entity.trip_update().trip().trip_id();
                    if (tripId.empty()) continue;
                    for (const auto& stu : entity.trip_update().stop_time_update()) {
                        int32_t delay;
                        if (stopTimeUpdateDelay(stu, delay)) {
                            delayByTrip[tripId] = delay;
                            break;
                        }
                    }
                }
            }
        }
    }

    fstream input(vehiclesPath, ios::in | ios::binary);
    if (!input) {
        cerr << "Error: could not open " << vehiclesPath << endl;
        return 1;
    }

    FeedMessage feed;
    if (!feed.ParseFromIstream(&input)) {
        cerr << "Error: failed to parse GTFS-realtime data" << endl;
        return 1;
    }

    cout << "{"
         << "\"route_id\":\"" << jsonEscape(route.route_id) << "\","
         << "\"route_short_name\":\"" << jsonEscape(route.route_short_name) << "\","
         << "\"feed_timestamp\":" << (feed.has_header() && feed.header().has_timestamp() ? feed.header().timestamp() : 0) << ","
         << "\"vehicles\":[";

    bool first = true;
    for (const auto& entity : feed.entity()) {
        if (!entity.has_vehicle()) continue;
        const VehiclePosition& vp = entity.vehicle();
        if (!vp.has_trip() || !vp.has_position()) continue;

        const std::string& tripId = vp.trip().trip_id();
        if (tripId.empty()) continue;

        // Per-trip lookup against trips.txt, same idiom as the rest of the
        // codebase (a linear scan per call -- fine at this feed's scale).
        gtfs::trip tripInfo = gtfs::getTripInfo(tripId);
        if (tripInfo.route_id != route.route_id) continue;

        const Position& pos = vp.position();
        std::string vehicleId = vp.has_vehicle() ? vp.vehicle().id() : "";
        std::string vehicleLabel = vp.has_vehicle() ? vp.vehicle().label() : "";

        if (!first) cout << ",";
        first = false;

        cout << "{"
             << "\"trip_id\":\"" << jsonEscape(tripId) << "\","
             << "\"vehicle_id\":\"" << jsonEscape(vehicleId) << "\","
             << "\"vehicle_label\":\"" << jsonEscape(vehicleLabel) << "\","
             << "\"lat\":" << pos.latitude() << ","
             << "\"lng\":" << pos.longitude() << ","
             << "\"bearing\":" << (pos.has_bearing() ? pos.bearing() : -1) << ",";

        auto it = delayByTrip.find(tripId);
        if (it != delayByTrip.end()) {
            cout << "\"delay\":" << it->second;
        } else {
            cout << "\"delay\":null";
        }
        cout << "}";
    }

    cout << "]}" << endl;

    google::protobuf::ShutdownProtobufLibrary();
    return 0;
}
