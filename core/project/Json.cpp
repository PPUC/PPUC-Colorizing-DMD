#include "project/Json.h"

#include <cstdio>
#include <limits>

namespace colorizing::project {

JsonValue::JsonValue(bool value)
    : m_type(Type::Bool),
      m_bool(value)
{
}

JsonValue::JsonValue(std::string value)
    : m_type(Type::String),
      m_string(std::move(value))
{
}

JsonValue::JsonValue(const char* value)
    : JsonValue(std::string(value))
{
}

JsonValue JsonValue::array()
{
    JsonValue value;
    value.m_type = Type::Array;
    return value;
}

JsonValue JsonValue::object()
{
    JsonValue value;
    value.m_type = Type::Object;
    return value;
}

void JsonValue::push(JsonValue value)
{
    m_items.push_back(std::move(value));
}

JsonValue& JsonValue::set(const std::string& key, JsonValue value)
{
    for (Member& member : m_members) {
        if (member.first == key) {
            member.second = std::move(value);
            return member.second;
        }
    }
    m_members.emplace_back(key, std::move(value));
    return m_members.back().second;
}

const JsonValue* JsonValue::find(std::string_view key) const
{
    for (const Member& member : m_members) {
        if (member.first == key) {
            return &member.second;
        }
    }
    return nullptr;
}

std::optional<bool> JsonValue::boolValue() const
{
    if (m_type != Type::Bool) {
        return std::nullopt;
    }
    return m_bool;
}

std::optional<int64_t> JsonValue::intValue() const
{
    if (m_type != Type::Number) {
        return std::nullopt;
    }
    return m_number;
}

const std::string* JsonValue::stringValue() const
{
    return m_type == Type::String ? &m_string : nullptr;
}

namespace {

void writeString(std::string& out, const std::string& text)
{
    out.push_back('"');
    for (const char character : text) {
        const auto byte = static_cast<unsigned char>(character);
        switch (character) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (byte < 0x20) {
                char escaped[8];
                std::snprintf(escaped, sizeof(escaped), "\\u%04x", byte);
                out += escaped;
            } else {
                out.push_back(character);
            }
        }
    }
    out.push_back('"');
}

void writeValue(std::string& out, const JsonValue& value, int depth)
{
    const auto indent = [&out](int level) { out.append(static_cast<std::size_t>(level) * 2, ' '); };
    switch (value.type()) {
    case JsonValue::Type::Null:
        out += "null";
        break;
    case JsonValue::Type::Bool:
        out += *value.boolValue() ? "true" : "false";
        break;
    case JsonValue::Type::Number:
        out += std::to_string(*value.intValue());
        break;
    case JsonValue::Type::String:
        writeString(out, *value.stringValue());
        break;
    case JsonValue::Type::Array:
        if (value.items().empty()) {
            out += "[]";
            break;
        }
        out += "[\n";
        for (std::size_t i = 0; i < value.items().size(); ++i) {
            indent(depth + 1);
            writeValue(out, value.items()[i], depth + 1);
            out += i + 1 < value.items().size() ? ",\n" : "\n";
        }
        indent(depth);
        out.push_back(']');
        break;
    case JsonValue::Type::Object:
        if (value.members().empty()) {
            out += "{}";
            break;
        }
        out += "{\n";
        for (std::size_t i = 0; i < value.members().size(); ++i) {
            indent(depth + 1);
            writeString(out, value.members()[i].first);
            out += ": ";
            writeValue(out, value.members()[i].second, depth + 1);
            out += i + 1 < value.members().size() ? ",\n" : "\n";
        }
        indent(depth);
        out.push_back('}');
        break;
    }
}

constexpr int kMaxDepth = 64;

class Parser {
public:
    explicit Parser(std::string_view text)
        : m_text(text)
    {
    }

    std::optional<JsonValue> parse(std::string* errorMessage)
    {
        std::optional<JsonValue> value = parseValue(0);
        skipSpace();
        if (value && m_position != m_text.size()) {
            fail("text after the value");
            value.reset();
        }
        if (!value && errorMessage) {
            *errorMessage = m_error;
        }
        return value;
    }

private:
    void fail(const char* problem)
    {
        if (m_error.empty()) {
            m_error = std::string(problem) + " at byte " + std::to_string(m_position);
        }
    }

    void skipSpace()
    {
        while (m_position < m_text.size() &&
               (m_text[m_position] == ' ' || m_text[m_position] == '\n' ||
                m_text[m_position] == '\r' || m_text[m_position] == '\t')) {
            ++m_position;
        }
    }

    bool consume(std::string_view word)
    {
        if (m_text.substr(m_position, word.size()) != word) {
            return false;
        }
        m_position += word.size();
        return true;
    }

