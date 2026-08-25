#include <iostream>
#include <vector>
#include <string>
#include <iomanip>
#include <cstdlib>
#include "../../static-gtfs/gtfs.hpp"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: \n" << argv[0] << " <route_number/name> [--total | -t <totalEntries> = 10]\n";
        return -1;
    }

    int top = 10;
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "-t" || arg == "--total") && i + 1 < argc) {
            top = std::atoi(argv[i + 1]);
            ++i;
        }
    }

    std::vector<gtfs::routematch> matches = gtfs::searchRoute(argv[1]);

    std::cout << "{\n\t\"query\": \"" << argv[1] << "\",\n\t\"matches\": [\n";

    const int matchesLen = std::min((int)matches.size(), top);

    for (int i = 0; i < matchesLen; i++) {
        gtfs::routematch x = matches[i];

        std::cout << "\t\t{ \"route_id\": \"" << x.route_id <<
                "\", \"route_short_name\": \"" << x.route_short_name <<
                "\", \"route_long_name\": \"" << x.route_long_name <<
                "\", \"score\": " << x.score <<
                ((matchesLen - 1) == i ? " }\n" : " },\n");
    }
    std::cout << "\t]\n}\n";
}
