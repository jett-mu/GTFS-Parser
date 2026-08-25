#include "../../static-gtfs/gtfs.hpp"

#include <iostream>
#include <unordered_set>
using namespace gtfs;
using namespace std;

// Single pass over stop_times.txt building, for each trip_id in wanted,
// the departure_time of its lowest stop_sequence (i.e. its first stop).
// Avoids re-scanning the whole file once per trip (what getAllStops does).
unordered_map<string, gtfs::time> getFirstDepartures(const unordered_set<string>& wanted) {
    unordered_map<string, gtfs::time> firstDeparture;
    unordered_map<string, int> firstSeq;

    ifstream stopTimesFile(stopTimesPath);
    string currentLine;
    bool firstLine = true;
    unordered_map<string, int> refs;

    while (getline(stopTimesFile, currentLine)) {
        vector<string> parsedCurrentLine = parseDataCSV(currentLine);
        if (firstLine) {
            refs = createMapFromVector(parsedCurrentLine);
            firstLine = false;
            continue;
        }

        const string& trip_id = parsedCurrentLine[refs["trip_id"]];
        if (wanted.find(trip_id) == wanted.end()) continue;

        int seq = to_integer(parsedCurrentLine[refs["stop_sequence"]]);
        auto it = firstSeq.find(trip_id);
        if (it == firstSeq.end() || seq < it->second) {
            firstSeq[trip_id] = seq;
            auto find = refs.find("departure_time");
            firstDeparture[trip_id] = (find != refs.end())
                ? parseFormattedTime(parsedCurrentLine[find->second])
                : gtfs::time();
        }
    }

    return firstDeparture;
}

int main(int argc, char* argv[]) {
    if (argc != 5) {
        std::cerr << "Usage: " << argv[0] << " <route_id> <year> <month> <day>\n";
        return -1;
    }
    string trip_id = argv[1];

    vector<trip> x = getAllTrips(trip_id);

    for (trip& x_trip : x) {
        x_trip = getTripInfo(x_trip.trip_id);
    }

    vector<service> y;

    for (trip& x_trip : x) {
        y.push_back(getServiceInfo(x_trip.service_id));
    }

    int i = 0;
    while (i < y.size()) {
        service& service_y = y[i];
        bool remove = false;

        if (service_y.schedule.start_date > getToday() || service_y.schedule.end_date < getToday()) {
            remove = true;
        }

        switch (convertDateToWeek(stoi(argv[2]), stoi(argv[3]), stoi(argv[4]))) {
            case mon: if (!service_y.schedule.monday) remove = true; break;
            case tue: if (!service_y.schedule.tuesday) remove = true; break;
            case wed: if (!service_y.schedule.wednesday) remove = true; break;
            case thu: if (!service_y.schedule.thursday) remove = true; break;
            case fri: if (!service_y.schedule.friday) remove = true; break;
            case sat: if (!service_y.schedule.saturday) remove = true; break;
            case sun: if (!service_y.schedule.sunday) remove = true; break;
        }

        for (gtfs::calendar_date special_day : service_y.exceptions) {
            if (special_day.date == getToday()) {
                if (special_day.exception_type == gtfs::calendar_date::removed)  remove = true;
                if (special_day.exception_type == gtfs::calendar_date::added) remove = false;
            }
        }

        if (remove) {
            x.erase(x.begin() + i);
            y.erase(y.begin() + i);
            // don't increment i, the next element has shifted into position i
        } else {
            i++;
        }
    }
    unordered_set<string> wantedTripIds;
    for (trip& x_trip : x) wantedTripIds.insert(x_trip.trip_id);
    unordered_map<string, gtfs::time> firstDepartures = getFirstDepartures(wantedTripIds);

    sort(x.begin(), x.end(), [&](const trip& a, const trip& b) {
        return firstDepartures[a.trip_id] < firstDepartures[b.trip_id];
    });

    cout << "{\n\t\"query_route_id\": \"" << argv[1] << "\",\n\t\"trips\": [\n";
    for (int i = 0; i < x.size(); i++) {
        trip x_a = x[i];
        string first_departure = firstDepartures.count(x_a.trip_id) ? firstDepartures[x_a.trip_id].leadingRoundedTime() : "";
        cout << "\t\t{ \"trip_id\": \"" << x_a.trip_id <<
                "\", \"trip_headsign\": \"" << x_a.trip_headsign <<
                "\", \"departure_time\": \"" << first_departure << "\" }" <<
                ((i == (x.size() - 1)) ? "\n" : ",\n");
    }
    cout << "\t]\n}\n";

    return 0;
}