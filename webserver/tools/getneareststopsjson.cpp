#include <iostream>
#include <vector>
#include <string>
#include <iomanip>
#include <cstdlib>
#include <cstdio>
#include "../../static-gtfs/gtfs.hpp"

// Escape a string for use inside a JSON string literal (stop names can
// contain quotes; the frontend parses this output with JSON.parse).
static std::string jsonEscape(const std::string& in) {
    std::string out;
    out.reserve(in.size() + 8);
    for (unsigned char c : in) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}


int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cout << "Usage: \n" << argv[0] << " <lat> <lon> [--precision | -p <precision> = 6] [--total | -t <totalEntries> = 10]\n";
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
    int top = 10;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "-t" || arg == "--total") && i + 1 < argc) {
            top = std::atoi(argv[i + 1]);
            ++i;
        }
    }

    std::cout << std::fixed << std::setprecision(precision);
    double lat, lon;
    
    try {
        lat = std::stod(argv[1]);
        lon = std::stod(argv[2]);
    } catch (std::invalid_argument& err) {
        std::cerr << "invalid coordinates\n" << std::flush;
        return 1;
    } catch (std::out_of_range& err) {
        std::cerr << "invalid coordinates, out of range\n" << std::flush;
        return 2;
    } catch (...) {
        std::cerr << "if you are reading this its prolly my fault just email me\n" << std::flush;
        return -1;
    }

    std::vector<gtfs::stop> ns = gtfs::getNearestStops(lat, lon);

    cout << "{\n\t\"request_lat\": "<< lat << ",\n\t\"request_lon\": " << lon << ",\n\t\"nearest_stops\": [\n";

    // Never read past the end of the vector when fewer stops exist than requested.
    const int ns_len = std::min(static_cast<int>(ns.size()), std::max(top, 0));

    for (int i = 0; i < ns_len; i++) {
        const gtfs::stop& x = ns[i];

        cout << "\t\t{ \"stop_id\": \"" << jsonEscape(x.stop_id)
             << "\", \"stop_code\": \"" << jsonEscape(x.stop_code)
             << "\", \"stop_name\": \"" << jsonEscape(x.stop_name)
             << "\", \"stop_lat\": " << x.stop_lat << ", \"stop_lon\": " << x.stop_lon
             << ", \"distanceKM\": " << gtfs::getDistanceKM(lat, lon, x.stop_lat, x.stop_lon)
             << ((ns_len - 1) == i ? " }\n" : " },\n");
    }
    cout << "\t]\n}\n";
}