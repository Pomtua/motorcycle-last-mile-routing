#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "router/instance.hpp"

namespace router
{
    Instance loadInstance(const std::string &path);
    Instance instanceFromJson(const nlohmann::json &j, const std::string &source = "instance JSON");
}
