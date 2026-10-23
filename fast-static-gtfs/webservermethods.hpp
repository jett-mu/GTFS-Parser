#include <algorithm>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <unordered_set>
#include "../static-gtfs/gtfs.hpp"
#include "fast-gtfs.hpp"
#include "planner.hpp"

using std::cout;
using std::ostringstream;

inline string getTrip(const string& trip_id,
                     const vector<pair<string, vector<string>>>& triplines,
                     const std::unordered_map<string, int>& triprefs,
                     const vector<pair<string, vector<string>>>& stoplines,
                     const std::unordered_map<string, int>& stoprefs,
                     const vector<pair<string, vector<string>>>& stoptimesstoplines,
                     const std::unordered_map<string, int>& stoptimesstoprefs,
                     const vector<pair<string, vector<string>>>& shapelines,
                     const std::unordered_map<string, int>& shaperefs,
                     const vector<pair<string, vector<string>>>& routelines,
                     const std::unordered_map<string, int>& routerefs,
                     const int& precision = 6) {

    ostringstream out;
    out << std::fixed << std::setprecision(precision);


    std::vector<gtfs::trip_segment> tripSegments = fast_gtfs::bin_search::getAllStops(trip_id, stoptimesstoplines, stoptimesstoprefs);
    gtfs::trip tx = fast_gtfs::bin_search::getTripInfo(trip_id, triplines, triprefs);
    gtfs::route bx = fast_gtfs::bin_search::getRouteInfo(tx.route_id, routelines, routerefs);
    std::vector<gtfs::shape> tsx = fast_gtfs::bin_search::getShapeInfo(tx.shape_id, shapelines, shaperefs);

    std::vector<gtfs::stop> stops;

    stops.reserve(tripSegments.size());

    for (gtfs::trip_segment& x : tripSegments) {
        stops.push_back(fast_gtfs::bin_search::getStopInfo(x.stop.stop_id, stoplines, stoprefs));
    }
    const size_t length = stops.size();

    out << "{\n";
    out << "\t\"total\": " << length << ",\n";
    out << "\t\"trip_id\": \"" << gtfs::jsonEscape(tx.trip_id) << "\",\n";
    out << "\t\"route_id\": \"" << gtfs::jsonEscape(tx.route_id) << "\",\n";
    out << "\t\"route_short_name\": \"" << gtfs::jsonEscape(bx.route_short_name) << "\",\n";
    out << "\t\"route_long_name\": \"" << gtfs::jsonEscape(bx.route_long_name) << "\",\n";
    out << "\t\"route_color\": \"#" << gtfs::jsonEscape(bx.route_color) << "\",\n";

    out << "\t\"stops\": [\n";
    for (int i = 0; i < length; i++) {
        const gtfs::stop& x = stops[i];
        const gtfs::trip_segment& y = tripSegments[i];
        out << "\t\t{ \"lat\": " << x.stop_lat <<
                ", \"lng\": " << x.stop_lon <<
                ", \"code\": \""<< gtfs::jsonEscape(x.stop_code) <<
                "\", \"id\": \"" << gtfs::jsonEscape(x.stop_id) <<
                "\", \"name\": \"" << gtfs::jsonEscape(x.stop_name) <<
                "\", \"time\": \"" << y.stop.arrival_time.leadingRoundedTime() << "\"" <<
                ", \"stop_sequence\": " << y.stop.stop_sequence <<
                ", \"pickup_type\": " << std::max(0, static_cast<int>(y.stop.pickup_type)) <<
                ", \"drop_off_type\": " << std::max(0, static_cast<int>(y.stop.drop_off_type)) <<
                (i == (length - 1) ? " }\n" : " },\n");
    }


    out << "\t],\n";

    const size_t tx_length = tsx.size();

    out << "\t\"pos_markers\": [\n";
    for (int i = 0; i < tx_length; i++) {
        const gtfs::shape& x = tsx[i];
        out << "\t\t{ \"lat\": " << x.shape_pt_lat <<
                ", \"lng\": " << x.shape_pt_lon <<
                ", \"sequence\": " << x.shape_pt_sequence <<
                (i == (tx_length - 1) ? " }\n" : " },\n");
    }

    out << "\t]\n}\n";

    return out.str();
}

