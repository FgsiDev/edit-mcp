#pragma once

#include <cmath>
#include <functional>
#include <initializer_list>
#include <map>
#include <string>
#include <vector>

#include "core.hpp"
#include "format.hpp"

namespace tools {

struct Ctx {
  std::string root;
};

struct Result {
  std::string text;
  bool isError = false;
};

struct UnknownTool : std::runtime_error {
  using std::runtime_error::runtime_error;
};

struct Tool {
  std::string name;
  std::string description;
  json::Value schema;
  bool readOnly = false;
  std::function<Result(const json::Value&, const Ctx&)> run;
};

inline std::optional<std::string> optStr(const json::Value& v, const char* key) {
  const json::Value* f = v.find(key);
  if (!f || f->isNull()) return std::nullopt;
  if (!f->isString()) edit::fail(std::string(key) + " must be a string");
  return f->text;
}

inline std::optional<long> optLong(const json::Value& v, const char* key) {
  const json::Value* f = v.find(key);
  if (!f || f->isNull()) return std::nullopt;
  if (f->isNumber() && f->number == std::floor(f->number)) return static_cast<long>(f->number);
  if (f->isString() && !f->text.empty() && f->text.find_first_not_of("0123456789") == std::string::npos) {
    return std::stol(f->text);
  }
  edit::fail(std::string(key) + " must be an integer");
}

inline std::optional<bool> optBool(const json::Value& v, const char* key) {
  const json::Value* f = v.find(key);
  if (!f || f->isNull()) return std::nullopt;
  if (!f->isBool()) edit::fail(std::string(key) + " must be a boolean");
  return f->boolean;
}

inline std::string reqStr(const json::Value& v, const char* key) {
  const auto s = optStr(v, key);
  if (!s) edit::fail(std::string(key) + " is required");
  return *s;
}

inline edit::Edit editFrom(const json::Value& v, const std::string& forcedType = "") {
  if (!v.isObject()) edit::fail("edit must be an object");
  edit::Edit e;
  e.type = forcedType.empty() ? optStr(v, "type").value_or("") : forcedType;
  e.search = optStr(v, "search");
  e.replace = optStr(v, "replace");
  e.content = optStr(v, "content");
  e.flags = optStr(v, "flags");
  e.regex = optBool(v, "regex").value_or(false);
  e.ignoreCase = optBool(v, "ignoreCase").value_or(false);
  e.keepIndent = optBool(v, "keepIndent").value_or(true);
  e.raw = optBool(v, "raw").value_or(false);
  e.remove = optBool(v, "remove").value_or(false);
  e.all = optBool(v, "all");
  e.occurrence = optLong(v, "occurrence");
  e.startLine = optLong(v, "startLine");
  e.endLine = optLong(v, "endLine");
  e.line = optLong(v, "line");
  e.from = optLong(v, "from");
  e.to = optLong(v, "to");
  e.length = optLong(v, "length");
  e.limit = optLong(v, "limit");
  e.context = optLong(v, "context");
  return e;
}

inline std::string readOne(const edit::Doc& d, const edit::Edit& e) {
  const std::string type =
      !e.type.empty() ? e.type : std::string(e.search ? "content" : (e.line ? "range" : "lines"));
  if (type == "content") return edit::formatContent(d, edit::contentView(d, e));
  if (type == "lines") return edit::formatLines(d.input, edit::linesView(d, e), e.raw);
  if (type == "range") return edit::formatRange(d.input, edit::rangeView(d, e));
  edit::fail("unknown read type " + json::Value(type).dump() + " (use content, lines or range)");
}

inline Result runReplace(const std::string& type, const json::Value& args, const Ctx& ctx, bool remove = false) {
  const std::string path = reqStr(args, "path");
  edit::Edit e = editFrom(args, type);
  if (remove) e.remove = true;
  const bool dry = optBool(args, "dryRun").value_or(false);
  return {edit::formatEdit(edit::editFile(path, {e}, ctx.root, dry)), false};
}

inline Result runRead(const std::string& type, const json::Value& args, const Ctx& ctx) {
  const std::string path = reqStr(args, "path");
  const edit::Doc d = edit::readDoc(path, ctx.root);
  return {readOne(d, editFrom(args, type)), false};
}

inline Result multiReplace(const json::Value& args, const Ctx& ctx) {
  const json::Value* list = args.find("edits");
  if (!list || !list->isArray() || list->items.empty()) edit::fail("edits must be a non-empty array");
  const bool dry = optBool(args, "dryRun").value_or(false);

  struct Group {
    std::string path;
    std::vector<edit::Edit> edits;
    std::vector<int> labels;
  };
  std::vector<Group> groups;
  std::map<std::string, size_t> index;

  for (size_t i = 0; i < list->items.size(); ++i) {
    const json::Value& item = list->items[i];
    const std::string tag = "edit #" + std::to_string(i + 1) + ": ";
    try {
      const std::string path = reqStr(item, "path");
      const std::string key = edit::absoluteKey(path, ctx.root).string();
      auto it = index.find(key);
      if (it == index.end()) {
        it = index.emplace(key, groups.size()).first;
        groups.push_back({path, {}, {}});
      }
      groups[it->second].edits.push_back(editFrom(item));
      groups[it->second].labels.push_back(static_cast<int>(i));
    } catch (const edit::EditError& err) {
      edit::fail(tag + err.what());
    }
  }

  std::vector<std::string> parts;
  bool failed = false;
  for (const Group& g : groups) {
    try {
      parts.push_back(edit::formatEdit(edit::editFile(g.path, g.edits, ctx.root, dry, &g.labels)));
    } catch (const std::exception& err) {
      failed = true;
      parts.push_back(g.path + ": error — " + err.what());
    }
  }
  return {edit::join(parts, "\n\n"), failed};
}

inline Result multiGet(const json::Value& args, const Ctx& ctx) {
  const json::Value* list = args.find("reads");
  if (!list || !list->isArray() || list->items.empty()) edit::fail("reads must be a non-empty array");

  std::map<std::string, edit::Doc> docs;
  std::map<std::string, std::string> failures;
  const auto load = [&](const std::string& path) -> const edit::Doc& {
    const std::string key = edit::absoluteKey(path, ctx.root).string();
    if (const auto f = failures.find(key); f != failures.end()) edit::fail(f->second);
    auto it = docs.find(key);
    if (it == docs.end()) {
      try {
        it = docs.emplace(key, edit::readDoc(path, ctx.root)).first;
      } catch (const edit::EditError& err) {
        failures[key] = err.what();
        throw;
      }
    }
    return it->second;
  };

  std::vector<std::string> parts;
  size_t failed = 0;
  for (size_t i = 0; i < list->items.size(); ++i) {
    const json::Value& item = list->items[i];
    const std::string tag = "#" + std::to_string(i + 1) + " ";
    try {
      const std::string path = reqStr(item, "path");
      edit::Edit e = editFrom(item);
      e.raw = false;
      parts.push_back(tag + readOne(load(path), e));
    } catch (const std::exception& err) {
      ++failed;
      parts.push_back(tag + "error — " + err.what());
    }
  }
  return {edit::join(parts, "\n\n"), failed == list->items.size()};
}

namespace schema {

inline constexpr const char* kPath = R"J("path":{"type":"string","description":"File path: relative to the server root (default: current directory), absolute, or ~/..."})J";
inline constexpr const char* kDry = R"J("dryRun":{"type":"boolean","description":"Preview the diff without writing the file"})J";
inline constexpr const char* kSearch = R"J("search":{"type":"string","description":"Text to find. Literal unless regex=true"})J";
inline constexpr const char* kReplace = R"J("replace":{"type":"string","description":"Replacement text. In regex mode $1, $2 and $& work"})J";
inline constexpr const char* kRegex = R"J("regex":{"type":"boolean","description":"Treat search as a regular expression (ECMAScript syntax, matched line by line)"})J";
inline constexpr const char* kFlags = R"J("flags":{"type":"string","description":"Regex flags: i = ignore case, s = let the pattern match across lines (whole file at once)"})J";
inline constexpr const char* kIgnoreCase = R"J("ignoreCase":{"type":"boolean","description":"Case-insensitive match"})J";
inline constexpr const char* kAll = R"J("all":{"type":"boolean","description":"Replace every match (default true). false replaces only the first"})J";
inline constexpr const char* kOccurrence = R"J("occurrence":{"type":"integer","minimum":1,"description":"Replace only the Nth match (1-based)"})J";
inline constexpr const char* kContext = R"J("context":{"type":"integer","minimum":0,"description":"Lines of context around each match (default 0)"})J";
inline constexpr const char* kLimit = R"J("limit":{"type":"integer","minimum":1,"description":"Maximum matches to return (default 50)"})J";
inline constexpr const char* kStartLine = R"J("startLine":{"type":"integer","minimum":1,"description":"First line (1-based)"})J";
inline constexpr const char* kStartLineOpt = R"J("startLine":{"type":"integer","minimum":1,"description":"First line (1-based). Omit both startLine and endLine for the whole file"})J";
inline constexpr const char* kEndLine = R"J("endLine":{"type":"integer","minimum":1,"description":"Last line, inclusive. Default: startLine"})J";
inline constexpr const char* kContentLines = R"J("content":{"type":"string","description":"New text, may span several lines. The indentation of the first replaced line is applied automatically, so write it without leading spaces (relative indentation inside multi-line content is kept)"})J";
inline constexpr const char* kKeepIndent = R"J("keepIndent":{"type":"boolean","description":"Default true. false = content replaces the lines exactly as written, leading whitespace included"})J";
inline constexpr const char* kRemove = R"J("remove":{"type":"boolean","description":"lines edits only: delete the lines (newline included) instead of replacing them; content is not needed"})J";
inline constexpr const char* kRaw = R"J("raw":{"type":"boolean","description":"Return plain text without line numbers"})J";
inline constexpr const char* kLine = R"J("line":{"type":"integer","minimum":1,"description":"Start line (1-based)"})J";
inline constexpr const char* kFrom = R"J("from":{"type":"integer","minimum":1,"description":"Start column (1-based, counted in characters)"})J";
inline constexpr const char* kTo = R"J("to":{"type":"integer","minimum":0,"description":"End column, inclusive. Without endLine it counts from the start of line and may run past the line end into the next lines (a newline counts as 1 char)"})J";
inline constexpr const char* kEndLineRange = R"J("endLine":{"type":"integer","minimum":1,"description":"Line that `to` refers to. Default: to = end of that line"})J";
inline constexpr const char* kLength = R"J("length":{"type":"integer","minimum":0,"description":"Characters to replace, instead of `to`. 0 = insert. Default when nothing is given: 1"})J";
inline constexpr const char* kContentRange = R"J("content":{"type":"string","description":"Replacement text, inserted exactly as written"})J";
inline constexpr const char* kTypeEdit = R"J("type":{"type":"string","enum":["content","lines","range"],"description":"content = search/replace, lines = by line number, range = by line + column"})J";
inline constexpr const char* kEndLineMulti = R"J("endLine":{"type":"integer","minimum":1,"description":"lines: last line (inclusive). range: the line `to` refers to"})J";
inline constexpr const char* kContentMulti = R"J("content":{"type":"string","description":"New text for lines/range edits (lines edits inherit the indentation of the replaced line)"})J";

inline std::string props(std::initializer_list<const char*> parts) {
  std::string out;
  for (const char* p : parts) {
    if (!out.empty()) out.push_back(',');
    out += p;
  }
  return out;
}

inline json::Value object(std::initializer_list<const char*> parts, const std::string& required) {
  return json::Value::parse("{\"type\":\"object\",\"properties\":{" + props(parts) + "},\"required\":[" + required + "]}");
}

inline std::string item(const std::string& properties, const std::string& required) {
  return "{\"type\":\"object\",\"properties\":{" + properties + "},\"required\":[" + required + "]}";
}

inline json::Value editsSchema() {
  const std::string properties =
      props({kPath, kTypeEdit, kSearch, kReplace, kRegex, kFlags, kIgnoreCase, kAll, kOccurrence, kStartLine,
             kEndLineMulti, kContentMulti, kKeepIndent, kRemove, kLine, kFrom, kTo, kLength});
  const std::string edits = std::string("\"edits\":{\"type\":\"array\",\"minItems\":1,\"description\":\"Edits to apply. ") +
                            "type=content needs search+replace; type=lines needs startLine (+endLine) and content, or remove:true to delete those lines; " +
                            "type=range needs line+from (+to | length | endLine+to) and content\",\"items\":" +
                            item(properties, "\"path\",\"type\"") + "}";
  return json::Value::parse("{\"type\":\"object\",\"properties\":{" + edits + "," + kDry + "},\"required\":[\"edits\"]}");
}

inline json::Value readsSchema() {
  const std::string properties =
      props({kPath, kTypeEdit, kSearch, kRegex, kFlags, kIgnoreCase, kContext, kLimit, kStartLine, kEndLineMulti,
             kLine, kFrom, kTo, kLength});
  const std::string reads = std::string("\"reads\":{\"type\":\"array\",\"minItems\":1,\"description\":\"Reads to run. ") +
                            "type=content needs search; type=lines takes startLine/endLine (whole file when omitted); " +
                            "type=range needs line+from (+to | length | endLine+to)\",\"items\":" +
                            item(properties, "\"path\"") + "}";
  return json::Value::parse("{\"type\":\"object\",\"properties\":{" + reads + "},\"required\":[\"reads\"]}");
}

}  // namespace schema

inline std::vector<Tool> buildTools() {
  using namespace schema;
  std::vector<Tool> list;
  const auto add = [&](std::string name, std::string description, json::Value inputSchema, bool readOnly,
                       std::function<Result(const json::Value&, const Ctx&)> run) {
    list.push_back({std::move(name), std::move(description), std::move(inputSchema), readOnly, std::move(run)});
  };

  add("replace_content",
      "Find and replace text in a file, like a normal search/replace (e.g. search `ee`, replace `path`). "
      "Literal by default; regex=true enables regex with $1 captures. Replaces every match unless all=false or occurrence=N.",
      object({kPath, kSearch, kReplace, kRegex, kFlags, kIgnoreCase, kAll, kOccurrence, kDry},
             "\"path\",\"search\",\"replace\""),
      false, [](const json::Value& a, const Ctx& c) { return runReplace("content", a, c); });

  add("replace_lines",
      "Replace one line (startLine) or a block (startLine..endLine) with new content. Multi-line content is fine. "
      "The indentation of the replaced line is applied automatically, so pass content without leading spaces. "
      "Set keepIndent=false to replace the leading whitespace too.",
      object({kPath, kStartLine, kEndLine, kContentLines, kKeepIndent, kDry}, "\"path\",\"startLine\",\"content\""),
      false, [](const json::Value& a, const Ctx& c) { return runReplace("lines", a, c); });

  add("delete_lines",
      "Delete line startLine, or the block startLine..endLine, newline included (the lines disappear, no blank line is left). "
      "Line numbers of later lines shift up; use multi_replace with remove:true to delete several blocks against the original numbering.",
      object({kPath, kStartLine, kEndLine, kDry}, "\"path\",\"startLine\""), false,
      [](const json::Value& a, const Ctx& c) { return runReplace("lines", a, c, true); });

  add("replace_range",
      "Replace a character range. Start is line + from (1-based column). End is `to` (inclusive column), or `length`, "
      "or endLine + to. Without endLine, `to` counts from the start of `line` and may run past the end of the line "
      "into the following lines (a newline counts as 1 char). length=0 inserts. Content goes in exactly as written.",
      object({kPath, kLine, kFrom, kTo, kEndLineRange, kLength, kContentRange, kDry},
             "\"path\",\"line\",\"from\",\"content\""),
      false, [](const json::Value& a, const Ctx& c) { return runReplace("range", a, c); });

  add("multi_replace",
      "Apply many edits (content, lines and range mixed) across one or more files in one call. Every line and column "
      "refers to the file as it was before the call, so edits never shift each other. Each file is all-or-nothing; "
      "overlapping edits are rejected. Use dryRun to preview.",
      editsSchema(), false, [](const json::Value& a, const Ctx& c) { return multiReplace(a, c); });

  add("get_content",
      "Find text and return every match with line:col, captures and surrounding lines (context). "
      "Read-only counterpart of replace_content.",
      object({kPath, kSearch, kRegex, kFlags, kIgnoreCase, kContext, kLimit}, "\"path\",\"search\""), true,
      [](const json::Value& a, const Ctx& c) { return runRead("content", a, c); });

  add("get_lines",
      "Read lines startLine..endLine with line numbers; the whole file when both are omitted. "
      "raw=true returns plain text without numbers.",
      object({kPath, kStartLineOpt, kEndLine, kRaw}, "\"path\""), true,
      [](const json::Value& a, const Ctx& c) { return runRead("lines", a, c); });

  add("get_range",
      "Read an exact character range (same addressing as replace_range) and return its text with line:col coordinates.",
      object({kPath, kLine, kFrom, kTo, kEndLineRange, kLength}, "\"path\",\"line\",\"from\""), true,
      [](const json::Value& a, const Ctx& c) { return runRead("range", a, c); });

  add("multi_get", "Run many reads (content, lines, range) across one or more files in one call.", readsSchema(), true,
      [](const json::Value& a, const Ctx& c) { return multiGet(a, c); });

  return list;
}

inline const std::vector<Tool>& all() {
  static const std::vector<Tool> list = buildTools();
  return list;
}

inline Result execute(const std::string& name, const json::Value& args, const Ctx& ctx) {
  for (const Tool& tool : all()) {
    if (tool.name != name) continue;
    const json::Value empty = json::Value::object();
    const json::Value& input = args.isObject() ? args : empty;
    try {
      return tool.run(input, ctx);
    } catch (const edit::EditError& err) {
      return {err.what(), true};
    } catch (const std::exception& err) {
      return {std::string("unexpected error: ") + err.what(), true};
    }
  }
  throw UnknownTool("Unknown tool: " + name);
}

}  // namespace tools