    std::optional<JsonValue> parseValue(int depth)
    {
        skipSpace();
        if (m_position >= m_text.size()) {
            fail("missing value");
            return std::nullopt;
        }
        const char next = m_text[m_position];
        if ((next == '{' || next == '[') && depth >= kMaxDepth) {
            fail("nesting too deep");
            return std::nullopt;
        }
        if (next == '{') {
            return parseObject(depth);
        }
        if (next == '[') {
            return parseArray(depth);
        }
        if (next == '"') {
            std::optional<std::string> text = parseString();
            if (!text) {
                return std::nullopt;
            }
            return JsonValue(std::move(*text));
        }
        if (consume("true")) {
            return JsonValue(true);
        }
        if (consume("false")) {
            return JsonValue(false);
        }
        if (consume("null")) {
            return JsonValue();
        }
        if (next == '-' || (next >= '0' && next <= '9')) {
            return parseNumber();
        }
        fail("unexpected character");
        return std::nullopt;
    }

    std::optional<JsonValue> parseNumber()
    {
        const bool negative = m_text[m_position] == '-';
        if (negative) {
            ++m_position;
        }
        const std::size_t start = m_position;
        uint64_t magnitude = 0;
        const uint64_t limit = negative
                                   ? uint64_t{1} << 63
                                   : static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
        while (m_position < m_text.size() && m_text[m_position] >= '0' &&
               m_text[m_position] <= '9') {
            const auto digit = static_cast<uint64_t>(m_text[m_position] - '0');
            if (magnitude > (limit - digit) / 10) {
                fail("number out of range");
                return std::nullopt;
            }
            magnitude = magnitude * 10 + digit;
            ++m_position;
        }
        if (m_position == start || (m_text[start] == '0' && m_position - start > 1)) {
            fail("malformed number");
            return std::nullopt;
        }
        if (m_position < m_text.size() &&
            (m_text[m_position] == '.' || m_text[m_position] == 'e' ||
             m_text[m_position] == 'E')) {
            fail("number that is no integer");
            return std::nullopt;
        }
        if (negative) {
            return JsonValue(magnitude == uint64_t{1} << 63
                                 ? std::numeric_limits<int64_t>::min()
                                 : -static_cast<int64_t>(magnitude));
        }
        return JsonValue(static_cast<int64_t>(magnitude));
    }

    std::optional<uint32_t> parseHex4()
    {
        if (m_text.size() - m_position < 4) {
            fail("incomplete escape");
            return std::nullopt;
        }
        uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = m_text[m_position++];
            value <<= 4;
            if (c >= '0' && c <= '9') {
                value |= static_cast<uint32_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                value |= static_cast<uint32_t>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                value |= static_cast<uint32_t>(c - 'A' + 10);
            } else {
                fail("malformed escape");
                return std::nullopt;
            }
        }
        return value;
    }

    static void appendUtf8(std::string& out, uint32_t code)
    {
        if (code < 0x80) {
            out.push_back(static_cast<char>(code));
        } else if (code < 0x800) {
            out.push_back(static_cast<char>(0xc0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3f)));
        } else if (code < 0x10000) {
            out.push_back(static_cast<char>(0xe0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3f)));
        } else {
            out.push_back(static_cast<char>(0xf0 | (code >> 18)));
            out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3f)));
        }
    }

    std::optional<std::string> parseString()
    {
        ++m_position;
        std::string out;
        while (m_position < m_text.size()) {
            const char c = m_text[m_position++];
            if (c == '"') {
                return out;
            }
            if (static_cast<unsigned char>(c) < 0x20) {
                fail("control character in a string");
                return std::nullopt;
            }
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (m_position >= m_text.size()) {
                break;
            }
            const char escape = m_text[m_position++];
            switch (escape) {
            case '"':
            case '\\':
            case '/':
                out.push_back(escape);
                break;
            case 'b':
                out.push_back('\b');
                break;
            case 'f':
                out.push_back('\f');
                break;
            case 'n':
                out.push_back('\n');
                break;
            case 'r':
                out.push_back('\r');
                break;
            case 't':
                out.push_back('\t');
                break;
            case 'u': {
                std::optional<uint32_t> code = parseHex4();
                if (!code) {
                    return std::nullopt;
                }
                if (*code >= 0xd800 && *code < 0xdc00) {
                    // A surrogate pair.
                    std::optional<uint32_t> low;
                    if (consume("\\u")) {
                        low = parseHex4();
                    }
                    if (!low || *low < 0xdc00 || *low >= 0xe000) {
                        fail("unpaired surrogate");
                        return std::nullopt;
                    }
                    *code = 0x10000 + ((*code - 0xd800) << 10) + (*low - 0xdc00);
                } else if (*code >= 0xdc00 && *code < 0xe000) {
                    fail("unpaired surrogate");
                    return std::nullopt;
                }
                appendUtf8(out, *code);
                break;
            }
            default:
                fail("unknown escape");
                return std::nullopt;
            }
        }
        fail("unterminated string");
        return std::nullopt;
    }

    std::optional<JsonValue> parseArray(int depth)
    {
        ++m_position;
        JsonValue array = JsonValue::array();
        skipSpace();
        if (consume("]")) {
            return array;
        }
        while (true) {
            std::optional<JsonValue> item = parseValue(depth + 1);
            if (!item) {
                return std::nullopt;
            }
            array.push(std::move(*item));
            skipSpace();
            if (consume("]")) {
                return array;
            }
            if (!consume(",")) {
                fail("expected , or ]");
                return std::nullopt;
            }
        }
    }

    std::optional<JsonValue> parseObject(int depth)
    {
        ++m_position;
        JsonValue object = JsonValue::object();
        skipSpace();
        if (consume("}")) {
            return object;
        }
        while (true) {
            skipSpace();
            if (m_position >= m_text.size() || m_text[m_position] != '"') {
                fail("expected a member name");
                return std::nullopt;
            }
            std::optional<std::string> key = parseString();
            if (!key) {
                return std::nullopt;
            }
            skipSpace();
            if (!consume(":")) {
                fail("expected :");
                return std::nullopt;
            }
            std::optional<JsonValue> value = parseValue(depth + 1);
            if (!value) {
                return std::nullopt;
            }
            object.set(*key, std::move(*value));
            skipSpace();
            if (consume("}")) {
                return object;
            }
            if (!consume(",")) {
                fail("expected , or }");
                return std::nullopt;
            }
        }
    }

    std::string_view m_text;
    std::size_t m_position = 0;
    std::string m_error;
};

