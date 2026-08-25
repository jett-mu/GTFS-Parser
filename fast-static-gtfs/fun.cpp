#include <iostream>
#include "fast-config.hpp"
#include "fast-gtfs.hpp"

int main() {
    fast_gtfs::bin_search::sortFile(fast_config::fast_stop_times_path, "stop_id",fast_config::fast_stop_times_stop_id);



}