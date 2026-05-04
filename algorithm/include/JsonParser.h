#pragma once
#include <string>
#include <nlohmann/json.hpp>
#include "RoutingInstance.h"

namespace routing {

class JsonParser {
public:
    static RoutingInstance parse(const std::string& file_path);
};

}
