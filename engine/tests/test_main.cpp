#include "studio/dbc.hpp"
#include "studio/project.hpp"
#include "studio/thread_pool.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

namespace {

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

studio::ProjectDefinition valid_project() {
    studio::ProjectDefinition project;
    project.name = "BodyController";
    project.mcu = {"Infineon", "AURIX", "TC397"};
    project.tasks = {
        {"Com_MainFunction", studio::TaskTrigger::periodic, 5, {}, {}},
        {"CanRx", studio::TaskTrigger::event, {}, "CanIfRxIndication", {}},
        {"Can0_Isr", studio::TaskTrigger::isr, {}, {}, "CAN0_IRQHandler"},
    };
    project.dcm_services = {
        {0x10, "DiagnosticSessionControl", true},
        {0x22, "ReadDataByIdentifier", true},
    };
    project.requirements = {
        {"SRS-COM-001", "Transmit configured CAN signals every 5 ms."},
    };
    return project;
}

void test_dbc_parser() {
    const auto parsed = studio::parse_dbc(
        "BU_: ECU_A ECU_B\n"
        "BO_ 291 Status: 8 ECU_A\n"
        " SG_ Speed : 0|16@1+ (0.1,0) [0|250] \"km/h\" ECU_B\n");
    check(parsed.nodes.size() == 2, "DBC parser should collect ECU nodes");
    check(parsed.messages.size() == 1, "DBC parser should collect CAN messages");
    check(parsed.messages[0].signals.size() == 1, "DBC parser should collect CAN signals");
    check(parsed.issues.empty(), "valid DBC declarations should not produce issues");
}

void test_srs_validation_accepts_valid_configuration() {
    const auto issues = studio::validate_project_definition(valid_project());
    check(issues.empty(), "valid project SRS should pass validation");
}

void test_srs_validation_reports_missing_trigger_data_and_duplicate_service() {
    auto project = valid_project();
    project.tasks[0].period_ms.reset();
    project.dcm_services.push_back({0x22, "DuplicateService", true});
    project.requirements.push_back({"SRS-COM-002", ""});

    const auto issues = studio::validate_project_definition(project);
    const auto has_issue = [&issues](const std::string& code) {
        for (const auto& issue : issues) {
            if (issue.code == code) return true;
        }
        return false;
    };
    check(has_issue("SRS_TASK_PERIOD_REQUIRED"), "periodic tasks should require a period");
    check(has_issue("SRS_DUPLICATE_DCM_SERVICE_ID"), "DCM service IDs should be unique");
    check(has_issue("SRS_REQUIREMENT_TEXT_REQUIRED"), "SRS requirements should have text");
}

void test_thread_pool_runs_independent_tasks_concurrently() {
    studio::ThreadPool pool(2);
    std::mutex mutex;
    std::condition_variable changed;
    int started = 0;
    bool release = false;
    const auto wait_for_release = [&] {
        std::unique_lock lock(mutex);
        ++started;
        changed.notify_all();
        changed.wait(lock, [&release] { return release; });
        return started;
    };

    auto first = pool.submit(wait_for_release);
    auto second = pool.submit(wait_for_release);
    {
        std::unique_lock lock(mutex);
        changed.wait_for(lock, std::chrono::seconds(2), [&started] { return started == 2; });
        release = true;
    }
    changed.notify_all();
    check(first.get() == 2 && second.get() == 2,
          "two queued generation jobs should execute on separate workers");
}

void test_thread_pool_rejects_zero_workers() {
    bool rejected = false;
    try {
        studio::ThreadPool pool(0);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    check(rejected, "thread pool should reject a zero-worker configuration");
}

} // namespace

int main() {
    test_dbc_parser();
    test_srs_validation_accepts_valid_configuration();
    test_srs_validation_reports_missing_trigger_data_and_duplicate_service();
    test_thread_pool_runs_independent_tasks_concurrently();
    test_thread_pool_rejects_zero_workers();
    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All C++ engine unit tests passed.\n";
    return EXIT_SUCCESS;
}
