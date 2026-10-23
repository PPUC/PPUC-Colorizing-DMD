#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace colorizing::project {

// What the metadata entries of a project file (ProjectFile.h) are written in. Numbers are
// integers only: the files hold no fractions. Object members keep their order, so that a file
// is written the same way every time.
class JsonValue {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };
    using Member = std::pair<std::string, JsonValue>;

    JsonValue() = default;
    JsonValue(bool value);
    // Any integer type; a value beyond int64_t does not fit (none of the files has one).
    template <typename T,
              std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>, int> = 0>
    JsonValue(T value)
        : m_type(Type::Number),
          m_number(static_cast<int64_t>(value))
    {
    }
    JsonValue(std::string value);
    JsonValue(const char* value);

    [[nodiscard]] static JsonValue array();
    [[nodiscard]] static JsonValue object();

    [[nodiscard]] Type type() const { return m_type; }
    [[nodiscard]] bool isNull() const { return m_type == Type::Null; }

    // Arrays.
    void push(JsonValue value);
    [[nodiscard]] const std::vector<JsonValue>& items() const { return m_items; }
    // Objects; set() replaces a member of the same key.
    JsonValue& set(const std::string& key, JsonValue value);
    [[nodiscard]] const std::vector<Member>& members() const { return m_members; }
    [[nodiscard]] const JsonValue* find(std::string_view key) const;

    // The value as the type; nothing for another type.
    [[nodiscard]] std::optional<bool> boolValue() const;
    [[nodiscard]] std::optional<int64_t> intValue() const;
    [[nodiscard]] const std::string* stringValue() const;

private:
    Type m_type = Type::Null;
    bool m_bool = false;
    int64_t m_number = 0;
    std::string m_string;
    std::vector<JsonValue> m_items;
    std::vector<Member> m_members;
};

// Indented by two spaces, one member or item per line; strings in UTF-8, escaped where JSON
// needs it.
[[nodiscard]] std::string writeJson(const JsonValue& value);

// Nothing for text that is no JSON (or has a number with a fraction or exponent, or nesting
// deeper than 64); errorMessage then says where.
[[nodiscard]] std::optional<JsonValue> parseJson(std::string_view text,
                                                 std::string* errorMessage = nullptr);

// What the readers of the project file throw for a value that is missing or has the wrong
// type or range; the message names it.
class JsonFormatError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Typed access to members of an object, throwing JsonFormatError: `what` names the object in
// the message ("manifest", "frame order").
class JsonObjectReader {
public:
    JsonObjectReader(const JsonValue& object, std::string what);

    [[nodiscard]] bool has(std::string_view key) const;
    [[nodiscard]] int64_t integer(std::string_view key, int64_t min, int64_t max) const;
    [[nodiscard]] int64_t integer(std::string_view key,
                                  int64_t min,
                                  int64_t max,
                                  int64_t fallback) const;
    [[nodiscard]] bool boolean(std::string_view key, bool fallback) const;
    [[nodiscard]] const std::string& string(std::string_view key) const;
    [[nodiscard]] std::string string(std::string_view key, const std::string& fallback) const;
    [[nodiscard]] const std::vector<JsonValue>& array(std::string_view key) const;
    // An empty array for a missing member.
    [[nodiscard]] const std::vector<JsonValue>& optionalArray(std::string_view key) const;
    [[nodiscard]] JsonObjectReader object(std::string_view key) const;
    [[nodiscard]] const std::string& what() const { return m_what; }

private:
    [[nodiscard]] const JsonValue& require(std::string_view key) const;
    [[noreturn]] void fail(std::string_view key, const char* problem) const;

    const JsonValue& m_object;
    std::string m_what;
};

// An array item as an object, throwing JsonFormatError for anything else.
[[nodiscard]] JsonObjectReader objectItem(const JsonValue& item, const std::string& what);

}  // namespace colorizing::project
