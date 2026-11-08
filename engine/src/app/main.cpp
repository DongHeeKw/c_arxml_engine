#include "studio/json.hpp"
#include "studio/dbc.hpp"
#include "studio/project.hpp"
#include "studio/project_store.hpp"
#include "studio/project_inventory.hpp"
#include "studio/thread_pool.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

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

studio::Json dbc_issues_to_json(const studio::DbcParseResult& parsed) {
    studio::Json::Array result;
    result.reserve(parsed.issues.size());
    for (const auto& issue : parsed.issues) {
        result.emplace_back(studio::Json::Object{
            {"code", issue.code},
            {"line", static_cast<double>(issue.line)},
            {"message", issue.message},
            {"severity", issue.severity},
        });
    }
    return studio::Json(std::move(result));
}

std::string issues_to_message(const std::vector<studio::ProjectIssue>& issues) {
    if (issues.empty()) return "Invalid project definition";
    return issues.front().path + ": " + issues.front().message;
}

std::string database_path_from_environment() {
#ifdef _WIN32
    const wchar_t* configured = _wgetenv(L"STUDIO_DATABASE_PATH");
    if (configured == nullptr || configured[0] == L'\0') return "studio.sqlite3";
    const int required = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, configured, -1, nullptr, 0, nullptr, nullptr);
    if (required == 0) {
        throw std::runtime_error("STUDIO_DATABASE_PATH is not valid Unicode");
    }
    std::string path(static_cast<std::size_t>(required), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, configured, -1,
                            path.data(), required, nullptr, nullptr) == 0) {
        throw std::runtime_error("Could not encode STUDIO_DATABASE_PATH as UTF-8");
    }
    path.pop_back();
    return path;
#else
    const char* configured = std::getenv("STUDIO_DATABASE_PATH");
    return configured == nullptr || configured[0] == '\0'
        ? "studio.sqlite3"
        : configured;
#endif
}

