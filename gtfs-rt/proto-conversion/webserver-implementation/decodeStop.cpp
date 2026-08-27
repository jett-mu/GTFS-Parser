#include <iostream>
#include <fstream>
#include <string>
#include "../transit-files/gtfs-realtime.pb.h"
#include <google/protobuf/text_format.h>
#include <google/protobuf/util/json_util.h>
#include <libgen.h>
#include <sys/stat.h>
#include <ctime>
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>

/* build command
clang++ -std=c++17 -O3 decodeStop.cpp ../transit-files/gtfs-realtime.pb.cc $(pkg-config --cflags --libs protobuf) -o decodeStop
*/
using namespace std;
using namespace transit_realtime;

// Refreshes outputPath from url if it is older than MAX_AGE_SECONDS.
// Uses a non-blocking flock so concurrent invocations don't race to
// download at once, and downloads to a per-process temp file that is
// only rename()'d over outputPath on success -- rename() is atomic, so
// readers never observe a partially-written (truncated) feed file.
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
        // Re-check now that we hold the lock: another process may have
        // just finished refreshing while we were opening the lock file.
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
    // If we didn't get the lock, another process is already refreshing --
    // just fall through and read whatever is currently on disk.
    close(lockFd);
}

int main(int argc, char* argv[]) {
    std::string exeDir = dirname(argv[0]);
    std::string outputPath = exeDir + "/downloaded_stop.pb";
    const int MAX_AGE_SECONDS = 15;

    refreshIfStale(outputPath, "https://rtu.york.ca/gtfsrealtime/TripUpdates", MAX_AGE_SECONDS);

    if (argc != 2) {
        cerr << "Usage: " << argv[0] << " <stopID>" << endl;
        return 1;
    }

    GOOGLE_PROTOBUF_VERIFY_VERSION;

    string inputFile = outputPath;
    string targetStopId = argv[1];

    fstream input(inputFile, ios::in | ios::binary);
    if (!input) {
        cerr << "Error: could not open " << inputFile << endl;
        return 1;
    }

    FeedMessage feed;
    if (!feed.ParseFromIstream(&input)) {
        cerr << "Error: failed to parse GTFS-realtime data" << endl;
        return 1;
    }

    FeedMessage filteredFeed;
    *filteredFeed.mutable_header() = feed.header();

    for (const auto& entity : feed.entity()) {
        bool matches = false;

        // Check trip_update stop_time_updates for matching stop_id
        if (entity.has_trip_update()) {
            for (const auto& stu : entity.trip_update().stop_time_update()) {
                if (stu.stop_id() == targetStopId) {
                    matches = true;
                    break;
                }
            }
        }

        // Check vehicle position current stop
        if (entity.has_vehicle()) {
            if (entity.vehicle().stop_id() == targetStopId) {
                matches = true;
            }
        }

        // Check alerts for matching stop
        if (entity.has_alert()) {
            for (const auto& selector : entity.alert().informed_entity()) {
                if (selector.stop_id() == targetStopId) {
                    matches = true;
                    break;
                }
            }
        }

        if (matches) {
            *filteredFeed.add_entity() = entity;
        }
    }
    // No live entities for this stop is a normal outcome (e.g. off-peak) --
    // fall through and emit a valid feed with an empty entity list, exit 0.

    google::protobuf::util::JsonPrintOptions options;
    options.add_whitespace = true;
    options.always_print_fields_with_no_presence = true;
    options.preserve_proto_field_names = true;

    string output;
    auto status = google::protobuf::util::MessageToJsonString(filteredFeed, &output, options);
    if (!status.ok()) {
        cerr << "Error: failed to convert feed to JSON: " << status.ToString() << endl;
        return 1;
    }

    cout << output << endl;

    google::protobuf::ShutdownProtobufLibrary();
    return 0;
}