// Every distinct shape a route runs on the given service day (busiest first) and every stop those
// trips serve, each with its trip count, so the route map can weigh variants by how often they run.
// Trips without a shape_id are grouped by their stop sequence and drawn stop to stop. If nothing
// runs that day, every trip of the route is counted instead ("all_days": true).
inline string getRouteMap(const string& route_id, const int& year, const int& month, const int& day,
                     const vector<pair<string, vector<string>>>& triplines,
                     const std::unordered_map<string, int>& triprefs,
                     const vector<pair<string, vector<string>>>& stoptimesstoplines,
                     const std::unordered_map<string, int>& stoptimesstoprefs,
                     const vector<pair<string, vector<string>>>& shapelines,
                     const std::unordered_map<string, int>& shaperefs,
                     const vector<pair<string, vector<string>>>& calendarlines,
                     const std::unordered_map<string, int>& calendarrefs,
                     const vector<pair<string, vector<string>>>& calendardatelines,
                     const std::unordered_map<string, int>& calendardaterefs,
                     const vector<pair<string, vector<string>>>& stoplines,
                     const std::unordered_map<string, int>& stoprefs,
                     const vector<pair<string, vector<string>>>& routelines,
                     const std::unordered_map<string, int>& routerefs,
                     const int& precision = 6) {

    ostringstream out;
    out << std::fixed << std::setprecision(precision);

    const int routeCol = triprefs.at("route_id");
    const int serviceCol = triprefs.at("service_id");
    auto optionalCol = [&](const char* key) { auto find = triprefs.find(key); return find == triprefs.end() ? -1 : find->second; };
    const int shapeCol = optionalCol("shape_id");
    const int headsignCol = optionalCol("trip_headsign");
    const int directionCol = optionalCol("direction_id");

    // trips.txt is sorted by trip_id, not route_id, so this is one pass over every trip
    vector<const pair<string, vector<string>>*> allTrips, dayTrips;
    std::unordered_map<string, bool> runs; // service_id -> runs on the date
    for (const auto& row : triplines) {
        if (row.second[routeCol] != route_id) continue;
        allTrips.push_back(&row);
        const string& service_id = row.second[serviceCol];
        auto find = runs.find(service_id);
        if (find == runs.end()) {
            find = runs.emplace(service_id, fast_gtfs::bin_search::isServiceActive(service_id, year, month, day,
                calendarlines, calendarrefs, calendardatelines, calendardaterefs)).first;
        }
        if (find->second) dayTrips.push_back(&row);
    }
    const bool allDays = dayTrips.empty();
    const auto& counted = allDays ? allTrips : dayTrips;

    struct variant {
        string shape_id;
        int trips = 0;
        int direction_id = -1;
        std::map<string, int> headsigns;
        vector<string> stopIds; // the first trip's calls, for drawing when there are no shape points
    };
    std::map<string, variant> variants; // keyed by shape_id, or "stops:" + call sequence when unshaped
    std::unordered_map<string, int> stopTrips;
    vector<string> stopOrder; // first-seen order, so the list follows the route

    for (const auto* row : counted) {
        const vector<gtfs::trip_segment> calls = fast_gtfs::bin_search::getAllStops(row->first, stoptimesstoplines, stoptimesstoprefs);
        std::unordered_set<string> seen; // a loop that calls twice still counts the trip once
        string pattern;
        for (const gtfs::trip_segment& x : calls) {
            pattern += x.stop.stop_id;
            pattern += ',';
            if (seen.insert(x.stop.stop_id).second && stopTrips[x.stop.stop_id]++ == 0) stopOrder.push_back(x.stop.stop_id);
        }
        const string shape_id = shapeCol >= 0 ? row->second[shapeCol] : "";
        variant& v = variants[shape_id.empty() ? "stops:" + pattern : shape_id];
        if (v.trips++ == 0) {
            v.shape_id = shape_id;
            for (const gtfs::trip_segment& x : calls) v.stopIds.push_back(x.stop.stop_id);
        }
        if (headsignCol >= 0) v.headsigns[row->second[headsignCol]]++;
        if (directionCol >= 0 && v.direction_id < 0 && !row->second[directionCol].empty()) v.direction_id = gtfs::to_integer(row->second[directionCol]);
    }

    vector<const variant*> ordered;
    for (const auto& [key, v] : variants) ordered.push_back(&v);
    std::stable_sort(ordered.begin(), ordered.end(), [](const variant* a, const variant* b) { return a->trips > b->trips; });

    std::unordered_map<string, gtfs::stop> stopInfo;
    auto stopOf = [&](const string& id) -> const gtfs::stop& {
        auto find = stopInfo.find(id);
        if (find == stopInfo.end()) find = stopInfo.emplace(id, fast_gtfs::bin_search::getStopInfo(id, stoplines, stoprefs)).first;
        return find->second;
    };

    const gtfs::route rx = fast_gtfs::bin_search::getRouteInfo(route_id, routelines, routerefs);

    out << "{\n";
    out << "\t\"route_id\": \"" << gtfs::jsonEscape(route_id) << "\",\n";
    out << "\t\"route_short_name\": \"" << gtfs::jsonEscape(rx.route_short_name) << "\",\n";
    out << "\t\"route_long_name\": \"" << gtfs::jsonEscape(rx.route_long_name) << "\",\n";
    out << "\t\"route_color\": \"" << gtfs::jsonEscape(rx.route_color) << "\",\n";
    out << "\t\"route_text_color\": \"" << gtfs::jsonEscape(rx.route_text_color) << "\",\n";
    out << "\t\"trips\": " << counted.size() << ",\n";
    out << "\t\"all_days\": " << (allDays ? "true" : "false") << ",\n";

    out << "\t\"shapes\": [\n";
    for (size_t i = 0; i < ordered.size(); i++) {
        const variant& v = *ordered[i];
        string headsign;
        int best = 0;
        for (const auto& [h, n] : v.headsigns) if (n > best) { best = n; headsign = h; }

        out << "\t\t{ \"shape_id\": \"" << gtfs::jsonEscape(v.shape_id) <<
                "\", \"trips\": " << v.trips <<
                ", \"headsign\": \"" << gtfs::jsonEscape(headsign) <<
                "\", \"direction_id\": " << v.direction_id <<
                ", \"points\": [";
        vector<gtfs::shape> points;
        if (!v.shape_id.empty()) points = fast_gtfs::bin_search::getShapeInfo(v.shape_id, shapelines, shaperefs);
        std::stable_sort(points.begin(), points.end(), [](const gtfs::shape& a, const gtfs::shape& b) {
            return a.shape_pt_sequence < b.shape_pt_sequence;
        });
        bool first = true;
        if (points.size() > 1) {
            for (const gtfs::shape& pt : points) {
                out << (first ? "" : ",") << "[" << pt.shape_pt_lat << "," << pt.shape_pt_lon << "]";
                first = false;
            }
        } else {
            for (const string& id : v.stopIds) {
                const gtfs::stop& st = stopOf(id);
                out << (first ? "" : ",") << "[" << st.stop_lat << "," << st.stop_lon << "]";
                first = false;
            }
        }
        out << "] }" << (i == ordered.size() - 1 ? "\n" : ",\n");
    }
    out << "\t],\n";

    out << "\t\"stops\": [\n";
    for (size_t i = 0; i < stopOrder.size(); i++) {
        const gtfs::stop& st = stopOf(stopOrder[i]);
        out << "\t\t{ \"stop_id\": \"" << gtfs::jsonEscape(stopOrder[i]) <<
                "\", \"stop_code\": \"" << gtfs::jsonEscape(st.stop_code) <<
                "\", \"stop_name\": \"" << gtfs::jsonEscape(st.stop_name) <<
                "\", \"lat\": " << st.stop_lat <<
                ", \"lon\": " << st.stop_lon <<
                ", \"trips\": " << stopTrips[stopOrder[i]] <<
                (i == stopOrder.size() - 1 ? " }\n" : " },\n");
    }
    out << "\t]\n}\n";

    return out.str();
}

