#pragma once

#include <string>
#include <utility>
#include <vector>

namespace wildfire {

// A parsed JSON value. Small and dependency-free: enough for settings.json.
struct JsonValue {
  enum class Type { Null, Bool, Number, String, Array, Object };

  Type type = Type::Null;
  bool boolean = false;
  double number = 0.0;
  std::string string;
  std::vector<JsonValue> array;
  // Members in file order. Duplicate keys are rejected by the parser.
  std::vector<std::pair<std::string, JsonValue>> object;
  int line = 0;  // line where the value starts (for error messages)

  bool is_number() const { return type == Type::Number; }
  bool is_array() const { return type == Type::Array; }
  bool is_object() const { return type == Type::Object; }

  // In:  key (string)
  // Out: pointer to the member, or nullptr if absent / not an object
  // Why: look up settings by name.
  const JsonValue* find(const std::string& key) const;
};

// In:  text (string): the whole JSON document
//      source (string): file name used in error messages
// Out: JsonValue holding the document's root value
// Why: read settings.json without an external library.
// Throws std::runtime_error "source:line:col: message" on any syntax error,
// duplicate object key, nesting deeper than 64 levels, or trailing content.
JsonValue parse_json(const std::string& text, const std::string& source);

// In:  type (JsonValue::Type)
// Out: const char*, e.g. "number"
// Why: readable "expected X, got Y" messages.
const char* json_type_name(JsonValue::Type type);

}  // namespace wildfire
