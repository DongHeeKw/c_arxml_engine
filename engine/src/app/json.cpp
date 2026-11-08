#include "studio/json.hpp"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>

namespace studio {
namespace {

class Parser {
public:
    explicit Parser(std::string_view input) : input_(input) {}

    bool parse(Json& output, std::string& error) {
        skip_whitespace();
        if (!parse_value(output, error)) {
            return false;
        }
        skip_whitespace();
        if (position_ != input_.size()) {
            error = "Unexpected characters after JSON value";
            return false;
        }
        return true;
    }

private:
    bool parse_value(Json& output, std::string& error) {
        if (position_ >= input_.size()) {
            error = "Unexpected end of input";
            return false;
        }

        switch (input_[position_]) {
        case 'n':
            return parse_literal("null", Json(nullptr), output, error);
        case 't':
            return parse_literal("true", Json(true), output, error);
        case 'f':
            return parse_literal("false", Json(false), output, error);
        case '"': {
            std::string text;
            if (!parse_string(text, error)) {
                return false;
            }
            output = Json(std::move(text));
            return true;
        }
        case '[':
            return parse_array(output, error);
        case '{':
            return parse_object(output, error);
        default:
            if (input_[position_] == '-' || is_digit(input_[position_])) {
                return parse_number(output, error);
            }
            error = "Invalid JSON value";
            return false;
        }
    }

    bool parse_literal(std::string_view literal, Json value, Json& output, std::string& error) {
        if (input_.substr(position_, literal.size()) != literal) {
            error = "Invalid JSON literal";
            return false;
        }
        position_ += literal.size();
        output = std::move(value);
        return true;
    }

    bool parse_string(std::string& output, std::string& error) {
        ++position_;
        while (position_ < input_.size()) {
            const char current = input_[position_++];
            if (current == '"') {
                return true;
            }
            if (static_cast<unsigned char>(current) < 0x20) {
                error = "Unescaped control character in JSON string";
                return false;
            }
            if (current != '\\') {
                output.push_back(current);
                continue;
            }

            if (position_ >= input_.size()) {
                error = "Incomplete JSON escape";
                return false;
            }
            const char escaped = input_[position_++];
            switch (escaped) {
            case '"': output.push_back('"'); break;
            case '\\': output.push_back('\\'); break;
            case '/': output.push_back('/'); break;
            case 'b': output.push_back('\b'); break;
            case 'f': output.push_back('\f'); break;
            case 'n': output.push_back('\n'); break;
            case 'r': output.push_back('\r'); break;
            case 't': output.push_back('\t'); break;
            case 'u': {
                std::uint32_t code_point = 0;
                if (!parse_hex_quad(code_point, error)) {
                    return false;
                }
                if (code_point >= 0xD800 && code_point <= 0xDBFF) {
                    if (input_.size() - position_ < 6 ||
                        input_[position_] != '\\' || input_[position_ + 1] != 'u') {
                        error = "Missing low surrogate in JSON escape";
                        return false;
                    }
                    position_ += 2;
                    std::uint32_t low_surrogate = 0;
                    if (!parse_hex_quad(low_surrogate, error)) {
                        return false;
                    }
                    if (low_surrogate < 0xDC00 || low_surrogate > 0xDFFF) {
                        error = "Invalid low surrogate in JSON escape";
                        return false;
                    }
                    code_point = 0x10000 + ((code_point - 0xD800) << 10) +
                                 (low_surrogate - 0xDC00);
                } else if (code_point >= 0xDC00 && code_point <= 0xDFFF) {
                    error = "Unexpected low surrogate in JSON escape";
                    return false;
                }
                append_utf8(code_point, output);
                break;
            }
            default:
                error = "Invalid JSON escape";
                return false;
            }
        }

        error = "Unterminated JSON string";
        return false;
    }

    bool parse_hex_quad(std::uint32_t& output, std::string& error) {
        if (input_.size() - position_ < 4) {
            error = "Incomplete Unicode escape";
            return false;
        }
        output = 0;
        for (int index = 0; index < 4; ++index) {
            const char digit = input_[position_++];
            output <<= 4;
            if (digit >= '0' && digit <= '9') {
                output += static_cast<std::uint32_t>(digit - '0');
            } else if (digit >= 'a' && digit <= 'f') {
                output += static_cast<std::uint32_t>(digit - 'a' + 10);
            } else if (digit >= 'A' && digit <= 'F') {
                output += static_cast<std::uint32_t>(digit - 'A' + 10);
            } else {
                error = "Invalid hexadecimal digit in Unicode escape";
                return false;
            }
        }
        return true;
    }

    static void append_utf8(std::uint32_t code_point, std::string& output) {
        if (code_point <= 0x7F) {
            output.push_back(static_cast<char>(code_point));
        } else if (code_point <= 0x7FF) {
            output.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
            output.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
        } else if (code_point <= 0xFFFF) {
            output.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
            output.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
        } else {
            output.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
            output.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
        }
    }

    bool parse_array(Json& output, std::string& error) {
        ++position_;
        skip_whitespace();
        Json::Array values;
        if (consume(']')) {
            output = Json(std::move(values));
            return true;
        }
        while (true) {
            Json item;
            if (!parse_value(item, error)) {
                return false;
            }
            values.push_back(std::move(item));
            skip_whitespace();
            if (consume(']')) {
                output = Json(std::move(values));
                return true;
            }
            if (!consume(',')) {
                error = "Expected ',' or ']' in JSON array";
                return false;
            }
            skip_whitespace();
        }
    }

