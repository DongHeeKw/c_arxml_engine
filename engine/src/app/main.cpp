#include "studio/json.hpp"
#include "studio/dbc.hpp"
#include "studio/project.hpp"
#include "studio/thread_pool.hpp"

#include <algorithm>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr std::size_t kMaxRequestBytes = 1024 * 1024;
constexpr std::size_t kMaxBatchFiles = 32;
constexpr const char* kProtocolVersion = "1.0";
constexpr const char* kEngineVersion = "0.1.0";

struct EngineState {
    std::optional<studio::ProjectDefinition> project;
    std::uint64_t project_revision = 0;
};

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

studio::Json issues_to_json(const std::vector<studio::ProjectIssue>& issues) {
    studio::Json::Array result;
    result.reserve(issues.size());
    for (const auto& issue : issues) result.push_back(studio::to_json(issue));
    return studio::Json(std::move(result));
}

std::string issues_to_message(const std::vector<studio::ProjectIssue>& issues) {
    if (issues.empty()) return "Invalid project definition";
    return issues.front().path + ": " + issues.front().message;
}

studio::Json handle_request(const studio::Json& request, EngineState& state,
                            studio::ThreadPool& workers) {
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
                "project.configure",
                "dbc.parse",
                "dbc.parseBatch",
            }},
        });
    }
    if (method_name == "project.status") {
        studio::Json::Object status{
            {"persistence", "in-memory-draft"},
            {"projectOpen", state.project.has_value()},
            {"revision", static_cast<double>(state.project_revision)},
            {"status", state.project ? "Project draft configured" : "No project is configured"},
        };
        if (state.project) status.emplace("definition", studio::to_json(*state.project));
        return make_result(request_id, studio::Json(std::move(status)));
    }
    if (method_name == "project.configure") {
        const auto params = object->find("params");
        const auto* params_object =
            params == object->end() ? nullptr : params->second.as_object();
        if (params_object == nullptr) {
            return make_error(request_id, -32602, "project.configure requires an object in params");
        }

        studio::ProjectDefinition project;
        std::vector<studio::ProjectIssue> issues;
        if (!studio::project_definition_from_json(studio::Json(*params_object), project, issues)) {
            return make_error(request_id, -32602, issues_to_message(issues));
        }
        issues = studio::validate_project_definition(project);
        for (const auto& issue : issues) {
            if (issue.severity == "error") {
                return make_result(request_id, studio::Json::Object{
                    {"accepted", false},
                    {"issues", issues_to_json(issues)},
                });
            }
        }
        state.project = std::move(project);
        ++state.project_revision;
        return make_result(request_id, studio::Json::Object{
            {"accepted", true},
            {"issues", issues_to_json(issues)},
            {"project", studio::to_json(*state.project)},
            {"revision", static_cast<double>(state.project_revision)},
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
    if (method_name == "dbc.parseBatch") {
        const auto params = object->find("params");
        const auto* params_object =
            params == object->end() ? nullptr : params->second.as_object();
        const auto files_iterator = params_object == nullptr
            ? studio::Json::Object::const_iterator{}
            : params_object->find("files");
        const auto* files = params_object == nullptr || files_iterator == params_object->end()
            ? nullptr
            : files_iterator->second.as_array();
        if (files == nullptr) {
            return make_error(request_id, -32602,
                              "dbc.parseBatch requires params.files as an array");
        }
        if (files->size() > kMaxBatchFiles) {
            return make_error(request_id, -32602,
                              "dbc.parseBatch supports at most 32 files per request");
        }

        std::vector<std::pair<std::string, std::string>> source_files;
        source_files.reserve(files->size());
        for (std::size_t index = 0; index < files->size(); ++index) {
            const auto* file = (*files)[index].as_object();
            if (file == nullptr) {
                return make_error(request_id, -32602,
                                  "dbc.parseBatch file entries must be objects");
            }
            const auto name = file->find("name");
            const auto text = file->find("text");
            const auto* file_name = name == file->end() ? nullptr : name->second.as_string();
            const auto* dbc_text = text == file->end() ? nullptr : text->second.as_string();
            if (file_name == nullptr || dbc_text == nullptr) {
                return make_error(request_id, -32602,
                                  "dbc.parseBatch file entries require string name and text fields");
            }
            if (dbc_text->size() > 900 * 1024) {
                return make_error(request_id, -32602,
                                  "DBC text exceeds the 900 KiB per-file limit");
            }
            source_files.emplace_back(*file_name, *dbc_text);
        }

        std::vector<std::future<studio::Json>> results;
        results.reserve(source_files.size());
        for (const auto& [name, text] : source_files) {
            results.push_back(workers.submit([name, text] {
                return studio::Json(studio::Json::Object{
                    {"name", name},
                    {"preview", studio::to_json(studio::parse_dbc(text))},
                });
            }));
        }

        studio::Json::Array batch;
        batch.reserve(results.size());
        for (auto& result : results) batch.push_back(result.get());
        return make_result(request_id, studio::Json(std::move(batch)));
    }
    return make_error(request_id, -32601, "Method not found: " + method_name);
}

} // namespace

int main() {
    const auto hardware_threads = std::thread::hardware_concurrency();
    const auto worker_count = std::clamp(
        hardware_threads == 0 ? 2u : hardware_threads, 2u, 8u);
    studio::ThreadPool workers(worker_count);
    EngineState state;

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

        std::cout << studio::serialize_json(handle_request(request, state, workers))
                  << '\n' << std::flush;
    }
    return 0;
}
