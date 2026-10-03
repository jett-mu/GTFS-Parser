#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include "../transit-files/gtfs-realtime.pb.h"
#include "../../../config/config.hpp"
#include <google/protobuf/util/json_util.h>
#include <libgen.h>
#include <sys/stat.h>
#include <ctime>
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>

/* build command
clang++ -std=c++17 -O3 decodeAlerts.cpp ../transit-files/gtfs-realtime.pb.cc $(pkg-config --cflags --libs protobuf) -o decodeAlerts
*/
using namespace std;
using namespace transit_realtime;

// Copied from decodeTrip.cpp's helper of the same name: refreshes outputPath
// from url if it's older than maxAgeSeconds, using a non-blocking flock so
// concurrent invocations don't race to download at once, and an atomic
// rename() so readers never see a partially-written file.
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
    // argc==1: no route_id -> ALL active alerts (the mode the sidebar panel
    // needs, and what the readme already documents as the no-arg usage).
    // argc==2: filter to that route_id (kept for CLI/back-compat).
    if (argc > 2) {
        cerr << "Usage: " << argv[0] << " [route_id]" << endl;
        return 1;
    }
    bool filterByRoute = (argc == 2);
    string routeId = filterByRoute ? argv[1] : "";

    string exeDir = dirname(argv[0]);
    string outputPath = exeDir + "/downloaded_alerts.pb";
    const int MAX_AGE_SECONDS = 15;

    refreshIfStale(outputPath, config::loadRtAlertUrl(), MAX_AGE_SECONDS);

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

    google::protobuf::util::JsonPrintOptions options;
    options.add_whitespace = true;
    options.always_print_fields_with_no_presence = true;
    options.preserve_proto_field_names = true;

    // MessageToJsonString only serializes one message at a time, so collect
    // each matching entity's JSON and join into a single array -- the
    // previous version printed one bare JSON object per line (JSONL), which
    // broke json.loads() in server.py whenever more than one alert matched.
    vector<string> entityJsons;
    for (const FeedEntity& entity : feed.entity()) {
        if (!entity.has_alert()) continue;

        if (filterByRoute) {
            const Alert& alert = entity.alert();
            bool matchesRoute = false;
            for (const EntitySelector& selector : alert.informed_entity()) {
                if (selector.route_id() == routeId) {
                    matchesRoute = true;
                    break;
                }
            }
            if (!matchesRoute) continue;
        }

        string entityJson;
        auto status = google::protobuf::util::MessageToJsonString(entity, &entityJson, options);
        if (!status.ok()) {
            cerr << "Error: failed to convert alert to JSON: " << status.ToString() << endl;
            continue;
        }
        entityJsons.push_back(entityJson);
    }

    // Always emit a single valid JSON value (even "[]") with exit 0 -- "no
    // active alerts" is the common/expected case, not an error, and an empty
    // stdout would make json.loads() in server.py throw.
    string result = "[";
    for (size_t i = 0; i < entityJsons.size(); ++i) {
        if (i > 0) result += ",";
        result += entityJsons[i];
    }
    result += "]";
    cout << result << endl;

    google::protobuf::ShutdownProtobufLibrary();
    return 0;
}
