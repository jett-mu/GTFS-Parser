#include <iostream>
#include <iomanip>
#include <sstream>
#include "../static-gtfs/gtfs.hpp"
#include "fast-gtfs.hpp"

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
    out << "\t\"trip_id\": " << tx.trip_id << ",\n";
    out << "\t\"route_id\": " << tx.route_id << ",\n";
    out << "\t\"route_short_name\": \"" << bx.route_short_name << "\",\n";
    out << "\t\"route_long_name\": \"" << bx.route_long_name << "\",\n";
    out << "\t\"route_color\": \"#" << bx.route_color << "\",\n";

    out << "\t\"stops\": [\n";
    for (int i = 0; i < length; i++) {
        const gtfs::stop& x = stops[i];
        const gtfs::trip_segment& y = tripSegments[i];
        out << "\t\t{ \"lat\": " << x.stop_lat <<
                ", \"lng\": " << x.stop_lon <<
                ", \"code\": \""<< x.stop_code <<
                "\", \"id\": \"" << x.stop_id <<
                "\", \"name\": \"" << x.stop_name <<
                "\", \"time\": \"" << y.stop.arrival_time.leadingRoundedTime() << "\"" <<
                ", \"stop_sequence\": " << y.stop.stop_sequence <<
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

    out << "{\n\t\"stop_id\": " << stop_id << ",\n"
            << "\t\"stop_code\": " << st.stop_code << ",\n"
            << "\t\"stop_name\": \"" << st.stop_name << "\",\n"
            << "\t\"lat\": " << st.stop_lat << ",\n"
            << "\t\"lon\": " << st.stop_lon << ",\n";

    out << "\t\"departures\": [\n";
    for (size_t i = 0; i < length; i++) {
        const gtfs::trip_segment& x = segments[i];

        gtfs::route routeInfo = fast_gtfs::bin_search::getRouteInfo(x.route_id, routelines, routerefs);
        gtfs::trip tripInfo = fast_gtfs::bin_search::getTripInfo(x.stop.trip_id, triplines, triprefs);

        out << "\t\t{ \"route_id\": \"" << routeInfo.route_short_name <<
                "\", \"arrival_time\": \"" << x.stop.arrival_time.leadingRoundedTime() <<
                "\", \"trip_id\": \"" << x.stop.trip_id <<
                "\", \"trip_headsign\": \"" << tripInfo.trip_headsign <<
                "\", \"route_color\": \"" << routeInfo.route_color <<
                (i == (length - 1) ? "\" } \n" : "\" }, \n");
    }
    out << "\t]\n}\n";

    return out.str();
}