    bool parse_object(Json& output, std::string& error) {
        ++position_;
        skip_whitespace();
        Json::Object values;
        if (consume('}')) {
            output = Json(std::move(values));
            return true;
        }
        while (true) {
            if (position_ >= input_.size() || input_[position_] != '"') {
                error = "Expected string key in JSON object";
                return false;
            }
            std::string key;
            if (!parse_string(key, error)) {
                return false;
            }
            skip_whitespace();
            if (!consume(':')) {
                error = "Expected ':' after JSON object key";
                return false;
            }
            skip_whitespace();
            Json value;
            if (!parse_value(value, error)) {
                return false;
            }
            if (!values.emplace(std::move(key), std::move(value)).second) {
                error = "Duplicate key in JSON object";
                return false;
            }
            skip_whitespace();
            if (consume('}')) {
                output = Json(std::move(values));
                return true;
            }
            if (!consume(',')) {
                error = "Expected ',' or '}' in JSON object";
                return false;
            }
            skip_whitespace();
        }
    }

    bool parse_number(Json& output, std::string& error) {
        const std::size_t start = position_;
        if (consume('-') && position_ == input_.size()) {
            error = "Incomplete JSON number";
            return false;
        }
        if (consume('0')) {
            if (position_ < input_.size() && is_digit(input_[position_])) {
                error = "Leading zero in JSON number";
                return false;
            }
        } else {
            if (position_ >= input_.size() || input_[position_] < '1' || input_[position_] > '9') {
                error = "Invalid JSON number";
                return false;
            }
            while (position_ < input_.size() && is_digit(input_[position_])) {
                ++position_;
            }
        }
        if (consume('.')) {
            const std::size_t fraction_start = position_;
            while (position_ < input_.size() && is_digit(input_[position_])) {
                ++position_;
            }
            if (fraction_start == position_) {
                error = "Missing digits after decimal point";
                return false;
            }
        }
        if (consume('e') || consume('E')) {
            consume('+') || consume('-');
            const std::size_t exponent_start = position_;
            while (position_ < input_.size() && is_digit(input_[position_])) {
                ++position_;
            }
            if (exponent_start == position_) {
                error = "Missing digits in exponent";
                return false;
            }
        }

        double number = 0;
        const auto text = input_.substr(start, position_ - start);
        const auto [end, status] = std::from_chars(text.data(), text.data() + text.size(), number);
        if (status != std::errc{} || end != text.data() + text.size() || !std::isfinite(number)) {
            error = "JSON number is outside the supported range";
            return false;
        }
        output = Json(number);
        return true;
    }

    void skip_whitespace() {
        while (position_ < input_.size()) {
            const char current = input_[position_];
            if (current != ' ' && current != '\t' && current != '\n' && current != '\r') {
                break;
            }
            ++position_;
        }
    }

    bool consume(char expected) {
        if (position_ < input_.size() && input_[position_] == expected) {
            ++position_;
            return true;
        }
        return false;
    }

    static bool is_digit(char value) {
        return value >= '0' && value <= '9';
    }

    std::string_view input_;
    std::size_t position_ = 0;
};

void append_escaped_string(std::string_view input, std::string& output) {
    static constexpr char hex_digits[] = "0123456789abcdef";
    output.push_back('"');
    for (const unsigned char character : input) {
        switch (character) {
        case '"': output += "\\\""; break;
        case '\\': output += "\\\\"; break;
        case '\b': output += "\\b"; break;
        case '\f': output += "\\f"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if (character < 0x20) {
                output += "\\u00";
                output.push_back(hex_digits[character >> 4]);
                output.push_back(hex_digits[character & 0x0F]);
            } else {
                output.push_back(static_cast<char>(character));
            }
        }
    }
    output.push_back('"');
}

void append_serialized(const Json& value, std::string& output) {
    if (std::holds_alternative<std::nullptr_t>(value.value)) {
        output += "null";
    } else if (const auto* boolean = std::get_if<bool>(&value.value)) {
        output += *boolean ? "true" : "false";
    } else if (const auto* number = std::get_if<double>(&value.value)) {
        char buffer[64];
        const auto [end, status] = std::to_chars(buffer, buffer + sizeof(buffer), *number);
        if (status == std::errc{}) {
            output.append(buffer, end);
        } else {
            output += "null";
        }
    } else if (const auto* text = std::get_if<std::string>(&value.value)) {
        append_escaped_string(*text, output);
    } else if (const auto* array = std::get_if<Json::Array>(&value.value)) {
        output.push_back('[');
        bool first = true;
        for (const auto& item : *array) {
            if (!first) output.push_back(',');
            first = false;
            append_serialized(item, output);
        }
        output.push_back(']');
    } else if (const auto* object = std::get_if<Json::Object>(&value.value)) {
        output.push_back('{');
        bool first = true;
        for (const auto& [key, item] : *object) {
            if (!first) output.push_back(',');
            first = false;
            append_escaped_string(key, output);
            output.push_back(':');
            append_serialized(item, output);
        }
        output.push_back('}');
    }
}

} // namespace

const Json::Object* Json::as_object() const {
    return std::get_if<Object>(&value);
}

const Json::Array* Json::as_array() const {
    return std::get_if<Array>(&value);
}

const std::string* Json::as_string() const {
    return std::get_if<std::string>(&value);
}

bool Json::is_null() const {
    return std::holds_alternative<std::nullptr_t>(value);
}

bool parse_json(std::string_view input, Json& output, JsonParseError& error) {
    std::string message;
    Parser parser(input);
    if (!parser.parse(output, message)) {
        error.message = std::move(message);
        return false;
    }
    return true;
}

std::string serialize_json(const Json& value) {
    std::string output;
    append_serialized(value, output);
    return output;
}

} // namespace studio
