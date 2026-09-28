#include "../config/config.hpp"
#include <iostream>

using namespace gtfs;
using namespace std;

int main() {
    vector<stop> x = getNearestStops(config::loadOrExit(), 43, -79);

    cout << x[0].stop_id;

    return 0;
}