#include <iostream>
#include <vector>
#include <string>
#include <iomanip>
#include <cstdlib>
#include "../../static-gtfs/gtfs.hpp"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: \n" << argv[0] << " <stopID>";
        return -1;
    }

    gtfs::stop st = gtfs::getStopInfo(argv[1]);

    std::cout << "{\n\t\"stop_id\": " << argv[1] << ",\n"
                << "\t\"stop_code\": " << st.stop_code << ",\n"
                << "\t\"stop_name\": \"" << st.stop_name << "\",\n"
                << "\t\"lat\": " << st.stop_lat << ",\n"
                << "\t\"lon\": " << st.stop_lon << "\n"
                << "}\n";
}
