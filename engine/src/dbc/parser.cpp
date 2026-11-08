#include "studio/dbc.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <iterator>
#include <regex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace studio {
namespace {

const std::regex kMessagePattern(
    R"(^\s*BO_\s+([0-9]+)\s+([A-Za-z_][A-Za-z0-9_]*)\s*:\s*([0-9]+)\s+([^\s,]+)\s*$)");
const std::regex kSignalPattern(
    R"DBC(^\s*SG_\s+([A-Za-z_][A-Za-z0-9_]*)(?:\s+(M|m[0-9]+M?))?\s*:\s*([0-9]+)\|([0-9]+)@([01])([+-])\s*\(([^,()]+),([^()]+)\)\s*\[([^|\]]+)\|([^\]]+)\]\s*"([^"]*)"(?:\s+(.*?))?\s*$)DBC");

std::string_view trim(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.remove_suffix(1);
    }
    return value;
}

bool parse_unsigned(std::string_view text, std::uint32_t& result) {
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), result);
    return error == std::errc{} && end == text.data() + text.size();
}

std::vector<std::string> split_words(std::string_view text) {
    std::vector<std::string> words;
    std::size_t position = 0;
    while (position < text.size()) {
        while (position < text.size() &&
               std::isspace(static_cast<unsigned char>(text[position]))) {
            ++position;
        }
        const auto start = position;
        while (position < text.size() &&
               !std::isspace(static_cast<unsigned char>(text[position]))) {
            ++position;
        }
        if (start != position) {
            words.emplace_back(text.substr(start, position - start));
        }
    }
    return words;
}

void add_issue(DbcParseResult& result, std::string code, std::string severity,
              std::size_t line, std::string message) {
    result.issues.push_back({std::move(code), std::move(severity), line, std::move(message)});
}

bool is_namespace_symbol_line(std::string_view line) {
    static constexpr std::string_view symbols[] = {
        "NS_DESC_", "CM_", "BA_DEF_", "BA_", "VAL_", "CAT_DEF_", "CAT_",
        "FILTER", "BA_DEF_DEF_", "EV_DATA_", "ENVVAR_DATA_", "SGTYPE_",
        "SGTYPE_VAL_", "BA_DEF_SGTYPE_", "BA_SGTYPE_", "SIG_TYPE_REF_",
        "VAL_TABLE_", "SIG_GROUP_", "SIG_VALTYPE_", "SIGTYPE_VALTYPE_",
        "BO_TX_BU_", "BA_DEF_REL_", "BA_REL_", "BA_DEF_DEF_REL_",
        "BU_SG_REL_", "BU_EV_REL_", "BU_BO_REL_", "SG_MUL_VAL_",
    };
    return std::find(std::begin(symbols), std::end(symbols), line) != std::end(symbols);
}

bool is_unsupported_declaration_with_supported_prefix(std::string_view line) {
    static constexpr std::string_view prefixes[] = {
        "BO_TX_BU_", "SG_MUL_VAL_", "BU_SG_REL_", "BU_EV_REL_", "BU_BO_REL_",
    };
    for (const auto prefix : prefixes) {
        if (line.starts_with(prefix)) {
            return true;
        }
    }
    return false;
}

void add_unsupported_declaration(DbcParseResult& result, std::size_t line_number) {
    ++result.unsupported_lines;
    add_issue(result, "DBC_UNSUPPORTED_DECLARATION", "warning", line_number,
              "Declaration or source line is not interpreted or preserved by this parser");
}

Json string_array(const std::vector<std::string>& values) {
    Json::Array result;
    result.reserve(values.size());
    for (const auto& value : values) {
        result.emplace_back(value);
    }
    return Json(std::move(result));
}

} // namespace

