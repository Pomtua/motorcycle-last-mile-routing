#pragma once

#include <string>

#include "router/instance.hpp"

namespace router
{
    Instance loadInstance(const std::string &path);
}