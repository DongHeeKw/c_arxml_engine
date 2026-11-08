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

void test_dbc_parser_warns_for_unsupported_declarations() {
    const auto parsed = studio::parse_dbc(
        "VERSION \"1.0\"\n"
        "\n"
        "NS_ :\n"
        "    NS_DESC_\n"
        "    CM_\n"
        "    BA_DEF_\n"
        "BS_:\n"
        "BU_: ECU_A\n"
        "BO_ 291 Status: 8 ECU_A\n"
        " SG_ State : 0|1@1+ (1,0) [0|1] \"\" ECU_A\n"
        "CM_ BO_ 291 \"status comment\";\n"
        "BA_DEF_ BO_ \"GenMsgCycleTime\" INT 0 65535;\n"
        "BA_ \"GenMsgCycleTime\" BO_ 291 10;\n"
        "VAL_ 291 State 0 \"Off\";\n"
        "SG_MUL_VAL_ 291 State Mux 1-2;\n"
        "BO_TX_BU_ 291: ECU_A;\n"
        "BU_SG_REL_ ECU_A ECU_B;\n"
        "VENDOR_EXT_ 123;\n");

    constexpr std::size_t expected_unsupported_lines = 10;
    check(parsed.unsupported_lines == expected_unsupported_lines,
          "unsupported declaration count should exclude NS_ header keyword entries");
    check(parsed.issues.size() == expected_unsupported_lines,
          "each unsupported declaration should produce a structured issue");
    for (const auto& issue : parsed.issues) {
        check(issue.code == "DBC_UNSUPPORTED_DECLARATION",
              "unsupported declarations should use the stable unsupported issue code");
        check(issue.severity == "warning",
              "unsupported declarations should be warnings rather than parse errors");
    }
    if (parsed.issues.size() == expected_unsupported_lines) {
        const std::vector<std::size_t> expected_lines{1, 7, 11, 12, 13, 14, 15, 16, 17, 18};
        for (std::size_t index = 0; index < expected_lines.size(); ++index) {
            check(parsed.issues[index].line == expected_lines[index],
                  "unsupported declaration warning should retain its source line");
        }
    }
}

void test_dbc_parse_is_deterministic_on_replay() {
    constexpr auto source =
        "BU_: ECU_A ECU_B\n"
        "BO_ 291 Status: 8 ECU_A\n"
        " SG_ Speed : 0|16@1+ (0.1,0) [0|250] \"km/h\" ECU_B\n";
    const auto first = studio::to_json(studio::parse_dbc(source));
    const auto replay = studio::to_json(studio::parse_dbc(source));
    check(studio::serialize_json(first) == studio::serialize_json(replay),
          "replaying a DBC parse should produce identical semantic output");
}

void test_dbc_message_diff_tracks_single_message_and_signal_mutations() {
    const auto baseline = studio::parse_dbc(
        "BU_: ECU_A ECU_B\n"
        "BO_ 291 VehicleStatus: 8 ECU_A\n"
        " SG_ Speed : 0|16@1+ (0.1,0) [0|250] \"km/h\" ECU_B\n"
        "BO_ 292 BodyStatus: 8 ECU_A\n"
        " SG_ DoorOpen : 0|1@1+ (1,0) [0|1] \"\" ECU_B\n"
        "BO_ 293 RemovedStatus: 8 ECU_A\n");
    const auto candidate = studio::parse_dbc(
        "BU_: ECU_A ECU_B\n"
        "BO_ 801 VehicleStatus: 8 ECU_A\n"
        " SG_ Speed : 0|16@1+ (0.01,0) [0|250] \"km/h\" ECU_B\n"
        "BO_ 292 BodyStatus: 8 ECU_A\n"
        " SG_ DoorOpen : 0|1@1+ (1,0) [0|1] \"\" ECU_B\n"
        "BO_ 294 AddedStatus: 8 ECU_A\n");

    const auto diff = studio::diff_dbc(baseline, candidate);
    check(diff.messages.size() == 4, "message diff should report two stable and two changed identities");
    if (diff.messages.size() != 4) return;

    const auto& vehicle = diff.messages[0];
    check(vehicle.kind == studio::DbcChangeKind::modified,
          "message frame ID and signal factor mutations should modify their named message");
    check(vehicle.previous_frame_id == 291 && vehicle.current_frame_id == 801,
          "message identity should remain stable when its frame ID changes");
    check(vehicle.changed_fields.size() == 2,
          "message changes should identify frame ID and signal collection changes");
    check(vehicle.signal_changes.size() == 1 &&
              vehicle.signal_changes[0].kind == studio::DbcChangeKind::modified,
          "signal changes should be reported within the owning message");
    check(vehicle.signal_changes[0].changed_fields == std::vector<std::string>{"factor"},
          "signal diff should identify the exact changed field");

    check(diff.messages[1].kind == studio::DbcChangeKind::unchanged &&
              diff.messages[1].message_name == "BodyStatus",
          "unrelated messages should remain unchanged");
    check(diff.messages[2].kind == studio::DbcChangeKind::removed &&
              diff.messages[2].message_name == "RemovedStatus",
          "removed messages should be identified");
    check(diff.messages[3].kind == studio::DbcChangeKind::added &&
              diff.messages[3].message_name == "AddedStatus",
          "added messages should be identified");
}

void test_dbc_message_diff_is_empty_for_repeated_parse() {
    const auto parsed = studio::parse_dbc(
        "BU_: ECU_A ECU_B\n"
        "BO_ 291 VehicleStatus: 8 ECU_A\n"
        " SG_ Speed : 0|16@1+ (0.1,0) [0|250] \"km/h\" ECU_B\n");
    const auto diff = studio::diff_dbc(parsed, parsed);
    check(diff.messages.size() == 1 &&
              diff.messages[0].kind == studio::DbcChangeKind::unchanged,
          "identical DBC semantic models should produce only unchanged messages");
}

