#ifndef GTFS_PARSER_FAST_CONFIG_HPP
#define GTFS_PARSER_FAST_CONFIG_HPP

#include <string>
using std::string;

namespace fast_config {

    string fast_root = "/Users/jettmu/Documents/VSCode/GTFS Parser/fast-static-gtfs/test-data/";
    string fast_stop_path = fast_root + "stops.txt";
    string fast_trip_path = fast_root + "trips.txt";
    string fast_trip_trip_id = fast_root + "trips_trip_id.txt";
    string fast_stop_stop_id = fast_root + "stops_stop_id.txt";
    string fast_stop_times_path = fast_root + "stop_times.txt";
    string fast_stop_times_trip_id = fast_root + "stop_times_trip_id.txt";
    string fast_shape_path = fast_root + "shapes.txt";
    string fast_shape_shape_id = fast_root + "shapes_shape_id.txt";
    string fast_stop_times_stop_id = fast_root + "stop_times_stop_id.txt";

}



#endif //GTFS_PARSER_FAST_CONFIG_HPP