studio::Json handle_request(const studio::Json& request, EngineState& state,
                            studio::ProjectStore& project_store,
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
                "dbc.diff",
                "dbc.import",
                "dbc.sources",
                "network.ecu",
                "project.inspect",
            }},
        });
    }
    if (method_name == "project.status") {
        studio::Json::Object status{
            {"persistence", "sqlite"},
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
        std::uint64_t revision = 0;
        try {
            revision = project_store.save(project);
        } catch (const std::exception& error) {
            return make_error(request_id, -32603,
                              "Could not persist project definition: " +
                                  std::string(error.what()));
        }
        state.project = std::move(project);
        state.project_revision = revision;
        return make_result(request_id, studio::Json::Object{
            {"accepted", true},
            {"issues", issues_to_json(issues)},
            {"project", studio::to_json(*state.project)},
            {"revision", static_cast<double>(state.project_revision)},
        });
    }
    if (method_name == "dbc.sources") return make_result(request_id,project_store.dbc_sources());
    if (method_name == "dbc.import" || method_name == "network.ecu" || method_name == "project.inspect") {
        try {
            const auto params=object->find("params");
            const auto* values=params==object->end() ? nullptr : params->second.as_object();
            if (!values) throw std::invalid_argument("Method requires an object in params");
            const auto string_field=[](const studio::Json::Object& item, const char* key) {
                const auto found=item.find(key);
                if (found==item.end() || !found->second.as_string() || found->second.as_string()->empty())
                    throw std::invalid_argument(std::string("Expected nonempty string: ")+key);
                return *found->second.as_string();
            };
            if (method_name=="project.inspect")
                return make_result(request_id,studio::inspect_eclipse_project(string_field(*values,"root")));
            if (method_name=="network.ecu")
                return make_result(request_id,project_store.ecu_view(string_field(*values,"channel"),string_field(*values,"ecu")));
            const auto files=values->find("files");
            if (files==values->end() || !files->second.as_array())
                throw std::invalid_argument("dbc.import requires a files array");
            std::vector<studio::DbcImport> sources;
            for (const auto& file : *files->second.as_array()) {
                const auto* item=file.as_object();
                if (!item) throw std::invalid_argument("Source entries must be objects");
                const auto format=string_field(*item,"frameFormat");
                if (format!="classic" && format!="fd")
                    throw std::invalid_argument("frameFormat must explicitly be classic or fd");
                // An empty source is a valid empty DBC, so text differs from identity fields.
                const auto text=item->find("text");
                if (text==item->end() || !text->second.as_string())
                    throw std::invalid_argument("Expected source text string");
                sources.push_back({string_field(*item,"sourceUri"),string_field(*item,"name"),
                    string_field(*item,"channel"),*text->second.as_string(),format=="fd"});
            }
            return make_result(request_id,project_store.import_dbcs(sources));
        } catch (const std::invalid_argument& error) {
            return make_error(request_id,-32602,error.what());
        } catch (const std::exception& error) {
            return make_error(request_id,-32603,error.what());
        }
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
    if (method_name == "dbc.diff") {
        const auto params = object->find("params");
        const auto* params_object =
            params == object->end() ? nullptr : params->second.as_object();
        if (params_object == nullptr) {
            return make_error(request_id, -32602, "dbc.diff requires an object in params");
        }
        const auto before = params_object->find("beforeText");
        const auto after = params_object->find("afterText");
        const auto* before_text = before == params_object->end() ? nullptr : before->second.as_string();
        const auto* after_text = after == params_object->end() ? nullptr : after->second.as_string();
        if (before_text == nullptr || after_text == nullptr) {
            return make_error(request_id, -32602,
                              "dbc.diff requires beforeText and afterText strings");
        }
        if (before_text->size() > 900 * 1024 || after_text->size() > 900 * 1024) {
            return make_error(request_id, -32602, "DBC text exceeds the 900 KiB per-file limit");
        }

        const auto previous = studio::parse_dbc(*before_text);
        const auto current = studio::parse_dbc(*after_text);
        return make_result(request_id, studio::Json::Object{
            {"afterIssues", dbc_issues_to_json(current)},
            {"afterUnsupportedLines", static_cast<double>(current.unsupported_lines)},
            {"beforeIssues", dbc_issues_to_json(previous)},
            {"beforeUnsupportedLines", static_cast<double>(previous.unsupported_lines)},
            {"changes", studio::to_json(studio::diff_dbc(previous, current))},
        });
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

        bool allow_can_fd=false;
        const auto format=params_object->find("frameFormat");
        if (format!=params_object->end()) {
            const auto* value=format->second.as_string();
            if (!value || (*value!="classic" && *value!="fd"))
                return make_error(request_id,-32602,"frameFormat must be classic or fd");
            allow_can_fd=*value=="fd";
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

        std::vector<std::future<std::pair<std::string, studio::DbcParseResult>>> results;
        results.reserve(source_files.size());
        for (const auto& [name, text] : source_files) {
            results.push_back(workers.submit([name, text, allow_can_fd] {
                return std::pair{name, studio::parse_dbc(text,allow_can_fd)};
            }));
        }

        std::vector<studio::DbcBatchSource> parsed_files;
        parsed_files.reserve(results.size());
        for (auto& result : results) {
            auto [name, parsed] = result.get();
            parsed_files.push_back({std::move(name), std::move(parsed)});
        }

        studio::Json::Array files_json;
        files_json.reserve(parsed_files.size());
        for (const auto& source : parsed_files) {
            files_json.emplace_back(studio::Json::Object{
                {"name", source.name},
                {"preview", studio::to_json(source.parsed)},
            });
        }

        studio::Json::Array findings_json;
        const auto findings = studio::analyze_dbc_batch(parsed_files);
        findings_json.reserve(findings.size());
        for (const auto& finding : findings) {
            studio::Json::Array source_files_json;
            studio::Json::Array source_indices_json;
            source_files_json.reserve(finding.source_indices.size());
            source_indices_json.reserve(finding.source_indices.size());
            for (const auto source_index : finding.source_indices) {
                source_files_json.emplace_back(parsed_files[source_index].name);
                source_indices_json.emplace_back(static_cast<double>(source_index + 1));
            }
            const auto code = finding.identity == "frameId"
                ? "DBC_CROSS_FILE_FRAME_ID_SCOPE_UNRESOLVED"
                : "DBC_CROSS_FILE_MESSAGE_NAME_SCOPE_UNRESOLVED";
            findings_json.emplace_back(studio::Json::Object{
                {"code", code},
                {"identity", finding.identity},
                {"key", finding.key},
                {"message",
                 "This identifier appears in multiple DBC files. Without explicit network "
                 "assignments, it may be a duplicate definition or a valid identifier on "
                 "separate CAN networks; no conflict is inferred."},
                {"networkScope", "unresolved"},
                {"severity", "warning"},
                {"sourceFiles", studio::Json(std::move(source_files_json))},
                {"sourceIndices", studio::Json(std::move(source_indices_json))},
            });
        }
        return make_result(request_id, studio::Json::Object{
            {"files", studio::Json(std::move(files_json))},
            {"findings", studio::Json(std::move(findings_json))},
        });
    }
    return make_error(request_id, -32601, "Method not found: " + method_name);
}

} // namespace

int main() {
    try {
        studio::ProjectStore project_store(database_path_from_environment());
        const auto stored_project = project_store.load();
        EngineState state{stored_project.definition, stored_project.revision};

        const auto hardware_threads = std::thread::hardware_concurrency();
        const auto worker_count = std::clamp(
            hardware_threads == 0 ? 2u : hardware_threads, 2u, 8u);
        studio::ThreadPool workers(worker_count);

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
                    nullptr, -32700, "Parse error: " + parse_error.message))
                          << '\n' << std::flush;
                continue;
            }

            auto response = studio::serialize_json(handle_request(request, state, project_store, workers));
            if (response.size() > kMaxRequestBytes) {
                const auto* request_object=request.as_object();
                const auto request_id=request_object && request_object->contains("id")
                    ? request_object->at("id") : studio::Json(nullptr);
                response=studio::serialize_json(make_error(request_id,-32603,
                    "Response exceeds the 1 MiB protocol limit; narrow the query"));
            }
            std::cout << response << '\n' << std::flush;
        }
    } catch (const std::exception& error) {
        std::cerr << "Could not initialize project persistence: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return 0;
}