void test_dbc_message_diff_uses_frame_id_when_a_name_changes() {
    const auto previous = studio::parse_dbc(
        "BU_: ECU_A ECU_B\n"
        "BO_ 291 OldName: 8 ECU_A\n"
        " SG_ Signal : 0|8@1+ (1,0) [0|255] \"\" ECU_B\n");
    const auto current = studio::parse_dbc(
        "// source line shifts must not count as semantic changes\n"
        "BU_: ECU_A ECU_B\n"
        "BO_ 291 NewName: 8 ECU_A\n"
        " SG_ Signal : 0|8@1+ (1,0) [0|255] \"\" ECU_B\n");

    const auto diff = studio::diff_dbc(previous, current);
    check(diff.messages.size() == 1 &&
              diff.messages[0].kind == studio::DbcChangeKind::modified,
          "a uniquely matched frame should report a message rename as modification");
    if (diff.messages.size() == 1) {
        check(diff.messages[0].changed_fields == std::vector<std::string>{"name"},
              "message rename should report only the name property");
        check(diff.messages[0].signal_changes.size() == 1 &&
                  diff.messages[0].signal_changes[0].kind == studio::DbcChangeKind::unchanged,
              "source line shifts should not mark the contained signal as changed");
    }
}

void test_dbc_message_diff_ignores_receiver_declaration_order() {
    const auto previous = studio::parse_dbc(
        "BU_: ECU_A ECU_B ECU_C\n"
        "BO_ 291 Status: 8 ECU_A\n"
        " SG_ Signal : 0|8@1+ (1,0) [0|255] \"\" ECU_B ECU_C\n");
    const auto current = studio::parse_dbc(
        "BU_: ECU_A ECU_B ECU_C\n"
        "BO_ 291 Status: 8 ECU_A\n"
        " SG_ Signal : 0|8@1+ (1,0) [0|255] \"\" ECU_C ECU_B\n");
    const auto diff = studio::diff_dbc(previous, current);
    check(diff.messages.size() == 1 &&
              diff.messages[0].kind == studio::DbcChangeKind::unchanged,
          "reordering signal receivers should not create a semantic message change");
}

void test_dbc_message_diff_does_not_guess_when_name_and_frame_id_change() {
    const auto previous = studio::parse_dbc(
        "BU_: ECU_A\n"
        "BO_ 291 OldName: 8 ECU_A\n");
    const auto current = studio::parse_dbc(
        "BU_: ECU_A\n"
        "BO_ 801 NewName: 8 ECU_A\n");
    const auto diff = studio::diff_dbc(previous, current);
    check(diff.messages.size() == 2 &&
              diff.messages[0].kind == studio::DbcChangeKind::removed &&
              diff.messages[1].kind == studio::DbcChangeKind::added,
          "diff should not invent identity when both the name and frame ID change");
    if (diff.messages.size() == 2) {
        check(diff.messages[0].match_basis == "none" &&
                  diff.messages[1].match_basis == "none",
              "unmatched messages should expose that no stable match basis was available");
    }
}

void test_dbc_batch_reports_cross_file_identity_candidates_without_resolving_networks() {
    const std::vector<studio::DbcBatchSource> sources{
        {"A.dbc", studio::parse_dbc("BO_ 291 Status: 8 ECU_A\n")},
        {"B.dbc", studio::parse_dbc("BO_ 291 BodyStatus: 8 ECU_B\n")},
        {"C.dbc", studio::parse_dbc("BO_ 801 Status: 8 ECU_C\n")},
        {"D.dbc", studio::parse_dbc("BO_ 291 Status: 8 ECU_D\n")},
    };

    const auto findings = studio::analyze_dbc_batch(sources);
    check(findings.size() == 2,
          "batch analysis should report shared frame IDs and names once per identity");
    if (findings.size() != 2) return;

    check(findings[0].identity == "frameId" && findings[0].key == "291" &&
              findings[0].source_indices == std::vector<std::size_t>{0, 1, 3},
          "frame ID candidates should list distinct source files deterministically");
    check(findings[1].identity == "messageName" && findings[1].key == "Status" &&
              findings[1].source_indices == std::vector<std::size_t>{0, 2, 3},
          "message name candidates should be reported independently from frame IDs");
}

void test_dbc_batch_does_not_report_identities_present_in_one_source_only() {
    const std::vector<studio::DbcBatchSource> sources{
        {"A.dbc", studio::parse_dbc("BO_ 291 Status: 8 ECU_A\nBO_ 291 Duplicate: 8 ECU_A\n")},
        {"B.dbc", studio::parse_dbc("BO_ 801 OtherNetwork: 8 ECU_B\n")},
    };
    check(studio::analyze_dbc_batch(sources).empty(),
          "batch identity candidates should require the same key in distinct source files");
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
    test_dbc_parser_warns_for_unsupported_declarations();
    test_dbc_parse_is_deterministic_on_replay();
    test_dbc_message_diff_tracks_single_message_and_signal_mutations();
    test_dbc_message_diff_is_empty_for_repeated_parse();
    test_dbc_message_diff_uses_frame_id_when_a_name_changes();
    test_dbc_message_diff_ignores_receiver_declaration_order();
    test_dbc_message_diff_does_not_guess_when_name_and_frame_id_change();
    test_dbc_batch_reports_cross_file_identity_candidates_without_resolving_networks();
    test_dbc_batch_does_not_report_identities_present_in_one_source_only();
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
