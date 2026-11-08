#include "studio/json.hpp"
#include "studio/dbc.hpp"

#include <iostream>
#include <string>

namespace {

constexpr std::size_t kMaxRequestBytes = 1024 * 1024;
constexpr const char* kProtocolVersion = "1.0";
constexpr const char* kEngineVersion = "0.1.0";

studio::Json make_error(const studio::Json& id, int code, const std::string& message) {
    return studio::Json::Object{
        {"error", studio::Json::Object{{"code", code}, {"message", message}}},
        {"id", id},
        {"jsonrpc", "2.0"},
    };
}

studio::Json make_result(const studio::Json& id, studio::Json result) {
    return studio::Json::Object{
        {"id", id},
        {"jsonrpc", "2.0"},
        {"result", std::move(result)},
    };
}

studio::Json handle_request(const studio::Json& request) {
    const auto* object = request.as_object();
    if (object == nullptr) {
        return make_error(nullptr, -32600, "Request must be a JSON object");
    }

    const auto version = object->find("jsonrpc");
    const auto id = object->find("id");
    const auto method = object->find("method");
    if (version == object->end() || version->second.as_string() == nullptr ||
        *version->second.as_string() != "2.0" ||
        id == object->end() || id->second.is_null() ||
        (!std::holds_alternative<std::string>(id->second.value) &&
         !std::holds_alternative<double>(id->second.value)) ||
        method == object->end() || method->second.as_string() == nullptr) {
        return make_error(nullptr, -32600, "Invalid JSON-RPC request");
    }

    const auto& request_id = id->second;
    const auto& method_name = *method->second.as_string();
    if (method_name == "initialize") {
        return make_result(request_id, studio::Json::Object{
            {"engineVersion", kEngineVersion},
            {"protocolVersion", kProtocolVersion},
            {"supportedMethods", studio::Json::Array{
                "initialize",
                "project.status",
                "dbc.parse",
            }},
        });
    }
    if (method_name == "project.status") {
        return make_result(request_id, studio::Json::Object{
            {"projectOpen", false},
            {"revision", 0},
            {"status", "No project is open"},
        });
    }
    if (method_name == "dbc.parse") {
        const auto params = object->find("params");
        const auto* params_object =
            params == object->end() ? nullptr : params->second.as_object();
        if (params_object == nullptr) {
            return make_error(request_id, -32602, "dbc.parse requires an object in params");
        }
        const auto text = params_object->find("text");
        const auto* dbc_text = text == params_object->end() ? nullptr : text->second.as_string();
        if (dbc_text == nullptr) {
            return make_error(request_id, -32602, "dbc.parse requires params.text as a string");
        }
        if (dbc_text->size() > kMaxRequestBytes) {
            return make_error(request_id, -32602, "DBC text exceeds the 1 MiB limit");
        }
        return make_result(request_id, studio::to_json(studio::parse_dbc(*dbc_text)));
    }
    return make_error(request_id, -32601, "Method not found: " + method_name);
}

} // namespace

int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.size() > kMaxRequestBytes) {
            std::cout << studio::serialize_json(make_error(
                nullptr, -32600, "Request exceeds the 1 MiB limit")) << '\n' << std::flush;
            continue;
        }

        studio::Json request;
        studio::JsonParseError parse_error;
        if (!studio::parse_json(line, request, parse_error)) {
            std::cout << studio::serialize_json(make_error(
                nullptr, -32700, "Parse error: " + parse_error.message)) << '\n' << std::flush;
            continue;
        }

        std::cout << studio::serialize_json(handle_request(request)) << '\n' << std::flush;
    }
    return 0;
}