inline string getStopDayTimes(const string& stop_id, const int& year, const int& month, const int& day,
                     const vector<pair<string, vector<string>>>& stoptimesstopidlines,
                     const std::unordered_map<string, int>& stoptimesstopidrefs,
                     const vector<pair<string, vector<string>>>& triplines,
                     const std::unordered_map<string, int>& triprefs,
                     const vector<pair<string, vector<string>>>& calendarlines,
                     const std::unordered_map<string, int>& calendarrefs,
                     const vector<pair<string, vector<string>>>& calendardatelines,
                     const std::unordered_map<string, int>& calendardaterefs,
                     const vector<pair<string, vector<string>>>& stoplines,
                     const std::unordered_map<string, int>& stoprefs,
                     const vector<pair<string, vector<string>>>& routelines,
                     const std::unordered_map<string, int>& routerefs) {

    ostringstream out;

    const gtfs::stop st = fast_gtfs::bin_search::getStopInfo(stop_id, stoplines, stoprefs);

    std::vector<gtfs::trip_segment> segments = fast_gtfs::bin_search::getDayTimesAtStop(stop_id, year, month, day,
        stoptimesstopidlines, stoptimesstopidrefs, triplines, triprefs,
        calendarlines, calendarrefs, calendardatelines, calendardaterefs);

    std::sort(segments.begin(), segments.end(), [](const gtfs::trip_segment& a, const gtfs::trip_segment& b) {
        return a.stop.arrival_time < b.stop.arrival_time;
    });

    const size_t length = segments.size();

    out << "{\n\t\"stop_id\": \"" << gtfs::jsonEscape(stop_id) << "\",\n"
            << "\t\"stop_code\": \"" << gtfs::jsonEscape(st.stop_code) << "\",\n"
            << "\t\"stop_name\": \"" << gtfs::jsonEscape(st.stop_name) << "\",\n"
            << "\t\"lat\": " << st.stop_lat << ",\n"
            << "\t\"lon\": " << st.stop_lon << ",\n";

    out << "\t\"departures\": [\n";
    for (size_t i = 0; i < length; i++) {
        const gtfs::trip_segment& x = segments[i];

        gtfs::route routeInfo = fast_gtfs::bin_search::getRouteInfo(x.route_id, routelines, routerefs);
        gtfs::trip tripInfo = fast_gtfs::bin_search::getTripInfo(x.stop.trip_id, triplines, triprefs);

        out << "\t\t{ \"route_id\": \"" << gtfs::jsonEscape(routeInfo.route_short_name) <<
                "\", \"arrival_time\": \"" << x.stop.arrival_time.leadingRoundedTime() <<
                "\", \"trip_id\": \"" << gtfs::jsonEscape(x.stop.trip_id) <<
                "\", \"trip_headsign\": \"" << gtfs::jsonEscape(tripInfo.trip_headsign) <<
                "\", \"direction_id\": " << tripInfo.direction_id <<
                ", \"pickup_type\": " << std::max(0, static_cast<int>(x.stop.pickup_type)) <<
                ", \"drop_off_type\": " << std::max(0, static_cast<int>(x.stop.drop_off_type)) <<
                ", \"route_color\": \"" << gtfs::jsonEscape(routeInfo.route_color) <<
                (i == (length - 1) ? "\" } \n" : "\" }, \n");
    }
    out << "\t]\n}\n";

    return out.str();
}
// "12,34,,12" -> {"12", "34"}: comma list from a URL segment, blanks and repeats dropped
inline vector<string> splitIdList(const string& list) {
    vector<string> out;
    std::istringstream in(list);
    string id;
    while (std::getline(in, id, ',')) {
        if (!id.empty() && std::find(out.begin(), out.end(), id) == out.end()) out.push_back(id);
    }
    return out;
}

