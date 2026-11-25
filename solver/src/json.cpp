#include "json.hpp"

#include <cmath>
#include <cstdlib>
#include <stdexcept>

namespace wildfire {
namespace {

constexpr int kMaxDepth = 64;

class Parser {
 public:
  Parser(const std::string& text, const std::string& source)
      : text_(text), source_(source) {}

  JsonValue parse_document() {
    skip_whitespace();
    JsonValue root = parse_value(0);
    skip_whitespace();
    if (pos_ < text_.size()) fail("unexpected content after the JSON value");
    return root;
  }

 private:
  const std::string& text_;
  const std::string& source_;
  std::size_t pos_ = 0;
  int line_ = 1;
  std::size_t line_start_ = 0;

  [[noreturn]] void fail(const std::string& msg) const {
    const std::size_t col = pos_ - line_start_ + 1;
    throw std::runtime_error(source_ + ":" + std::to_string(line_) + ":" +
                             std::to_string(col) + ": " + msg);
  }

  bool at_end() const { return pos_ >= text_.size(); }
  char peek() const { return at_end() ? '\0' : text_[pos_]; }

  void advance() {
    if (text_[pos_] == '\n') {
      ++line_;
      line_start_ = pos_ + 1;
    }
    ++pos_;
  }

  void skip_whitespace() {
    while (!at_end()) {
      const char ch = peek();
      if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r') break;
      advance();
    }
  }

  void expect(char ch) {
    if (peek() != ch) {
      fail(std::string("expected '") + ch + "'" + found());
    }
    advance();
  }

  std::string found() const {
    if (at_end()) return " but reached the end of the file";
    return std::string(" but found '") + peek() + "'";
  }

  JsonValue parse_value(int depth) {
    if (depth > kMaxDepth) fail("JSON nested too deeply");
    JsonValue v;
    v.line = line_;
    switch (peek()) {
      case '{': parse_object(v, depth); break;
      case '[': parse_array(v, depth); break;
      case '"':
        v.type = JsonValue::Type::String;
        v.string = parse_string();
        break;
      case 't': parse_literal("true"); v.type = JsonValue::Type::Bool; v.boolean = true; break;
      case 'f': parse_literal("false"); v.type = JsonValue::Type::Bool; break;
      case 'n': parse_literal("null"); break;
      default:
        if (peek() == '-' || (peek() >= '0' && peek() <= '9')) {
          v.type = JsonValue::Type::Number;
          v.number = parse_number();
        } else {
          fail("expected a value" + found());
        }
    }
    return v;
  }

  void parse_literal(const char* word) {
    for (const char* p = word; *p; ++p) {
      if (peek() != *p) fail(std::string("invalid literal, expected '") + word + "'");
      advance();
    }
  }

  void parse_object(JsonValue& v, int depth) {
    v.type = JsonValue::Type::Object;
    advance();  // '{'
    skip_whitespace();
    if (peek() == '}') { advance(); return; }
    while (true) {
      skip_whitespace();
      if (peek() != '"') fail("expected a quoted key" + found());
      const std::string key = parse_string();
      if (v.find(key)) fail("duplicate key '" + key + "'");
      skip_whitespace();
      expect(':');
      skip_whitespace();
      v.object.emplace_back(key, parse_value(depth + 1));
      skip_whitespace();
      if (peek() == ',') { advance(); continue; }
      if (peek() == '}') { advance(); return; }
      fail("expected ',' or '}'" + found());
    }
  }

  void parse_array(JsonValue& v, int depth) {
    v.type = JsonValue::Type::Array;
    advance();  // '['
    skip_whitespace();
    if (peek() == ']') { advance(); return; }
    while (true) {
      skip_whitespace();
      v.array.push_back(parse_value(depth + 1));
      skip_whitespace();
      if (peek() == ',') { advance(); continue; }
      if (peek() == ']') { advance(); return; }
      fail("expected ',' or ']'" + found());
    }
  }

