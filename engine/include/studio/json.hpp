#pragma once

#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace studio {

struct Json {
    using Array = std::vector<Json>;
    using Object = std::map<std::string, Json, std::less<>>;
    using Value = std::variant<std::nullptr_t, bool, double, std::string, Array, Object>;

    Value value;

    Json() : value(nullptr) {}
    Json(std::nullptr_t) : value(nullptr) {}
    Json(bool input) : value(input) {}
    Json(double input) : value(input) {}
    Json(int input) : value(static_cast<double>(input)) {}
    Json(std::string input) : value(std::move(input)) {}
    Json(const char* input) : value(std::string(input)) {}
    Json(Array input) : value(std::move(input)) {}
    Json(Object input) : value(std::move(input)) {}

    [[nodiscard]] const Object* as_object() const;
    [[nodiscard]] const Array* as_array() const;
    [[nodiscard]] const std::string* as_string() const;
    [[nodiscard]] bool is_null() const;
};

struct JsonParseError {
    std::string message;
};

[[nodiscard]] bool parse_json(std::string_view input, Json& output, JsonParseError& error);
[[nodiscard]] std::string serialize_json(const Json& value);

} // namespace studio