// Direct trips that call at one of fromIds and later at one of toIds on the given service day.
// Each trip is listed once, boarding at the From stop closest before the first reachable To stop
// (the shortest ride), so passing both sides of a street only keeps the direction that gets there.
inline string getBetweenDayTimes(const vector<string>& fromIds, const vector<string>& toIds,
                     const int& year, const int& month, const int& day,
                     const vector<pair<string, vector<string>>>& stoptimesstopidlines,
                     const std::unordered_map<string, int>& stoptimesstopidrefs,
                     const vector<pair<string, vector<string>>>& triplines,
                     const std::unordered_map<string, int>& triprefs,
                     const vector<pair<string, vector<string>>>& calendarlines,
                     const std::unordered_map<string, int>& calendarrefs,
                     const vector<pair<string, vector<string>>>& calendardatelines,
                     const std::unordered_map<string, int>& calendardaterefs,
                     const vector<pair<string, vector<string>>>& stoplines,
                     const std::unordered_map<string, int>& stoprefs,
                     const vector<pair<string, vector<string>>>& routelines,
                     const std::unordered_map<string, int>& routerefs,
                     const int& precision = 6) {

    ostringstream out;
    out << std::fixed << std::setprecision(precision);

    // trip_id -> that trip's calls at the From / To stops
    std::unordered_map<string, pair<vector<gtfs::trip_segment>, vector<gtfs::trip_segment>>> calls;
    auto collect = [&](const vector<string>& ids, const bool isFrom) {
        for (const string& id : ids) {
            for (gtfs::trip_segment& x : fast_gtfs::bin_search::getDayTimesAtStop(id, year, month, day,
                    stoptimesstopidlines, stoptimesstopidrefs, triplines, triprefs,
                    calendarlines, calendarrefs, calendardatelines, calendardaterefs)) {
                auto& entry = calls[x.stop.trip_id];
                (isFrom ? entry.first : entry.second).push_back(std::move(x));
            }
        }
    };
    collect(fromIds, true);
    collect(toIds, false);

    // an unset time parses as -1:-1:-1 (or stays 00:00:00 when the column is missing)
    auto isSet = [](const gtfs::time& t) { return t.h >= 0 && !(t == gtfs::time()); };

    struct ride { gtfs::trip_segment from, to; gtfs::time depart, arrive; };
    vector<ride> rides;
    for (auto& [trip_id, fromTo] : calls) {
        auto& [boards, alights] = fromTo;
        if (boards.empty() || alights.empty()) continue;
        std::sort(alights.begin(), alights.end(), [](const gtfs::trip_segment& a, const gtfs::trip_segment& b) {
            return a.stop.stop_sequence < b.stop.stop_sequence;
        });
        for (const gtfs::trip_segment& to : alights) {
            if (to.stop.drop_off_type == gtfs::stop_time::not_available) continue;
            const gtfs::trip_segment* best = nullptr;
            for (const gtfs::trip_segment& from : boards) {
                if (from.stop.pickup_type == gtfs::stop_time::not_available) continue;
                if (from.stop.stop_sequence >= to.stop.stop_sequence) continue;
                if (!best || from.stop.stop_sequence > best->stop.stop_sequence) best = &from;
            }
            if (!best) continue;
            const gtfs::time depart = isSet(best->stop.departure_time) ? best->stop.departure_time : best->stop.arrival_time;
            const gtfs::time arrive = isSet(to.stop.arrival_time) ? to.stop.arrival_time : to.stop.departure_time;
            if (depart.h >= 0 && arrive.h >= 0) rides.push_back({*best, to, depart, arrive});
            break;
        }
    }

    std::sort(rides.begin(), rides.end(), [](const ride& a, const ride& b) { return a.depart < b.depart; });

    auto printStops = [&](const char* key, const vector<string>& ids) {
        out << "\t\"" << key << "\": [\n";
        for (size_t i = 0; i < ids.size(); i++) {
            const gtfs::stop st = fast_gtfs::bin_search::getStopInfo(ids[i], stoplines, stoprefs);
            out << "\t\t{ \"stop_id\": \"" << gtfs::jsonEscape(ids[i]) <<
                    "\", \"stop_code\": \"" << gtfs::jsonEscape(st.stop_code) <<
                    "\", \"stop_name\": \"" << gtfs::jsonEscape(st.stop_name) <<
                    "\", \"lat\": " << st.stop_lat <<
                    ", \"lon\": " << st.stop_lon <<
                    (i == (ids.size() - 1) ? " }\n" : " },\n");
        }
        out << "\t],\n";
    };

    out << "{\n";
    printStops("from_stops", fromIds);
    printStops("to_stops", toIds);

    const size_t length = rides.size();
    out << "\t\"trips\": [\n";
    for (size_t i = 0; i < length; i++) {
        const ride& r = rides[i];

        gtfs::route routeInfo = fast_gtfs::bin_search::getRouteInfo(r.from.route_id, routelines, routerefs);
        gtfs::trip tripInfo = fast_gtfs::bin_search::getTripInfo(r.from.stop.trip_id, triplines, triprefs);

        out << "\t\t{ \"route_id\": \"" << gtfs::jsonEscape(routeInfo.route_short_name) <<
                "\", \"route_color\": \"" << gtfs::jsonEscape(routeInfo.route_color) <<
                "\", \"trip_id\": \"" << gtfs::jsonEscape(r.from.stop.trip_id) <<
                "\", \"trip_headsign\": \"" << gtfs::jsonEscape(tripInfo.trip_headsign) <<
                "\", \"direction_id\": " << tripInfo.direction_id <<
                ", \"from_stop_id\": \"" << gtfs::jsonEscape(r.from.stop.stop_id) <<
                "\", \"to_stop_id\": \"" << gtfs::jsonEscape(r.to.stop.stop_id) <<
                "\", \"from_sequence\": " << r.from.stop.stop_sequence <<
                ", \"to_sequence\": " << r.to.stop.stop_sequence <<
                ", \"depart_time\": \"" << r.depart.leadingRoundedTime() <<
                "\", \"arrive_time\": \"" << r.arrive.leadingRoundedTime() <<
                (i == (length - 1) ? "\" }\n" : "\" },\n");
    }
    out << "\t]\n}\n";

    return out.str();
}

