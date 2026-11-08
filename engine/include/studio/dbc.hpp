#pragma once

#include "studio/json.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace studio {

struct DbcIssue {
    std::string code;
    std::string severity;
    std::size_t line;
    std::string message;
};

struct DbcSignal {
    std::string name;
    std::uint32_t start_bit;
    std::uint32_t bit_length;
    bool little_endian;
    bool signed_value;
    std::string factor;
    std::string offset;
    std::string minimum;
    std::string maximum;
    std::string unit;
    std::string multiplex;
    std::vector<std::string> receivers;
    std::size_t line;
};

struct DbcMessage {
    std::uint32_t frame_id;
    std::string name;
    std::uint32_t dlc_bytes;
    std::string transmitter;
    std::vector<DbcSignal> signals;
    std::size_t line;
};

struct DbcParseResult {
    std::vector<std::string> nodes;
    std::vector<DbcMessage> messages;
    std::vector<DbcIssue> issues;
    std::size_t unsupported_lines = 0;
};

enum class DbcChangeKind {
    added,
    removed,
    modified,
    unchanged,
};

struct DbcSignalChange {
    DbcChangeKind kind = DbcChangeKind::unchanged;
    std::string name;
    std::vector<std::string> changed_fields;
};

struct DbcMessageChange {
    DbcChangeKind kind = DbcChangeKind::unchanged;
    std::string message_name;
    std::string match_basis;
    std::optional<std::uint32_t> previous_frame_id;
    std::optional<std::uint32_t> current_frame_id;
    std::vector<std::string> changed_fields;
    std::vector<DbcSignalChange> signal_changes;
};

struct DbcDiffResult {
    std::vector<DbcMessageChange> messages;
};

struct DbcBatchFinding {
    std::string identity;
    std::string key;
    std::vector<std::size_t> source_indices;
};

struct DbcBatchSource {
    std::string name;
    DbcParseResult parsed;
};

[[nodiscard]] DbcParseResult parse_dbc(std::string_view input, bool allow_can_fd = false);
[[nodiscard]] std::vector<DbcBatchFinding> analyze_dbc_batch(
    const std::vector<DbcBatchSource>& sources);
[[nodiscard]] DbcDiffResult diff_dbc(
    const DbcParseResult& previous, const DbcParseResult& current);
[[nodiscard]] Json to_json(const DbcParseResult& result);
[[nodiscard]] Json to_json(const DbcDiffResult& result);
[[nodiscard]] const char* to_string(DbcChangeKind kind);

} // namespace studio
