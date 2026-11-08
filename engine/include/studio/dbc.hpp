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

[[nodiscard]] DbcParseResult parse_dbc(std::string_view input);
[[nodiscard]] Json to_json(const DbcParseResult& result);

} // namespace studio
