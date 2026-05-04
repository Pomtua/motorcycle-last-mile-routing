#pragma once
#include <string>

namespace routing {

struct TimeWindow {
    double start; 
    double end;
};

struct Parcel {
    int id;
    int location_id;
    double weight;
    double volume;
    TimeWindow time_window;
    bool is_split; 
};

}
