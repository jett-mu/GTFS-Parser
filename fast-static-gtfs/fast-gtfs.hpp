// this file is for experimetial features, that will improve the speed of the
//      quering hopefully by a lot. Implementations like hashing, binary 
//      search lexographically, are all here. Stil a work in progress.
//
//      When utilizing this header file try to avoid using the line 
//      "using namespace fast_gtfs" while also having "using namespace gtfs"
//      because function names may collide.

#ifndef FAST_GTFS_HPP
#define FAST_GTFS_HPP

#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include "../static-gtfs/gtfs.hpp"
#include "fast-config.hpp"

using std::cout;
using std::string;
using std::ifstream;
using std::ofstream;
using std::cerr;
using std::to_string;
using std::endl;
using std::vector;
using std::pair;

namespace fast_gtfs {

namespace bin_search {

inline void sortFile(const string& path, const string& keyColumn, const string& outputpath) { // e.g. for stops.txt's stop_id or trips.txt's trip_id
    string header;
    std::vector<std::pair<string, string>> keyedLines;
    {
        ifstream in(path);
        std::getline(in, header);
        auto refs = gtfs::createMapFromVector(gtfs::parseDataCSV(header));
        int keyIdx = refs[keyColumn];

        string line;
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            string key = gtfs::parseDataCSV(line)[keyIdx];
            keyedLines.emplace_back(std::move(key), std::move(line));
        }
        in.close();
    }

    std::sort(keyedLines.begin(), keyedLines.end(),
        [](const std::pair<string, string>& a,
           const std::pair<string, string>& b) { return a.first < b.first; });

