#pragma once

#include "studio/json.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace studio {

enum class TaskTrigger {
    periodic,
    event,
    isr,
};

struct McuTarget {
    std::string vendor;
    std::string family;
    std::string part_number;
};

struct OsTaskRequirement {
    std::string name;
    TaskTrigger trigger = TaskTrigger::periodic;
    std::optional<std::uint32_t> period_ms;
    std::string event_name;
    std::string isr_vector;
};

struct DcmServiceRequirement {
    std::uint8_t service_id = 0;
    std::string name;
    bool enabled = true;
};

struct SoftwareRequirement {
    std::string id;
    std::string text;
};

struct ProjectDefinition {
    std::string name;
    McuTarget mcu;
    std::vector<std::string> dbc_sources;
    std::vector<OsTaskRequirement> tasks;
    std::vector<DcmServiceRequirement> dcm_services;
    std::vector<SoftwareRequirement> requirements;
};

struct ProjectIssue {
    std::string code;
    std::string severity;
    std::string path;
    std::string message;
};

[[nodiscard]] bool project_definition_from_json(
    const Json& input, ProjectDefinition& output, std::vector<ProjectIssue>& issues);
[[nodiscard]] std::vector<ProjectIssue> validate_project_definition(
    const ProjectDefinition& project);
[[nodiscard]] Json to_json(const ProjectDefinition& project);
[[nodiscard]] Json to_json(const ProjectIssue& issue);
[[nodiscard]] const char* to_string(TaskTrigger trigger);

} // namespace studio
