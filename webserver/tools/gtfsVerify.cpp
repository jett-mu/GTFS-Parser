#include <iostream>
#include <string>
#include "../../config/config.hpp"

static const char* statusName(gtfs::feedStatus s) {
    switch (s) {
        case gtfs::in_use:      return "in_use";
        case gtfs::expired:     return "expired";
        case gtfs::upcoming:    return "upcoming";
        case gtfs::no_result_a: return "no_feed_end_date";
        case gtfs::no_result_b: return "no_feed_start_date";
        default:                return "no_result";
    }
}

static void printDate(const gtfs::calendar_day& d) {
    if (d.year < 0) { std::cout << "null"; return; }
    std::cout << "{\"year\": " << d.year << ", \"month\": " << d.month << ", \"day\": " << d.day << "}";
}

int main(int argc, char* argv[]) {
    if (argc != 1 && argc != 4) {
        std::cerr << "Usage: " << argv[0] << " [<year> <month> <day>]  (defaults to today)\n";
        return -1;
    }

    const gtfs::data_feed feed = config::loadOrExit();

    gtfs::calendar_day date = gtfs::getToday();
    if (argc == 4) date = gtfs::calendar_day(std::stoi(argv[1]), std::stoi(argv[2]), std::stoi(argv[3]));

    gtfs::feedStatus status = gtfs::verifyGTFS(feed, date);

    std::cout << "{\n\t\"status\": \"" << statusName(status) << "\",\n"
              << "\t\"status_code\": " << static_cast<int>(status) << ",\n"
              << "\t\"checked_date\": ";
    printDate(date);
    std::cout << ",\n\t\"start_date\": ";
    printDate(gtfs::getStartDate(feed));
    std::cout << ",\n\t\"end_date\": ";
    printDate(gtfs::getEndDate(feed));
    std::cout << "\n}\n";
}