DbcParseResult parse_dbc(std::string_view input) {
    DbcParseResult result;
    std::unordered_set<std::string> node_names;
    std::unordered_map<std::uint32_t, std::size_t> frame_ids;
    DbcMessage* current_message = nullptr;
    bool reading_namespace_symbols = false;

    std::size_t line_number = 0;
    while (!input.empty()) {
        ++line_number;
        const auto newline = input.find('\n');
        auto line = trim(input.substr(0, newline));
        if (newline == std::string_view::npos) {
            input = {};
        } else {
            input.remove_prefix(newline + 1);
        }
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
            line = trim(line);
        }
        if (line.empty() || line.starts_with("//")) {
            continue;
        }

        if (line == "NS_ :" || line == "NS_:") {
            reading_namespace_symbols = true;
            continue;
        }
        if (reading_namespace_symbols && is_namespace_symbol_line(line)) {
            continue;
        }
        reading_namespace_symbols = false;

        if (is_unsupported_declaration_with_supported_prefix(line)) {
            add_unsupported_declaration(result, line_number);
            continue;
        }

        if (line.starts_with("BU_")) {
            const auto colon = line.find(':');
            if (colon == std::string_view::npos) {
                add_issue(result, "DBC_INVALID_NODE_DECLARATION", "error", line_number,
                          "Expected ':' after BU_");
                continue;
            }
            const auto names = split_words(line.substr(colon + 1));
            for (const auto& name : names) {
                if (!node_names.insert(name).second) {
                    add_issue(result, "DBC_DUPLICATE_NODE", "error", line_number,
                              "Node is declared more than once: " + name);
                } else {
                    result.nodes.push_back(name);
                }
            }
            continue;
        }

        if (line.starts_with("BO_")) {
            std::smatch match;
            const std::string source(line);
            if (!std::regex_match(source, match, kMessagePattern)) {
                add_issue(result, "DBC_INVALID_MESSAGE_DECLARATION", "error", line_number,
                          "Could not parse BO_ message declaration");
                current_message = nullptr;
                continue;
            }

            std::uint32_t frame_id = 0;
            std::uint32_t dlc_bytes = 0;
            if (!parse_unsigned(match[1].str(), frame_id) ||
                !parse_unsigned(match[3].str(), dlc_bytes)) {
                add_issue(result, "DBC_INVALID_MESSAGE_NUMBER", "error", line_number,
                          "Frame ID or DLC is outside the supported numeric range");
                current_message = nullptr;
                continue;
            }

            const auto existing = frame_ids.find(frame_id);
            if (existing != frame_ids.end()) {
                add_issue(result, "DBC_DUPLICATE_FRAME_ID", "error", line_number,
                          "Frame ID is already declared on line " +
                              std::to_string(existing->second));
            } else {
                frame_ids.emplace(frame_id, line_number);
            }
            if (dlc_bytes > 64) {
                add_issue(result, "DBC_DLC_OUT_OF_RANGE", "error", line_number,
                          "DLC exceeds the 64-byte maximum supported by this parser");
            }

            result.messages.push_back({
                frame_id,
                match[2].str(),
                dlc_bytes,
                match[4].str(),
                {},
                line_number,
            });
            current_message = &result.messages.back();
            continue;
        }

        if (line.starts_with("SG_")) {
            if (current_message == nullptr) {
                add_issue(result, "DBC_SIGNAL_WITHOUT_MESSAGE", "error", line_number,
                          "Signal declaration does not follow a valid BO_ declaration");
                continue;
            }
            std::smatch match;
            const std::string source(line);
            if (!std::regex_match(source, match, kSignalPattern)) {
                add_issue(result, "DBC_INVALID_SIGNAL_DECLARATION", "error", line_number,
                          "Could not parse SG_ signal declaration");
                continue;
            }

            DbcSignal signal{};
            signal.name = match[1].str();
            signal.multiplex = match[2].matched ? match[2].str() : "";
            signal.little_endian = match[5].str() == "1";
            signal.signed_value = match[6].str() == "-";
            signal.factor = std::string(trim(match[7].str()));
            signal.offset = std::string(trim(match[8].str()));
            signal.minimum = std::string(trim(match[9].str()));
            signal.maximum = std::string(trim(match[10].str()));
            signal.unit = match[11].str();
            signal.line = line_number;

            if (!parse_unsigned(match[3].str(), signal.start_bit) ||
                !parse_unsigned(match[4].str(), signal.bit_length)) {
                add_issue(result, "DBC_INVALID_SIGNAL_LAYOUT", "error", line_number,
                          "Signal start bit or length is outside the supported range");
                continue;
            }
            if (signal.bit_length == 0 || signal.bit_length > 64) {
                add_issue(result, "DBC_SIGNAL_LENGTH_OUT_OF_RANGE", "error", line_number,
                          "Signal length must be between 1 and 64 bits");
            } else {
                std::uint64_t bit_position = signal.start_bit;
                bool outside_frame = false;
                for (std::uint32_t bit = 0; bit < signal.bit_length; ++bit) {
                    if (bit_position >= static_cast<std::uint64_t>(current_message->dlc_bytes) * 8) {
                        outside_frame = true;
                        break;
                    }
                    if (bit + 1 < signal.bit_length) {
                        if (signal.little_endian) {
                            ++bit_position;
                        } else {
                            bit_position = bit_position % 8 == 0
                                ? bit_position + 15
                                : bit_position - 1;
                        }
                    }
                }
                if (outside_frame) {
                    add_issue(result, "DBC_SIGNAL_OUT_OF_FRAME", "error", line_number,
                              "Signal bit layout exceeds the frame payload");
                }
            }
            signal.receivers = split_words(match[12].matched ? match[12].str() : "");
            current_message->signals.push_back(std::move(signal));
            continue;
        }

        add_unsupported_declaration(result, line_number);
    }

    for (const auto& message : result.messages) {
        if (message.transmitter != "Vector__XXX" &&
            !node_names.contains(message.transmitter)) {
            add_issue(result, "DBC_UNKNOWN_TRANSMITTER", "warning", message.line,
                      "Transmitter node is not declared: " + message.transmitter);
        }
        for (const auto& signal : message.signals) {
            for (const auto& receiver : signal.receivers) {
                if (receiver != "Vector__XXX" && !node_names.contains(receiver)) {
                    add_issue(result, "DBC_UNKNOWN_RECEIVER", "warning", signal.line,
                              "Receiver node is not declared: " + receiver);
                }
            }
        }
    }
    return result;
}

