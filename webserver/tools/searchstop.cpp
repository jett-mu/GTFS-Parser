#include <iostream>
#include <vector>
#include <string>
#include <iomanip>
#include <cstdlib>
#include <cstdio>
#include "../../static-gtfs/gtfs.hpp"

// Escape a string for use inside a JSON string literal. Stop names can
// contain quotes ("KING'S COLLEGE" is fine, but some feeds use " for inches
// or feet), and the query is user input.
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

    std::cout << "{\n\t\"query\": \"" << jsonEscape(argv[1]) << "\",\n\t\"matches\": [\n";

    const int matchesLen = std::min((int)matches.size(), top);

    for (int i = 0; i < matchesLen; i++) {
        const gtfs::matchsearch& x = matches[i];

        std::cout << "\t\t{ \"stop_id\": \"" << jsonEscape(x.stop_id) <<
                "\", \"stop_code\": \"" << jsonEscape(x.stop_code) <<
                "\", \"stop_name\": \"" << jsonEscape(x.text.str) <<
                "\", \"score\": " << x.score <<
                ", \"lat\": " << std::setprecision(7) << x.lat <<
                ", \"lon\": " << std::setprecision(7) << x.lon <<
                ((matchesLen - 1) == i ? " }\n" : " },\n");
    }
    std::cout << "\t]\n}\n";
}