  // Strict JSON number grammar, then converted with strtod.
  double parse_number() {
    const std::size_t start = pos_;
    auto digits = [&] {
      if (!(peek() >= '0' && peek() <= '9')) fail("expected a digit" + found());
      while (peek() >= '0' && peek() <= '9') advance();
    };
    if (peek() == '-') advance();
    if (peek() == '0') {
      advance();
      if (peek() >= '0' && peek() <= '9') fail("numbers may not have leading zeros");
    } else {
      digits();
    }
    if (peek() == '.') { advance(); digits(); }
    if (peek() == 'e' || peek() == 'E') {
      advance();
      if (peek() == '+' || peek() == '-') advance();
      digits();
    }
    const std::string literal = text_.substr(start, pos_ - start);
    const double value = std::strtod(literal.c_str(), nullptr);
    if (!std::isfinite(value)) fail("number out of range: " + literal);
    return value;
  }

  unsigned parse_hex4() {
    unsigned code = 0;
    for (int i = 0; i < 4; ++i) {
      const char ch = peek();
      code <<= 4;
      if (ch >= '0' && ch <= '9') code |= static_cast<unsigned>(ch - '0');
      else if (ch >= 'a' && ch <= 'f') code |= static_cast<unsigned>(ch - 'a' + 10);
      else if (ch >= 'A' && ch <= 'F') code |= static_cast<unsigned>(ch - 'A' + 10);
      else fail("invalid \\u escape");
      advance();
    }
    return code;
  }

  static void append_utf8(std::string& out, unsigned code) {
    if (code < 0x80) {
      out += static_cast<char>(code);
    } else if (code < 0x800) {
      out += static_cast<char>(0xC0 | (code >> 6));
      out += static_cast<char>(0x80 | (code & 0x3F));
    } else if (code < 0x10000) {
      out += static_cast<char>(0xE0 | (code >> 12));
      out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (code & 0x3F));
    } else {
      out += static_cast<char>(0xF0 | (code >> 18));
      out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (code & 0x3F));
    }
  }

  std::string parse_string() {
    advance();  // opening quote
    std::string out;
    while (true) {
      if (at_end()) fail("unterminated string");
      const char ch = peek();
      if (ch == '"') { advance(); return out; }
      if (static_cast<unsigned char>(ch) < 0x20) fail("control character in string");
      if (ch != '\\') { out += ch; advance(); continue; }

      advance();  // backslash
      const char esc = peek();
      if (at_end()) fail("unterminated string");
      advance();
      switch (esc) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u': {
          unsigned code = parse_hex4();
          // Surrogate pair -> one code point.
          if (code >= 0xD800 && code <= 0xDBFF && peek() == '\\') {
            advance();
            if (peek() != 'u') fail("invalid surrogate pair");
            advance();
            const unsigned low = parse_hex4();
            if (low < 0xDC00 || low > 0xDFFF) fail("invalid surrogate pair");
            code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
          }
          append_utf8(out, code);
          break;
        }
        default:
          fail(std::string("invalid escape '\\") + esc + "'");
      }
    }
  }
};

}  // namespace

const JsonValue* JsonValue::find(const std::string& key) const {
  if (type != Type::Object) return nullptr;
  for (const auto& member : object) {
    if (member.first == key) return &member.second;
  }
  return nullptr;
}

JsonValue parse_json(const std::string& text, const std::string& source) {
  return Parser(text, source).parse_document();
}

const char* json_type_name(JsonValue::Type type) {
  switch (type) {
    case JsonValue::Type::Null: return "null";
    case JsonValue::Type::Bool: return "true/false";
    case JsonValue::Type::Number: return "number";
    case JsonValue::Type::String: return "string";
    case JsonValue::Type::Array: return "array";
    case JsonValue::Type::Object: return "object";
  }
  return "unknown";
}

}  // namespace wildfire
