#pragma once
#include <string>

namespace routing {

struct Location {
    int id;
    double lat;
    double lon;
    std::string zone_id;
    std::string name;
};

}
