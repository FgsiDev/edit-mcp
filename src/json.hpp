#pragma once

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace json {

struct ParseError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

class Value {
 public:
  enum class Type { Null, Bool, Number, String, Array, Object };

  Value() = default;
  Value(std::nullptr_t) {}
  Value(bool b) : type(Type::Bool), boolean(b) {}
  Value(double n) : type(Type::Number), number(n) {}
  template <class T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>, int> = 0>
  Value(T n) : type(Type::Number), number(static_cast<double>(n)) {}
  Value(std::string s) : type(Type::String), text(std::move(s)) {}
  Value(const char* s) : type(Type::String), text(s) {}

  static Value array() {
    Value v;
    v.type = Type::Array;
    return v;
  }

  static Value object() {
    Value v;
    v.type = Type::Object;
    return v;
  }

  bool isObject() const { return type == Type::Object; }
  bool isArray() const { return type == Type::Array; }
  bool isString() const { return type == Type::String; }
  bool isNumber() const { return type == Type::Number; }
  bool isBool() const { return type == Type::Bool; }
  bool isNull() const { return type == Type::Null; }

  const Value* find(const std::string& key) const {
    if (type != Type::Object) return nullptr;
    for (size_t i = 0; i < keys.size(); ++i) {
      if (keys[i] == key) return &items[i];
    }
    return nullptr;
  }

  Value& set(const std::string& key, Value v) {
    for (size_t i = 0; i < keys.size(); ++i) {
      if (keys[i] == key) {
        items[i] = std::move(v);
        return items[i];
      }
    }
    keys.push_back(key);
    items.push_back(std::move(v));
    return items.back();
  }

  Value& push(Value v) {
    items.push_back(std::move(v));
    return items.back();
  }

  std::string dump() const {
    std::string out;
    write(out);
    return out;
  }

  static Value parse(const std::string& source);

  Type type = Type::Null;
  bool boolean = false;
  double number = 0;
  std::string text;
  std::vector<Value> items;
  std::vector<std::string> keys;

 private:
  static void escape(std::string& out, const std::string& s) {
    out.push_back('"');
    for (unsigned char c : s) {
      switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        default:
          if (c < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof buf, "\\u%04x", c);
            out += buf;
          } else {
            out.push_back(static_cast<char>(c));
          }
      }
    }
    out.push_back('"');
  }

  void write(std::string& out) const {
    switch (type) {
      case Type::Null: out += "null"; break;
      case Type::Bool: out += boolean ? "true" : "false"; break;
      case Type::Number: {
        char buf[40];
        const bool whole = number > -1e15 && number < 1e15 && number == static_cast<double>(static_cast<long long>(number));
        if (whole) std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(number));
        else std::snprintf(buf, sizeof buf, "%.17g", number);
        out += buf;
        break;
      }
      case Type::String: escape(out, text); break;
      case Type::Array:
        out.push_back('[');
        for (size_t i = 0; i < items.size(); ++i) {
          if (i) out.push_back(',');
          items[i].write(out);
        }
        out.push_back(']');
        break;
      case Type::Object:
        out.push_back('{');
        for (size_t i = 0; i < items.size(); ++i) {
          if (i) out.push_back(',');
          escape(out, keys[i]);
          out.push_back(':');
          items[i].write(out);
        }
        out.push_back('}');
        break;
    }
  }
};

class Parser {
 public:
  explicit Parser(const std::string& source) : s_(source) {}

  Value run() {
    Value v = value(0);
    skip();
    if (i_ != s_.size()) fail("trailing characters");
    return v;
  }

 private:
  const std::string& s_;
  size_t i_ = 0;

  [[noreturn]] void fail(const std::string& message) const {
    throw ParseError(message + " at offset " + std::to_string(i_));
  }

  void skip() {
    while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r')) ++i_;
  }

  void literal(const char* word) {
    const size_t n = std::strlen(word);
    if (s_.compare(i_, n, word) != 0) fail("invalid literal");
    i_ += n;
  }

