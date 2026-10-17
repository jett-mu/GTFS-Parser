#include <iostream>
#include <vector>
#include <string>
#include <iomanip>
#include <cstdlib>
#include "../../config/config.hpp"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: \n" << argv[0] << " <routeID>";
        return -1;
    }

    const gtfs::data_feed feed = config::loadOrExit();

    gtfs::route rt = gtfs::getRouteInfo(feed, argv[1]);

    if (rt.route_id.empty()) {
        std::cerr << "Route not found: " << argv[1] << "\n";
        return 1;
    }

    std::cout << "{\n\t\"route_id\": \"" << gtfs::jsonEscape(rt.route_id) << "\",\n"
                << "\t\"agency_id\": \"" << gtfs::jsonEscape(rt.agency_id) << "\",\n"
                << "\t\"route_short_name\": \"" << gtfs::jsonEscape(rt.route_short_name) << "\",\n"
                << "\t\"route_long_name\": \"" << gtfs::jsonEscape(rt.route_long_name) << "\",\n"
                << "\t\"route_desc\": \"" << gtfs::jsonEscape(rt.route_desc) << "\",\n"
                << "\t\"route_type\": " << static_cast<int>(rt.route_type) << ",\n"
                << "\t\"route_url\": \"" << gtfs::jsonEscape(rt.route_url) << "\",\n"
                << "\t\"route_color\": \"" << gtfs::jsonEscape(rt.route_color) << "\",\n"
                << "\t\"route_text_color\": \"" << gtfs::jsonEscape(rt.route_text_color) << "\"\n"
                << "}\n";
}