// HH:MM:SS for seconds after the service day's midnight (past 24:00 stays past 24:00, like stop_times)
inline string secsToTime(const int secs) {
    return gtfs::time(secs / 3600, secs / 60 % 60, static_cast<float>(secs % 60)).leadingRoundedTime();
}

// Journeys (direct or with transfers, walking up to planner::WALK_MAX_M between stops) from any of
// fromIds to any of toIds, leaving in [after, until] seconds after the date's midnight. Trips of the
// previous service day are included shifted back a day (prev_day), so early-morning queries see them.
inline string getJourneyDayTimes(const planner::index& idx, const vector<string>& fromIds, const vector<string>& toIds,
                     int year, int month, int day, const int after, const int until, const int limit,
                     const vector<pair<string, vector<string>>>& triplines,
                     const std::unordered_map<string, int>& triprefs,
                     const vector<pair<string, vector<string>>>& calendarlines,
                     const std::unordered_map<string, int>& calendarrefs,
                     const vector<pair<string, vector<string>>>& calendardatelines,
                     const std::unordered_map<string, int>& calendardaterefs,
                     const vector<pair<string, vector<string>>>& stoplines,
                     const std::unordered_map<string, int>& stoprefs,
                     const vector<pair<string, vector<string>>>& routelines,
                     const std::unordered_map<string, int>& routerefs,
                     const int& precision = 6) {

    ostringstream out;
    out << std::fixed << std::setprecision(precision);

    auto toIndices = [&](const vector<string>& ids) {
        vector<int> v;
        for (const string& id : ids) {
            auto f = idx.stopIndex.find(id);
            if (f != idx.stopIndex.end()) v.push_back(f->second);
        }
        return v;
    };
    const vector<int> from = toIndices(fromIds), to = toIndices(toIds);

    const vector<char> today = planner::activeServices(idx, year, month, day,
        calendarlines, calendarrefs, calendardatelines, calendardaterefs);
    planner::previousDay(year, month, day);
    const vector<char> yesterday = planner::activeServices(idx, year, month, day,
        calendarlines, calendarrefs, calendardatelines, calendardaterefs);

    const vector<planner::journey> journeys = planner::plan(idx, from, to, today, yesterday, after, until, limit);

    const planner::walk_pair walk = planner::directWalk(idx, from, to);

    auto printStops = [&](const char* key, const vector<string>& ids) {
        out << "\t\"" << key << "\": [\n";
        for (size_t i = 0; i < ids.size(); i++) {
            const gtfs::stop st = fast_gtfs::bin_search::getStopInfo(ids[i], stoplines, stoprefs);
            out << "\t\t{ \"stop_id\": \"" << gtfs::jsonEscape(ids[i]) <<
                    "\", \"stop_code\": \"" << gtfs::jsonEscape(st.stop_code) <<
                    "\", \"stop_name\": \"" << gtfs::jsonEscape(st.stop_name) <<
                    "\", \"lat\": " << st.stop_lat <<
                    ", \"lon\": " << st.stop_lon <<
                    (i == (ids.size() - 1) ? " }\n" : " },\n");
        }
        out << "\t],\n";
    };

    // every stop a leg starts or ends at, for names on transfer points
    vector<string> legStops;
    for (const planner::journey& j : journeys) for (const planner::leg& l : j.legs) {
        for (int s : {l.fromStop, l.toStop}) {
            const string& id = idx.stops[s].id;
            if (std::find(legStops.begin(), legStops.end(), id) == legStops.end()) legStops.push_back(id);
        }
    }

    out << "{\n";
    printStops("from_stops", fromIds);
    printStops("to_stops", toIds);
    printStops("stops", legStops);

    out << "\t\"walk_only\": ";
    if (walk.from >= 0) {
        out << "{ \"from_stop_id\": \"" << gtfs::jsonEscape(idx.stops[walk.from].id) <<
                "\", \"to_stop_id\": \"" << gtfs::jsonEscape(idx.stops[walk.to].id) <<
                "\", \"distance_m\": " << std::lround(walk.metres) <<
                ", \"walk_secs\": " << planner::walkSecs(walk.metres) << " },\n";
    } else {
        out << "null,\n";
    }

    std::unordered_map<string, gtfs::route> routes;
    out << "\t\"journeys\": [\n";
    for (size_t i = 0; i < journeys.size(); i++) {
        const planner::journey& j = journeys[i];
        out << "\t\t{ \"depart_time\": \"" << secsToTime(j.depart) <<
                "\", \"arrive_time\": \"" << secsToTime(j.arrive) <<
                "\", \"transfers\": " << j.transfers << ", \"legs\": [\n";
        for (size_t k = 0; k < j.legs.size(); k++) {
            const planner::leg& l = j.legs[k];
            out << "\t\t\t{ \"type\": \"" << (l.walk ? "walk" : "ride") <<
                    "\", \"from_stop_id\": \"" << gtfs::jsonEscape(idx.stops[l.fromStop].id) <<
                    "\", \"to_stop_id\": \"" << gtfs::jsonEscape(idx.stops[l.toStop].id) <<
                    "\", \"depart_time\": \"" << secsToTime(l.depart) <<
                    "\", \"arrive_time\": \"" << secsToTime(l.arrive) << "\"";
            if (l.walk) {
                out << ", \"distance_m\": " << l.metres;
            } else {
                const planner::trip_node& tn = idx.trips[l.trip];
                auto r = routes.find(tn.route_id);
                if (r == routes.end()) r = routes.emplace(tn.route_id, fast_gtfs::bin_search::getRouteInfo(tn.route_id, routelines, routerefs)).first;
                const gtfs::trip tripInfo = fast_gtfs::bin_search::getTripInfo(tn.trip_id, triplines, triprefs);
                out << ", \"route_id\": \"" << gtfs::jsonEscape(r->second.route_short_name) <<
                        "\", \"route_color\": \"" << gtfs::jsonEscape(r->second.route_color) <<
                        "\", \"trip_id\": \"" << gtfs::jsonEscape(tn.trip_id) <<
                        "\", \"trip_headsign\": \"" << gtfs::jsonEscape(tripInfo.trip_headsign) <<
                        "\", \"direction_id\": " << tripInfo.direction_id <<
                        ", \"from_sequence\": " << idx.calls[tn.first + l.boardPos].seq <<
                        ", \"to_sequence\": " << idx.calls[tn.first + l.alightPos].seq <<
                        ", \"prev_day\": " << (l.prevDay ? "true" : "false");
            }
            out << (k == (j.legs.size() - 1) ? " }\n" : " },\n");
        }
        out << (i == (journeys.size() - 1) ? "\t\t] }\n" : "\t\t] },\n");
    }
    out << "\t]\n}\n";

    return out.str();
}