  Value value(int depth) {
    if (depth > 256) fail("nesting too deep");
    skip();
    if (i_ >= s_.size()) fail("unexpected end of input");
    switch (s_[i_]) {
      case '{': return object(depth);
      case '[': return array(depth);
      case '"': return Value(string());
      case 't': literal("true"); return Value(true);
      case 'f': literal("false"); return Value(false);
      case 'n': literal("null"); return Value();
      default: return number();
    }
  }

  Value number() {
    const size_t start = i_;
    while (i_ < s_.size() && (std::isdigit(static_cast<unsigned char>(s_[i_])) || std::strchr("+-.eE", s_[i_]))) ++i_;
    if (start == i_) fail("unexpected character");
    const std::string token = s_.substr(start, i_ - start);
    char* end = nullptr;
    const double d = std::strtod(token.c_str(), &end);
    if (end == token.c_str() || *end != '\0') fail("invalid number");
    return Value(d);
  }

  uint32_t hex4() {
    if (i_ + 4 > s_.size()) fail("bad unicode escape");
    uint32_t cp = 0;
    for (int k = 0; k < 4; ++k) {
      const char c = s_[i_++];
      cp <<= 4;
      if (c >= '0' && c <= '9') cp |= static_cast<uint32_t>(c - '0');
      else if (c >= 'a' && c <= 'f') cp |= static_cast<uint32_t>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') cp |= static_cast<uint32_t>(c - 'A' + 10);
      else fail("bad unicode escape");
    }
    return cp;
  }

  static void appendUtf8(std::string& out, uint32_t cp) {
    if (cp >= 0xD800 && cp <= 0xDFFF) cp = 0xFFFD;
    if (cp < 0x80) {
      out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
      out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }

  std::string string() {
    ++i_;
    std::string out;
    for (;;) {
      if (i_ >= s_.size()) fail("unterminated string");
      const char c = s_[i_++];
      if (c == '"') return out;
      if (c != '\\') {
        out.push_back(c);
        continue;
      }
      if (i_ >= s_.size()) fail("bad escape");
      switch (s_[i_++]) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'u': {
          uint32_t cp = hex4();
          if (cp >= 0xD800 && cp < 0xDC00 && s_.compare(i_, 2, "\\u") == 0) {
            i_ += 2;
            const uint32_t low = hex4();
            if (low < 0xDC00 || low > 0xDFFF) fail("bad surrogate pair");
            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
          }
          appendUtf8(out, cp);
          break;
        }
        default: fail("bad escape");
      }
    }
  }

  Value array(int depth) {
    ++i_;
    Value v = Value::array();
    skip();
    if (i_ < s_.size() && s_[i_] == ']') {
      ++i_;
      return v;
    }
    for (;;) {
      v.push(value(depth + 1));
      skip();
      if (i_ >= s_.size()) fail("unterminated array");
      if (s_[i_] == ',') {
        ++i_;
        continue;
      }
      if (s_[i_] == ']') {
        ++i_;
        return v;
      }
      fail("expected , or ]");
    }
  }

  Value object(int depth) {
    ++i_;
    Value v = Value::object();
    skip();
    if (i_ < s_.size() && s_[i_] == '}') {
      ++i_;
      return v;
    }
    for (;;) {
      skip();
      if (i_ >= s_.size() || s_[i_] != '"') fail("expected string key");
      std::string key = string();
      skip();
      if (i_ >= s_.size() || s_[i_] != ':') fail("expected :");
      ++i_;
      v.set(key, value(depth + 1));
      skip();
      if (i_ >= s_.size()) fail("unterminated object");
      if (s_[i_] == ',') {
        ++i_;
        continue;
      }
      if (s_[i_] == '}') {
        ++i_;
        return v;
      }
      fail("expected , or }");
    }
  }
};

inline Value Value::parse(const std::string& source) { return Parser(source).run(); }

}  // namespace json