const std::vector<JsonValue>& emptyItems()
{
    static const std::vector<JsonValue> empty;
    return empty;
}

}  // namespace

std::string writeJson(const JsonValue& value)
{
    std::string out;
    writeValue(out, value, 0);
    out.push_back('\n');
    return out;
}

std::optional<JsonValue> parseJson(std::string_view text, std::string* errorMessage)
{
    return Parser(text).parse(errorMessage);
}

JsonObjectReader::JsonObjectReader(const JsonValue& object, std::string what)
    : m_object(object),
      m_what(std::move(what))
{
    if (object.type() != JsonValue::Type::Object) {
        throw JsonFormatError("the " + m_what + " is not a JSON object");
    }
}

void JsonObjectReader::fail(std::string_view key, const char* problem) const
{
    throw JsonFormatError("\"" + std::string(key) + "\" of the " + m_what + " " + problem);
}

bool JsonObjectReader::has(std::string_view key) const
{
    const JsonValue* value = m_object.find(key);
    return value && !value->isNull();
}

const JsonValue& JsonObjectReader::require(std::string_view key) const
{
    const JsonValue* value = m_object.find(key);
    if (!value) {
        fail(key, "is missing");
    }
    return *value;
}

int64_t JsonObjectReader::integer(std::string_view key, int64_t min, int64_t max) const
{
    const std::optional<int64_t> value = require(key).intValue();
    if (!value) {
        fail(key, "is no integer");
    }
    if (*value < min || *value > max) {
        fail(key, "is out of range");
    }
    return *value;
}

int64_t JsonObjectReader::integer(std::string_view key,
                                  int64_t min,
                                  int64_t max,
                                  int64_t fallback) const
{
    return has(key) ? integer(key, min, max) : fallback;
}

bool JsonObjectReader::boolean(std::string_view key, bool fallback) const
{
    if (!has(key)) {
        return fallback;
    }
    const std::optional<bool> value = require(key).boolValue();
    if (!value) {
        fail(key, "is no boolean");
    }
    return *value;
}

const std::string& JsonObjectReader::string(std::string_view key) const
{
    const std::string* value = require(key).stringValue();
    if (!value) {
        fail(key, "is no string");
    }
    return *value;
}

std::string JsonObjectReader::string(std::string_view key, const std::string& fallback) const
{
    return has(key) ? string(key) : fallback;
}

const std::vector<JsonValue>& JsonObjectReader::array(std::string_view key) const
{
    const JsonValue& value = require(key);
    if (value.type() != JsonValue::Type::Array) {
        fail(key, "is no array");
    }
    return value.items();
}

const std::vector<JsonValue>& JsonObjectReader::optionalArray(std::string_view key) const
{
    return has(key) ? array(key) : emptyItems();
}

JsonObjectReader JsonObjectReader::object(std::string_view key) const
{
    return JsonObjectReader(require(key), m_what + " \"" + std::string(key) + "\"");
}

JsonObjectReader objectItem(const JsonValue& item, const std::string& what)
{
    return JsonObjectReader(item, what);
}

}  // namespace colorizing::project
