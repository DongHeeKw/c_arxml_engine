#pragma once
#include <istream>
#include <string>
#include <string_view>

namespace studio {
[[nodiscard]] std::string sha256(std::string_view bytes);
[[nodiscard]] std::string sha256(std::istream& input);
}