    ofstream out(outputpath, std::ios::trunc);
    out << header << '\n';
    for (const auto& kl : keyedLines) out << kl.second << '\n';
    out.close();
}
inline vector<pair<string, vector<string>>> createMap(const string& path, const string& key) {
    ifstream stopFile(path);
    string header;
    std::getline(stopFile, header);
    auto refs = gtfs::createMapFromVector(gtfs::parseDataCSV(header));

    vector<pair<string, vector<string>>> lines;

    string currentLine;

    while (getline(stopFile, currentLine)) {
        if (currentLine.empty()) continue;
        vector<string> parsedCurrentLine = gtfs::parseDataCSV(currentLine);
        lines.emplace_back(parsedCurrentLine[refs[key]], parsedCurrentLine);
    }

    stopFile.close();
    return lines;
}
inline std::unordered_map<string, int> generateHeaderMap(const string& path) {
    ifstream stopFile(path);
    string header;
    std::getline(stopFile, header);
    auto refs = gtfs::createMapFromVector(gtfs::parseDataCSV(header));

    stopFile.close();

    return refs;
}
inline gtfs::stop getStopInfo(const string& stop_id, const vector<pair<string, vector<string>>>& lines, const std::unordered_map<string, int>& refs) { // stops.txt by stop_id
    gtfs::stop output;
    output.stop_id = "-1";


    auto stop_index = std::lower_bound(lines.begin(), lines.end(), stop_id,
        [](const std::pair<std::string, std::vector<string>>& element, const std::string& key) {
                return element.first < key;
        });

    if (stop_index == lines.end() || stop_index->first != stop_id) return output;

    // required fields
    output.stop_id = stop_id;


    // optional/conditionally required/conditionally forbidden fields
    { auto find = refs.find("stop_code");
    if (find != refs.end()) output.stop_code = stop_index->second[find->second]; }

    { auto find = refs.find("stop_name");
    if (find != refs.end()) output.stop_name = stop_index->second[find->second]; }

    { auto find = refs.find("tts_stop_name");
    if (find != refs.end()) output.tts_stop_name = stop_index->second[find->second]; }

    { auto find = refs.find("stop_desc");
    if (find != refs.end()) output.stop_desc = stop_index->second[find->second]; }

    { auto find = refs.find("stop_lat");
    if (find != refs.end()) output.stop_lat = gtfs::to_double(stop_index->second[find->second]); }

    { auto find = refs.find("stop_lon");
    if (find != refs.end()) output.stop_lon = gtfs::to_double(stop_index->second[find->second]); }

    { auto find = refs.find("zone_id");
    if (find != refs.end()) output.zone_id = stop_index->second[find->second]; }

    { auto find = refs.find("stop_url");
    if (find != refs.end()) output.stop_url = stop_index->second[find->second]; }

    { auto find = refs.find("location_type");
    if (find != refs.end()) output.location_type = static_cast<gtfs::stop::location>(gtfs::to_integer(stop_index->second[find->second])); }

    { auto find = refs.find("parent_station");
    if (find != refs.end()) output.parent_station = stop_index->second[find->second]; }

    { auto find = refs.find("stop_timezone");
    if (find != refs.end()) output.stop_timezone = stop_index->second[find->second]; }

    { auto find = refs.find("wheelchair_boarding");
    if (find != refs.end()) output.wheelchair_boarding = static_cast<gtfs::stop::wheelchair>(gtfs::to_integer(stop_index->second[find->second])); }

    { auto find = refs.find("level_id");
    if (find != refs.end()) output.level_id = stop_index->second[find->second]; }

    { auto find = refs.find("platform_code");
    if (find != refs.end()) output.platform_code = stop_index->second[find->second]; }

    { auto find = refs.find("stop_access");
    if (find != refs.end()) output.stop_access = static_cast<gtfs::stop::access>(gtfs::to_integer(stop_index->second[find->second])); }


    return output;
}
inline gtfs::route getRouteInfo(const string& route_id, const vector<pair<string, vector<string>>>& lines, const std::unordered_map<string, int>& refs) { // routes.txt by route_id
    gtfs::route output;
    output.route_id = "-1";


    auto route_index = std::lower_bound(lines.begin(), lines.end(), route_id,
        [](const std::pair<std::string, std::vector<string>>& element, const std::string& key) {
                return element.first < key;
        });

    if (route_index == lines.end() || route_index->first != route_id) return output;

    // required fields
    output.route_id = route_id;
    output.route_type = static_cast<gtfs::route::type>(gtfs::to_integer(route_index->second[refs.at("route_type")]));

    // optional/conditionally required/conditionally forbidden fields
    { auto find = refs.find("agency_id");
    if (find != refs.end()) output.agency_id = route_index->second[find->second]; }

    { auto find = refs.find("route_short_name");
    if (find != refs.end()) output.route_short_name = route_index->second[find->second]; }

    { auto find = refs.find("route_long_name");
    if (find != refs.end()) output.route_long_name = route_index->second[find->second]; }

    { auto find = refs.find("route_desc");
    if (find != refs.end()) output.route_desc = route_index->second[find->second]; }

    { auto find = refs.find("route_url");
    if (find != refs.end()) output.route_url = route_index->second[find->second]; }

    { auto find = refs.find("route_color");
    if (find != refs.end()) output.route_color = route_index->second[find->second]; }

    { auto find = refs.find("route_text_color");
    if (find != refs.end()) output.route_text_color = route_index->second[find->second]; }

    { auto find = refs.find("route_sort_order");
    if (find != refs.end()) output.route_sort_order = (route_index->second[find->second].empty() || route_index->second[find->second] == " ") ? 25565 : gtfs::to_integer(route_index->second[find->second]); }

    { auto find = refs.find("continuous_pickup");
    if (find != refs.end()) output.continuous_pickup = static_cast<gtfs::route::continuous_pickup_dropoff>(gtfs::to_integer(route_index->second[find->second])); }

    { auto find = refs.find("continuous_drop_off");
    if (find != refs.end()) output.continuous_drop_off = static_cast<gtfs::route::continuous_pickup_dropoff>(gtfs::to_integer(route_index->second[find->second])); }

    { auto find = refs.find("network_id");
    if (find != refs.end()) output.network_id = route_index->second[find->second]; }

    { auto find = refs.find("cemv_support");
    if (find != refs.end()) output.cemv_support = static_cast<gtfs::route::cemv>(gtfs::to_integer(route_index->second[find->second])); }

    return output;
}
inline gtfs::trip getTripInfo(const string& trip_id, const vector<pair<string, vector<string>>>& lines, const std::unordered_map<string, int>& refs) { // trips.txt by trip_id
    gtfs::trip output;
    output.trip_id = "-1";


    auto trip_index = std::lower_bound(lines.begin(), lines.end(), trip_id,
        [](const std::pair<std::string, std::vector<string>>& element, const std::string& key) {
                return element.first < key;
        });

    if (trip_index == lines.end() || trip_index->first != trip_id) return output;

    output.route_id = trip_index->second[refs.at("route_id")];
    output.service_id = trip_index->second[refs.at("service_id")];
    output.trip_id = trip_id;

    // optional/conditionally required/conditionally forbidden fields
    { auto find = refs.find("trip_headsign");
        if (find != refs.end()) output.trip_headsign = trip_index->second[find->second]; }

    { auto find = refs.find("trip_short_name");
        if (find != refs.end()) output.trip_short_name = trip_index->second[find->second]; }

    { auto find = refs.find("block_id");
        if (find != refs.end()) output.block_id = trip_index->second[find->second]; }

    { auto find = refs.find("shape_id");
        if (find != refs.end()) output.shape_id = trip_index->second[find->second]; }

    // bool field
    { auto find = refs.find("direction_id");
        if (find != refs.end()) output.direction_id = static_cast<bool>(stoi(trip_index->second[find->second])); }

    // allowable fields
    { auto find = refs.find("wheelchair_accessible");
        if (find != refs.end()) output.wheelchair_accessible = static_cast<gtfs::trip::allowable>(stoi(trip_index->second[find->second])); }

    { auto find = refs.find("bikes_allowed");
        if (find != refs.end()) output.bikes_allowed = static_cast<gtfs::trip::allowable>(stoi(trip_index->second[find->second])); }

    { auto find = refs.find("cars_allowed");
        if (find != refs.end()) output.cars_allowed = static_cast<gtfs::trip::allowable>(stoi(trip_index->second[find->second])); }

    return output;
}
inline vector<gtfs::trip_segment> getAllStops(const string& trip_id, const vector<pair<string, vector<string>>>& lines, const std::unordered_map<string, int>& refs) { // stop_times by trip_id
    vector<gtfs::trip_segment> output;
    // get all stops with trip_id matching,

    auto trip_index_lower = std::lower_bound(lines.begin(), lines.end(), trip_id,
        [](const std::pair<std::string, std::vector<string>>& element, const std::string& key) {
            return element.first < key;
        });

    if (trip_index_lower == lines.end() || trip_index_lower->first != trip_id) return output;

    auto trip_index_upper = std::upper_bound(lines.begin(), lines.end(), trip_id,
        [](const std::string& key, const std::pair<std::string, std::vector<string>>& element) {
            return key < element.first;
        });


    for (auto k = trip_index_lower; k != trip_index_upper; ++k) {
            gtfs::trip_segment x;

            // required fields
            x.stop.trip_id = trip_id;
            x.stop.stop_sequence = gtfs::to_integer(k->second[refs.at("stop_sequence")]);

            // optional/conditionally required/conditionally forbidden fields
            { auto find = refs.find("stop_id");
            if (find != refs.end()) x.stop.stop_id = k->second[refs.at("stop_id")]; }

            { auto find = refs.find("arrival_time");
            if (find != refs.end()) x.stop.arrival_time = gtfs::parseFormattedTime(k->second[find->second]); }

            { auto find = refs.find("departure_time");
            if (find != refs.end()) x.stop.departure_time = gtfs::parseFormattedTime(k->second[find->second]); }

            { auto find = refs.find("location_group_id");
            if (find != refs.end()) x.stop.location_group_id = k->second[find->second]; }

            { auto find = refs.find("location_id");
            if (find != refs.end()) x.stop.location_id = k->second[find->second]; }

            { auto find = refs.find("stop_sequence");
            if (find != refs.end()) x.stop.stop_sequence = gtfs::to_integer(k->second[find->second]); }

            { auto find = refs.find("stop_headsign");
            if (find != refs.end()) x.stop.stop_headsign = k->second[find->second]; }

            { auto find = refs.find("start_pickup_drop_off_window");
            if (find != refs.end()) x.stop.start_pickup_drop_off_window = gtfs::parseFormattedTime(k->second[find->second]); }

            { auto find = refs.find("end_pickup_drop_off_window");
            if (find != refs.end()) x.stop.end_pickup_drop_off_window = gtfs::parseFormattedTime(k->second[find->second]); }

            { auto find = refs.find("pickup_type");
            if (find != refs.end()) x.stop.pickup_type = static_cast<gtfs::stop_time::pickup_dropoff>(gtfs::to_integer(k->second[find->second])); }

            { auto find = refs.find("drop_off_type");
            if (find != refs.end()) x.stop.drop_off_type = static_cast<gtfs::stop_time::pickup_dropoff>(gtfs::to_integer(k->second[find->second])); }

            { auto find = refs.find("continuous_pickup");
            if (find != refs.end()) x.stop.continuous_pickup = static_cast<gtfs::stop_time::continuous_pickup_dropoff>(gtfs::to_integer(k->second[find->second])); }

            { auto find = refs.find("continuous_drop_off");
            if (find != refs.end()) x.stop.continuous_drop_off = static_cast<gtfs::stop_time::continuous_pickup_dropoff>(gtfs::to_integer(k->second[find->second])); }

            { auto find = refs.find("shape_dist_traveled");
            if (find != refs.end()) x.stop.shape_dist_traveled = gtfs::to_float(k->second[find->second]); }

            { auto find = refs.find("timepoint");
            if (find != refs.end()) x.stop.timepoint = static_cast<gtfs::stop_time::timepoint_type>(gtfs::to_integer(k->second[find->second])); }

            { auto find = refs.find("pickup_booking_rule_id");
            if (find != refs.end()) x.stop.pickup_booking_rule_id = k->second[find->second]; }

            { auto find = refs.find("drop_off_booking_rule_id");
            if (find != refs.end()) x.stop.drop_off_booking_rule_id = k->second[find->second]; }

            output.push_back(x);
    }
    return output;
}
inline vector<gtfs::shape> getShapeInfo(const string& shape_id, const vector<pair<string, vector<string>>>& lines, const std::unordered_map<string, int>& refs) { // shapes.txt by shape_id
    vector<gtfs::shape> output;
    // get all shape points with shape_id matching,

    auto shape_index_lower = std::lower_bound(lines.begin(), lines.end(), shape_id,
        [](const std::pair<std::string, std::vector<string>>& element, const std::string& key) {
            return element.first < key;
        });

    if (shape_index_lower == lines.end() || shape_index_lower->first != shape_id) return output;

    auto shape_index_upper = std::upper_bound(lines.begin(), lines.end(), shape_id,
        [](const std::string& key, const std::pair<std::string, std::vector<string>>& element) {
            return key < element.first;
        });

    for (auto k = shape_index_lower; k != shape_index_upper; ++k) {
            gtfs::shape x;

            // required fields
            x.shape_id = shape_id;
            x.shape_pt_lat = gtfs::to_double(k->second[refs.at("shape_pt_lat")]);
            x.shape_pt_lon = gtfs::to_double(k->second[refs.at("shape_pt_lon")]);
            x.shape_pt_sequence = gtfs::to_integer(k->second[refs.at("shape_pt_sequence")]);

            // optional field
            { auto find = refs.find("shape_dist_traveled");
            if (find != refs.end()) x.shape_dist_traveled = gtfs::to_float(k->second[find->second]); }

            output.push_back(x);
    }

    // sortFile only guarantees ordering by shape_id (std::sort is not stable),
    // so restore shape_pt_sequence order within the matched group.
    std::sort(output.begin(), output.end(),
        [](const gtfs::shape& a, const gtfs::shape& b) { return a.shape_pt_sequence < b.shape_pt_sequence; });

    return output;
}
inline bool isTripValid(const string& trip_id, const int& year, const int& month, const int& day,
                        const vector<pair<string, vector<string>>>& triplines, const std::unordered_map<string, int>& triprefs,
                        const vector<pair<string, vector<string>>>& calendarlines, const std::unordered_map<string, int>& calendarrefs,
                        const vector<pair<string, vector<string>>>& calendardatelines, const std::unordered_map<string, int>& calendardaterefs,
                        const bool noException = false) { // requirements: trips.txt by trip_id, calendar.txt by service_id, and if noException is false, calendar_dates.txt by service_id
    bool output = false;

    string constructedDate = to_string(year) +
                            (month < 10 ? "0" + to_string(month) : to_string(month)) +
                            (day < 10 ? "0" + to_string(day) : to_string(day));

    gtfs::week dayOfWeek = gtfs::convertDateToWeek(year, month, day);
    string service_id = fast_gtfs::bin_search::getTripInfo(trip_id, triplines, triprefs).service_id;

    auto calendarIndex = std::lower_bound(calendarlines.begin(), calendarlines.end(), service_id,
        [](const std::pair<std::string, std::vector<string>>& element, const std::string& key) {
            return element.first < key;
        });

    if (calendarIndex != calendarlines.end() && calendarIndex->first == service_id) {
        string test;

        switch (static_cast<int>(dayOfWeek)) {
            case 0: test = "monday"; break;
            case 1: test = "tuesday"; break;
            case 2: test = "wednesday"; break;
            case 3: test = "thursday"; break;
            case 4: test = "friday"; break;
            case 5: test = "saturday"; break;
            case 6: test = "sunday"; break;
            default:
                cerr << "dayOfWeek is invalid or uninitialized\n";
                return false;
        }

        if (gtfs::parseFormattedDate(calendarIndex->second[calendarrefs.at("start_date")]) > gtfs::calendar_day(year, month, day)
            || gtfs::parseFormattedDate(calendarIndex->second[calendarrefs.at("end_date")]) < gtfs::calendar_day(year, month, day)) {
            output = false;
        } else if (static_cast<bool>(gtfs::to_integer(calendarIndex->second[calendarrefs.at(test)]))) {
            output = true;
        }
    }

    if (noException) return output;

    auto calendarDateLower = std::lower_bound(calendardatelines.begin(), calendardatelines.end(), service_id,
        [](const std::pair<std::string, std::vector<string>>& element, const std::string& key) {
            return element.first < key;
        });

    auto calendarDateUpper = std::upper_bound(calendardatelines.begin(), calendardatelines.end(), service_id,
        [](const std::string& key, const std::pair<std::string, std::vector<string>>& element) {
            return key < element.first;
        });

    for (auto k = calendarDateLower; k != calendarDateUpper; ++k) {
        if (k->second[calendardaterefs.at("date")] == constructedDate) {
            output = (k->second[calendardaterefs.at("exception_type")] == "1");
        }
    }

    return output;
}
inline vector<gtfs::trip_segment> getDayTimesAtStop(const string& stop_id, const int& year, const int& month, const int& day,
                                                    const vector<pair<string, vector<string>>>& stoptimelines, const std::unordered_map<string, int>& stoptimerefs,
                                                    const vector<pair<string, vector<string>>>& triplines, const std::unordered_map<string, int>& triprefs,
                                                    const vector<pair<string, vector<string>>>& calendarlines, const std::unordered_map<string, int>& calendarrefs,
                                                    const vector<pair<string, vector<string>>>& calendardatelines, const std::unordered_map<string, int>& calendardaterefs,
                                                    const bool noException = false) { // stop_times.txt by stop_id, trips.txt by trip_id, calendar.txt by service_id, calendar_dates.txt by service_id
    vector<gtfs::trip_segment> output;
    auto stopTimeLower = std::lower_bound(stoptimelines.begin(), stoptimelines.end(), stop_id,
    [](const std::pair<std::string, std::vector<string>>& element, const std::string& key) {
        return element.first < key;
    });

    if (stopTimeLower == stoptimelines.end() || stopTimeLower->first != stop_id) return output;

    auto stopTimeUpper = std::upper_bound(stoptimelines.begin(), stoptimelines.end(), stop_id,
        [](const std::string& key, const std::pair<std::string, std::vector<string>>& element) {
            return key < element.first;
        });


    for (auto k = stopTimeLower; k != stopTimeUpper; ++k) {
        gtfs::trip_segment x;

        // required fields
        x.stop.stop_id = stop_id;
        x.stop.trip_id = k->second[stoptimerefs.at("trip_id")];
        x.stop.stop_sequence = gtfs::to_integer(k->second[stoptimerefs.at("stop_sequence")]);

        // optional/conditionally required/conditionally forbidden fields
        { auto find = stoptimerefs.find("arrival_time");
        if (find != stoptimerefs.end()) x.stop.arrival_time = gtfs::parseFormattedTime(k->second[find->second]); }

        { auto find = stoptimerefs.find("departure_time");
        if (find != stoptimerefs.end()) x.stop.departure_time = gtfs::parseFormattedTime(k->second[find->second]); }

        { auto find = stoptimerefs.find("location_group_id");
        if (find != stoptimerefs.end()) x.stop.location_group_id = k->second[find->second]; }

        { auto find = stoptimerefs.find("location_id");
        if (find != stoptimerefs.end()) x.stop.location_id = k->second[find->second]; }

        { auto find = stoptimerefs.find("stop_sequence");
        if (find != stoptimerefs.end()) x.stop.stop_sequence = gtfs::to_integer(k->second[find->second]); }

        { auto find = stoptimerefs.find("stop_headsign");
        if (find != stoptimerefs.end()) x.stop.stop_headsign = k->second[find->second]; }

        { auto find = stoptimerefs.find("start_pickup_drop_off_window");
        if (find != stoptimerefs.end()) x.stop.start_pickup_drop_off_window = gtfs::parseFormattedTime(k->second[find->second]); }

        { auto find = stoptimerefs.find("end_pickup_drop_off_window");
        if (find != stoptimerefs.end()) x.stop.end_pickup_drop_off_window = gtfs::parseFormattedTime(k->second[find->second]); }

        { auto find = stoptimerefs.find("pickup_type");
        if (find != stoptimerefs.end()) x.stop.pickup_type = static_cast<gtfs::stop_time::pickup_dropoff>(gtfs::to_integer(k->second[find->second])); }

        { auto find = stoptimerefs.find("drop_off_type");
        if (find != stoptimerefs.end()) x.stop.drop_off_type = static_cast<gtfs::stop_time::pickup_dropoff>(gtfs::to_integer(k->second[find->second])); }

        { auto find = stoptimerefs.find("continuous_pickup");
        if (find != stoptimerefs.end()) x.stop.continuous_pickup = static_cast<gtfs::stop_time::continuous_pickup_dropoff>(gtfs::to_integer(k->second[find->second])); }

        { auto find = stoptimerefs.find("continuous_drop_off");
        if (find != stoptimerefs.end()) x.stop.continuous_drop_off = static_cast<gtfs::stop_time::continuous_pickup_dropoff>(gtfs::to_integer(k->second[find->second])); }

        { auto find = stoptimerefs.find("shape_dist_traveled");
        if (find != stoptimerefs.end()) x.stop.shape_dist_traveled = gtfs::to_float(k->second[find->second]); }

        { auto find = stoptimerefs.find("timepoint");
        if (find != stoptimerefs.end()) x.stop.timepoint = static_cast<gtfs::stop_time::timepoint_type>(gtfs::to_integer(k->second[find->second])); }

        { auto find = stoptimerefs.find("pickup_booking_rule_id");
        if (find != stoptimerefs.end()) x.stop.pickup_booking_rule_id = k->second[find->second]; }

        { auto find = stoptimerefs.find("drop_off_booking_rule_id");
        if (find != stoptimerefs.end()) x.stop.drop_off_booking_rule_id = k->second[find->second]; }

        output.push_back(x);
    }

    for (gtfs::trip_segment& k : output) {
        k.route_id = fast_gtfs::bin_search::getTripInfo(k.stop.trip_id, triplines, triprefs).route_id;
    }

    output.erase(std::remove_if(output.begin(), output.end(),
        [&](const gtfs::trip_segment& x) {
            return !fast_gtfs::bin_search::isTripValid(x.stop.trip_id, year, month, day,
                triplines, triprefs, calendarlines, calendarrefs, calendardatelines, calendardaterefs, noException);
        }), output.end());

    return output;
}

}

};

#endif