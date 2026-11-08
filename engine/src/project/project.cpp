#include "studio/project.hpp"

#include <cmath>
#include <unordered_set>

namespace studio {
namespace {

using Object = Json::Object;

const Json* find(const Object& object, const std::string& key) {
    const auto value = object.find(key);
    return value == object.end() ? nullptr : &value->second;
}

bool read_string(const Object& object, const std::string& key, const std::string& path,
                 std::string& output, std::vector<ProjectIssue>& issues) {
    const auto* value = find(object, key);
    if (value == nullptr) return true;
    const auto* text = value->as_string();
    if (text == nullptr) {
        issues.push_back({"SRS_INVALID_FIELD_TYPE", "error", path + "." + key,
                          "Expected a string"});
        return false;
    }
    output = *text;
    return true;
}

bool read_boolean(const Object& object, const std::string& key, const std::string& path,
                  bool& output, std::vector<ProjectIssue>& issues) {
    const auto* value = find(object, key);
    if (value == nullptr) return true;
    const auto* boolean = std::get_if<bool>(&value->value);
    if (boolean == nullptr) {
        issues.push_back({"SRS_INVALID_FIELD_TYPE", "error", path + "." + key,
                          "Expected a boolean"});
        return false;
    }
    output = *boolean;
    return true;
}

bool read_unsigned(const Object& object, const std::string& key, const std::string& path,
                   std::uint32_t maximum, std::uint32_t& output,
                   std::vector<ProjectIssue>& issues) {
    const auto* value = find(object, key);
    if (value == nullptr) return true;
    const auto* number = std::get_if<double>(&value->value);
    if (number == nullptr || !std::isfinite(*number) || *number < 0 ||
        std::floor(*number) != *number || *number > maximum) {
        issues.push_back({"SRS_INVALID_FIELD_TYPE", "error", path + "." + key,
                          "Expected an unsigned integer in the supported range"});
        return false;
    }
    output = static_cast<std::uint32_t>(*number);
    return true;
}

bool read_array(const Object& object, const std::string& key, const std::string& path,
                const Json::Array*& output, std::vector<ProjectIssue>& issues) {
    const auto* value = find(object, key);
    if (value == nullptr) return true;
    output = value->as_array();
    if (output == nullptr) {
        issues.push_back({"SRS_INVALID_FIELD_TYPE", "error", path + "." + key,
                          "Expected an array"});
        return false;
    }
    return true;
}

bool has_errors(const std::vector<ProjectIssue>& issues) {
    for (const auto& issue : issues) {
        if (issue.severity == "error") return true;
    }
    return false;
}

} // namespace

const char* to_string(TaskTrigger trigger) {
    switch (trigger) {
    case TaskTrigger::periodic: return "periodic";
    case TaskTrigger::event: return "event";
    case TaskTrigger::isr: return "isr";
    }
    return "periodic";
}

bool project_definition_from_json(const Json& input, ProjectDefinition& output,
                                  std::vector<ProjectIssue>& issues) {
    issues.clear();
    const auto* object = input.as_object();
    if (object == nullptr) {
        issues.push_back({"SRS_INVALID_PROJECT", "error", "$",
                          "Project definition must be an object"});
        return false;
    }

    ProjectDefinition project;
    bool valid = read_string(*object, "name", "$", project.name, issues);

    if (const auto* mcu_value = find(*object, "mcu")) {
        const auto* mcu = mcu_value->as_object();
        if (mcu == nullptr) {
            issues.push_back({"SRS_INVALID_FIELD_TYPE", "error", "$.mcu",
                              "Expected an object"});
            valid = false;
        } else {
            valid = read_string(*mcu, "vendor", "$.mcu", project.mcu.vendor, issues) && valid;
            valid = read_string(*mcu, "family", "$.mcu", project.mcu.family, issues) && valid;
            valid = read_string(*mcu, "partNumber", "$.mcu", project.mcu.part_number, issues) && valid;
        }
    }

    const Json::Array* dbc_sources = nullptr;
    valid = read_array(*object, "dbcSources", "$", dbc_sources, issues) && valid;
    if (dbc_sources != nullptr) {
        for (std::size_t index = 0; index < dbc_sources->size(); ++index) {
            const auto* name = (*dbc_sources)[index].as_string();
            if (name == nullptr) {
                issues.push_back({"SRS_INVALID_FIELD_TYPE", "error",
                                  "$.dbcSources[" + std::to_string(index) + "]",
                                  "Expected a string"});
                valid = false;
            } else {
                project.dbc_sources.push_back(*name);
            }
        }
    }

    const Json::Array* tasks = nullptr;
    valid = read_array(*object, "tasks", "$", tasks, issues) && valid;
    if (tasks != nullptr) {
        for (std::size_t index = 0; index < tasks->size(); ++index) {
            const auto path = "$.tasks[" + std::to_string(index) + "]";
            const auto* task_object = (*tasks)[index].as_object();
            if (task_object == nullptr) {
                issues.push_back({"SRS_INVALID_FIELD_TYPE", "error", path, "Expected an object"});
                valid = false;
                continue;
            }
            OsTaskRequirement task;
            valid = read_string(*task_object, "name", path, task.name, issues) && valid;
            std::string trigger;
            valid = read_string(*task_object, "trigger", path, trigger, issues) && valid;
            if (trigger == "periodic") task.trigger = TaskTrigger::periodic;
            else if (trigger == "event") task.trigger = TaskTrigger::event;
            else if (trigger == "isr") task.trigger = TaskTrigger::isr;
            else {
                issues.push_back({"SRS_INVALID_TASK_TRIGGER", "error", path + ".trigger",
                                  "Trigger must be periodic, event, or isr"});
                valid = false;
            }
            std::uint32_t period = 0;
            const auto* period_value = find(*task_object, "periodMs");
            if (period_value != nullptr && !period_value->is_null()) {
                if (read_unsigned(*task_object, "periodMs", path, 60000, period, issues)) {
                    task.period_ms = period;
                } else {
                    valid = false;
                }
            }
            valid = read_string(*task_object, "event", path, task.event_name, issues) && valid;
            valid = read_string(*task_object, "isrVector", path, task.isr_vector, issues) && valid;
            project.tasks.push_back(std::move(task));
        }
    }

    const Json::Array* services = nullptr;
    valid = read_array(*object, "dcmServices", "$", services, issues) && valid;
    if (services != nullptr) {
        for (std::size_t index = 0; index < services->size(); ++index) {
            const auto path = "$.dcmServices[" + std::to_string(index) + "]";
            const auto* service_object = (*services)[index].as_object();
            if (service_object == nullptr) {
                issues.push_back({"SRS_INVALID_FIELD_TYPE", "error", path, "Expected an object"});
                valid = false;
                continue;
            }
            DcmServiceRequirement service;
            std::uint32_t service_id = 0;
            if (find(*service_object, "serviceId") == nullptr) {
                issues.push_back({"SRS_DCM_SERVICE_ID_REQUIRED", "error", path + ".serviceId",
                                  "DCM service ID is required"});
                valid = false;
            }
            if (!read_unsigned(*service_object, "serviceId", path, 255, service_id, issues)) {
                valid = false;
            }
            service.service_id = static_cast<std::uint8_t>(service_id);
            valid = read_string(*service_object, "name", path, service.name, issues) && valid;
            valid = read_boolean(*service_object, "enabled", path, service.enabled, issues) && valid;
            project.dcm_services.push_back(std::move(service));
        }
    }

    const Json::Array* requirements = nullptr;
    valid = read_array(*object, "requirements", "$", requirements, issues) && valid;
    if (requirements != nullptr) {
        for (std::size_t index = 0; index < requirements->size(); ++index) {
            const auto path = "$.requirements[" + std::to_string(index) + "]";
            const auto* requirement_object = (*requirements)[index].as_object();
            if (requirement_object == nullptr) {
                issues.push_back({"SRS_INVALID_FIELD_TYPE", "error", path, "Expected an object"});
                valid = false;
                continue;
            }
            SoftwareRequirement requirement;
            valid = read_string(*requirement_object, "id", path, requirement.id, issues) && valid;
            valid = read_string(*requirement_object, "text", path, requirement.text, issues) && valid;
            project.requirements.push_back(std::move(requirement));
        }
    }

    if (!valid || has_errors(issues)) return false;
    output = std::move(project);
    return true;
}

std::vector<ProjectIssue> validate_project_definition(const ProjectDefinition& project) {
    std::vector<ProjectIssue> issues;
    if (project.name.empty()) {
        issues.push_back({"SRS_PROJECT_NAME_REQUIRED", "error", "$.name",
                          "Project name is required"});
    }
    if (project.mcu.part_number.empty()) {
        issues.push_back({"SRS_MCU_NOT_SELECTED", "warning", "$.mcu.partNumber",
                          "Select or identify the target MCU before generation"});
    }
    std::unordered_set<std::string> task_names;
    for (std::size_t index = 0; index < project.tasks.size(); ++index) {
        const auto& task = project.tasks[index];
        const auto path = "$.tasks[" + std::to_string(index) + "]";
        if (task.name.empty()) {
            issues.push_back({"SRS_TASK_NAME_REQUIRED", "error", path + ".name",
                              "OS task name is required"});
        } else if (!task_names.insert(task.name).second) {
            issues.push_back({"SRS_DUPLICATE_TASK_NAME", "error", path + ".name",
                              "OS task names must be unique"});
        }
        switch (task.trigger) {
        case TaskTrigger::periodic:
            if (!task.period_ms.has_value()) {
                issues.push_back({"SRS_TASK_PERIOD_REQUIRED", "error", path + ".periodMs",
                                  "Periodic tasks require a period in milliseconds"});
            } else if (*task.period_ms == 0 || *task.period_ms > 60000) {
                issues.push_back({"SRS_TASK_PERIOD_OUT_OF_RANGE", "error", path + ".periodMs",
                                  "Task period must be between 1 and 60000 ms"});
            }
            if (!task.event_name.empty() || !task.isr_vector.empty()) {
                issues.push_back({"SRS_TASK_TRIGGER_CONFLICT", "error", path,
                                  "Periodic tasks cannot declare event or ISR data"});
            }
            break;
        case TaskTrigger::event:
            if (task.event_name.empty()) {
                issues.push_back({"SRS_TASK_EVENT_REQUIRED", "error", path + ".event",
                                  "Event-driven tasks require an event name"});
            }
            if (task.period_ms.has_value() || !task.isr_vector.empty()) {
                issues.push_back({"SRS_TASK_TRIGGER_CONFLICT", "error", path,
                                  "Event tasks cannot declare period or ISR data"});
            }
            break;
        case TaskTrigger::isr:
            if (task.isr_vector.empty()) {
                issues.push_back({"SRS_TASK_ISR_REQUIRED", "error", path + ".isrVector",
                                  "ISR tasks require an interrupt vector"});
            }
            if (task.period_ms.has_value() || !task.event_name.empty()) {
                issues.push_back({"SRS_TASK_TRIGGER_CONFLICT", "error", path,
                                  "ISR tasks cannot declare period or event data"});
            }
            break;
        }
    }

    std::unordered_set<std::uint8_t> service_ids;
    for (std::size_t index = 0; index < project.dcm_services.size(); ++index) {
        const auto& service = project.dcm_services[index];
        const auto path = "$.dcmServices[" + std::to_string(index) + "]";
        if (!service_ids.insert(service.service_id).second) {
            issues.push_back({"SRS_DUPLICATE_DCM_SERVICE_ID", "error", path + ".serviceId",
                              "DCM service IDs must be unique"});
        }
        if (service.name.empty()) {
            issues.push_back({"SRS_DCM_SERVICE_NAME_REQUIRED", "error", path + ".name",
                              "DCM service name is required"});
        }
    }

    std::unordered_set<std::string> requirement_ids;
    for (std::size_t index = 0; index < project.requirements.size(); ++index) {
        const auto& requirement = project.requirements[index];
        const auto path = "$.requirements[" + std::to_string(index) + "]";
        if (requirement.id.empty()) {
            issues.push_back({"SRS_REQUIREMENT_ID_REQUIRED", "error", path + ".id",
                              "Requirement ID is required"});
        } else if (!requirement_ids.insert(requirement.id).second) {
            issues.push_back({"SRS_DUPLICATE_REQUIREMENT_ID", "error", path + ".id",
                              "Requirement IDs must be unique"});
        }
        if (requirement.text.empty()) {
            issues.push_back({"SRS_REQUIREMENT_TEXT_REQUIRED", "error", path + ".text",
                              "Requirement text is required"});
        }
    }
    return issues;
}

Json to_json(const ProjectDefinition& project) {
    Json::Array dbc_sources;
    for (const auto& source : project.dbc_sources) dbc_sources.emplace_back(source);

    Json::Array tasks;
    for (const auto& task : project.tasks) {
        tasks.emplace_back(Json::Object{
            {"event", task.event_name},
            {"isrVector", task.isr_vector},
            {"name", task.name},
            {"periodMs", task.period_ms ? Json(static_cast<int>(*task.period_ms)) : Json(nullptr)},
            {"trigger", to_string(task.trigger)},
        });
    }

    Json::Array services;
    for (const auto& service : project.dcm_services) {
        services.emplace_back(Json::Object{
            {"enabled", service.enabled},
            {"name", service.name},
            {"serviceId", static_cast<int>(service.service_id)},
        });
    }

    Json::Array requirements;
    for (const auto& requirement : project.requirements) {
        requirements.emplace_back(Json::Object{
            {"id", requirement.id},
            {"text", requirement.text},
        });
    }

    return Json::Object{
        {"dbcSources", Json(std::move(dbc_sources))},
        {"dcmServices", Json(std::move(services))},
        {"mcu", Json::Object{
            {"family", project.mcu.family},
            {"partNumber", project.mcu.part_number},
            {"vendor", project.mcu.vendor},
        }},
        {"name", project.name},
        {"requirements", Json(std::move(requirements))},
        {"tasks", Json(std::move(tasks))},
    };
}

Json to_json(const ProjectIssue& issue) {
    return Json::Object{
        {"code", issue.code},
        {"message", issue.message},
        {"path", issue.path},
        {"severity", issue.severity},
    };
}

} // namespace studio
