#pragma once
#include "studio/json.hpp"
#include <string>
namespace studio {
// Read-only: never executes build wrappers or creates files in the inspected project.
[[nodiscard]] Json inspect_eclipse_project(const std::string& root);
}
