#include <iostream>
#include <vector>
#include <string>
#include <iomanip>
#include <cstdlib>
#include "../../static-gtfs/gtfs.hpp"


int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: \n" << argv[0] << " <tripID> [--precision | -p <precision>]\n";
        return -1;
    }

    int precision = 6;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "-p" || arg == "--precision") && i + 1 < argc) {
            precision = std::atoi(argv[i + 1]);
            ++i;
        }
    }

    std::cout << std::fixed << std::setprecision(precision);

    string trip_id = argv[1];

    std::vector<gtfs::trip_segment> tripSegments = gtfs::getAllStops(trip_id);
    gtfs::trip tx = gtfs::getTripInfo(trip_id);
    gtfs::route bx = gtfs::getRouteInfo(tx.route_id);
    std::vector<gtfs::shape> tsx = gtfs::getShapeInfo(tx.shape_id);
    
    std::vector<gtfs::stop> stops;

    for (gtfs::trip_segment& x : tripSegments) {
        stops.push_back(gtfs::getStopInfo(x.stop.stop_id));
    }
    int length = stops.size();

    std::cout << "{\n";
    std::cout << "\t\"total\": " << length << ",\n";
    std::cout << "\t\"trip_id\": " << tx.trip_id << ",\n";
    std::cout << "\t\"route_id\": " << tx.route_id << ",\n";
    std::cout << "\t\"route_short_name\": \"" << bx.route_short_name << "\",\n";
    std::cout << "\t\"route_long_name\": \"" << bx.route_long_name << "\",\n";
    std::cout << "\t\"route_color\": \"#" << bx.route_color << "\",\n";
    
    std::cout << "\t\"stops\": [\n";
    for (int i = 0; i < length; i++) {
        gtfs::stop x = stops[i];
        gtfs::trip_segment y = tripSegments[i];
        std::cout << "\t\t{ \"lat\": " << x.stop_lat <<
                ", \"lng\": " << x.stop_lon <<
                ", \"code\": \""<< x.stop_code <<
                "\", \"id\": \"" << x.stop_id <<
                "\", \"name\": \"" << x.stop_name <<
                "\", \"time\": \"" << y.stop.arrival_time.leadingRoundedTime() << "\"" <<
                ", \"stop_sequence\": " << y.stop.stop_sequence <<
                (i == (length - 1) ? " }\n" : " },\n");
    } 

    
    cout << "\t],\n";

    int tx_length = tsx.size();

    std::cout << "\t\"pos_markers\": [\n";
    for (int i = 0; i < tx_length; i++) {
        gtfs::shape x = tsx[i];
        std::cout << "\t\t{ \"lat\": " << x.shape_pt_lat << 
                ", \"lng\": " << x.shape_pt_lon << 
                ", \"sequence\": " << x.shape_pt_sequence << 
                (i == (tx_length - 1) ? " }\n" : " },\n");
    } 
    
    cout << "\t]\n}\n";
    

    

}