Json to_json(const DbcParseResult& result) {
    Json::Array messages;
    messages.reserve(result.messages.size());
    for (const auto& message : result.messages) {
        Json::Array signals;
        signals.reserve(message.signals.size());
        for (const auto& signal : message.signals) {
            signals.emplace_back(Json::Object{
                {"bitLength", static_cast<double>(signal.bit_length)},
                {"byteOrder", signal.little_endian ? "little-endian" : "big-endian"},
                {"factor", signal.factor},
                {"line", static_cast<double>(signal.line)},
                {"maximum", signal.maximum},
                {"minimum", signal.minimum},
                {"multiplex", signal.multiplex},
                {"name", signal.name},
                {"offset", signal.offset},
                {"receivers", string_array(signal.receivers)},
                {"signed", signal.signed_value},
                {"startBit", static_cast<double>(signal.start_bit)},
                {"unit", signal.unit},
            });
        }
        messages.emplace_back(Json::Object{
            {"dlcBytes", static_cast<double>(message.dlc_bytes)},
            {"frameId", static_cast<double>(message.frame_id)},
            {"line", static_cast<double>(message.line)},
            {"name", message.name},
            {"signals", Json(std::move(signals))},
            {"transmitter", message.transmitter},
        });
    }

    Json::Array issues;
    issues.reserve(result.issues.size());
    for (const auto& issue : result.issues) {
        issues.emplace_back(Json::Object{
            {"code", issue.code},
            {"line", static_cast<double>(issue.line)},
            {"message", issue.message},
            {"severity", issue.severity},
        });
    }

    return Json::Object{
        {"issues", Json(std::move(issues))},
        {"messages", Json(std::move(messages))},
        {"nodes", string_array(result.nodes)},
        {"unsupportedLines", static_cast<double>(result.unsupported_lines)},
    };
}

} // namespace studio
