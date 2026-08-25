#include <iostream>
#include <vector>
#include <string>
#include <iomanip>
#include <cstdlib>
#include "../../static-gtfs/gtfs.hpp"
#include <ctime>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: \n" << argv[0] << " <stopID> [year, month, day]";
        return -1;
    }

    gtfs::stop st;

    st = gtfs::getStopInfo(argv[1]);

    int year, month, day;

    if (argc == 2) {
        std::time_t rn = std::time(0);
        std::tm* ltm = std::localtime(&rn);
        year = 1900 + ltm->tm_year;
        month = 1 + ltm->tm_mon;
        day = ltm->tm_mday;
    } else if (argc == 5) {
        year = std::stoi(argv[2]);
        month = std::stoi(argv[3]);
        day = std::stoi(argv[4]);
    } else {
        std::cerr << "Usage: \n" << argv[0] << " <stopID> [year, month, day]";
        return -1;
    }

    std::vector<gtfs::trip_segment> ts = gtfs::getDayTimesAtStop(st.stop_id,year, month, day);

    std::cout << "{\n\t\"stop_id\": " << argv[1] << ",\n"
                << "\t\"stop_code\": " << st.stop_code << ",\n"
                << "\t\"stop_name\": \"" << st.stop_name << "\",\n"
                << "\t\"lat\": " << st.stop_lat << ",\n"
                << "\t\"lon\": " << st.stop_lon << ",\n";
    
    std::cout << "\t\"departures\": [\n";

    const int tsLen = ts.size();
    for (int i = 0; i < tsLen; i++) {
        gtfs::trip_segment x = ts[i];

        gtfs::route routeInfo = gtfs::getRouteInfo(x.route_id);
        cout << "\t\t{ \"route_id\": \"" << routeInfo.route_short_name << 
                "\", \"arrival_time\": \"" << x.stop.arrival_time.leadingRoundedTime() << 
                "\", \"trip_id\": \"" << x.stop.trip_id << 
                "\", \"trip_headsign\": \"" << gtfs::getTripInfo(x.stop.trip_id).trip_headsign << 
                "\", \"route_color\": \"" << routeInfo.route_color << 
                (i == (tsLen-1) ? "\" } \n" : "\" }, \n");
    }

    cout << "\t]\n}\n";

}