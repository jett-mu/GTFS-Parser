#include <iostream>
#include <vector>
#include <string>
#include <iomanip>
#include <cstdlib>
#include "../../static-gtfs/gtfs.hpp"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: \n" << argv[0] << " <stop_name> [--total | -t <totalEntries> = 10]\n";
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

    std::vector<gtfs::matchsearch> matches = gtfs::searchStop(argv[1]);

    std::cout << "{\n\t\"query\": \"" << argv[1] << "\",\n\t\"matches\": [\n";

    const int matchesLen = std::min((int)matches.size(), top);

    for (int i = 0; i < matchesLen; i++) {
        gtfs::matchsearch x = matches[i];
        gtfs::stop st = gtfs::getStopInfo(std::to_string(x.text.num));

        std::cout << "\t\t{ \"stop_id\": " << x.text.num <<
                ", \"stop_name\": \"" << x.text.str <<
                "\", \"score\": " << x.score <<
                ", \"lat\": " << st.stop_lat <<
                ", \"lon\": " << st.stop_lon <<
                ((matchesLen - 1) == i ? " }\n" : " },\n");
    }
    std::cout << "\t]\n}\n";
}
