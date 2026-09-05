#ifndef GTFS_PARSER_FAST_CONFIG_HPP
#define GTFS_PARSER_FAST_CONFIG_HPP

#include <string>
using std::string;

namespace fast_config {

    string fast_root = "/absolute/path/to/GTFS Parser/fast-static-gtfs/test-data/";
    string fast_stop_path = fast_root + "stops.txt";
    string fast_trip_path = fast_root + "trips.txt";
    string fast_trip_trip_id = fast_root + "trips_trip_id.txt";
    string fast_stop_stop_id = fast_root + "stops_stop_id.txt";
    string fast_stop_times_path = fast_root + "stop_times.txt";
    string fast_stop_times_trip_id = fast_root + "stop_times_trip_id.txt";
    string fast_shape_path = fast_root + "shapes.txt";
    string fast_shape_shape_id = fast_root + "shapes_shape_id.txt";
    string fast_stop_times_stop_id = fast_root + "stop_times_stop_id.txt";
    string fast_calendar_path = fast_root + "calendar.txt";
    string fast_calendar_service_id = fast_root + "calendar_service_id.txt";
    string fast_calendar_dates_path = fast_root + "calendar_dates.txt";
    string fast_calendar_dates_service_id = fast_root + "calendar_dates_service_id.txt";
    string fast_route_path = fast_root + "routes.txt";
    string fast_route_route_id = fast_root + "routes_route_id.txt";

}



#endif //GTFS_PARSER_FAST_CONFIG_HPP
