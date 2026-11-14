#pragma once

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <io.h>
#define EDIT_ISATTY(fd) _isatty(fd)
#else
#include <unistd.h>
#define EDIT_ISATTY(fd) isatty(fd)
#endif

#include "meta.hpp"
#include "server.hpp"
#include "skill.hpp"
#include "tools.hpp"

namespace cli {

struct Args {
  std::vector<std::string> pos;
  std::map<std::string, std::string> opts;
  std::set<std::string> flags;
};

struct UsageError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

inline const std::set<std::string>& boolFlags() {
  static const std::set<std::string> f = {"regex", "ignore-case", "first", "dry-run", "no-keep-indent",
                                          "raw",   "escapes",     "help",  "version", "stdin", "project"};
  return f;
}

inline const std::map<std::string, std::string>& shortFlags() {
  static const std::map<std::string, std::string> m = {{"r", "regex"}, {"i", "ignore-case"}, {"n", "dry-run"},
                                                       {"e", "escapes"}, {"h", "help"},        {"v", "version"}};
  return m;
}

inline Args parse(const std::vector<std::string>& argv) {
  Args a;
  bool literal = false;
  for (size_t i = 0; i < argv.size(); ++i) {
    const std::string& s = argv[i];
    if (literal || s == "-" || s.size() < 2 || s[0] != '-') {
      a.pos.push_back(s);
      continue;
    }
    if (s == "--") {
      literal = true;
      continue;
    }
    std::string name;
    std::optional<std::string> inlineValue;
    if (s[1] == '-') {
      name = s.substr(2);
      if (const size_t eq = name.find('='); eq != std::string::npos) {
        inlineValue = name.substr(eq + 1);
        name.resize(eq);
      }
    } else {
      const auto it = shortFlags().find(s.substr(1));
      if (it == shortFlags().end()) throw UsageError("unknown option " + s);
      name = it->second;
    }
    if (boolFlags().count(name)) {
      a.flags.insert(name);
      continue;
    }
    if (inlineValue) {
      a.opts[name] = *inlineValue;
    } else if (i + 1 < argv.size()) {
      a.opts[name] = argv[++i];
    } else {
      throw UsageError("option --" + name + " needs a value");
    }
  }
  return a;
}

inline std::string readStream(std::istream& in) {
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

inline std::string readFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw UsageError("cannot read " + path);
  return readStream(in);
}

inline std::string unescape(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] != '\\' || i + 1 >= s.size()) {
      out.push_back(s[i]);
      continue;
    }
    switch (s[++i]) {
      case 'n': out.push_back('\n'); break;
      case 't': out.push_back('\t'); break;
      case 'r': out.push_back('\r'); break;
      case '\\': out.push_back('\\'); break;
      default:
        out.push_back('\\');
        out.push_back(s[i]);
    }
  }
  return out;
}

inline std::string textArg(const Args& a, size_t index, const char* name, bool fromStdin = false) {
  std::string s;
  if (fromStdin && a.flags.count("stdin")) {
    s = readStream(std::cin);
    if (!s.empty() && s.back() == '\n') s.pop_back();
  } else {
    if (index >= a.pos.size()) throw UsageError(std::string("missing ") + name);
    s = a.pos[index];
  }
  return a.flags.count("escapes") ? unescape(s) : s;
}

inline long number(const std::string& s, const std::string& what) {
  if (s.empty() || s.find_first_not_of("0123456789") != std::string::npos) {
    throw UsageError("invalid " + what + ": " + s);
  }
  return std::stol(s);
}

inline void parseLines(const std::string& spec, json::Value& args, bool allowEmpty) {
  if (spec.empty() && allowEmpty) return;
  const size_t dash = spec.find('-');
  args.set("startLine", number(spec.substr(0, dash), "line"));
  if (dash != std::string::npos) args.set("endLine", number(spec.substr(dash + 1), "line"));
}

inline void parseRange(const std::string& spec, json::Value& args) {
  const size_t colon = spec.find(':');
  if (colon == std::string::npos) throw UsageError("range must look like LINE:FROM, LINE:FROM-TO or LINE:FROM-ENDLINE:TO");
  args.set("line", number(spec.substr(0, colon), "line"));
  const std::string rest = spec.substr(colon + 1);
  const size_t dash = rest.find('-');
  args.set("from", number(rest.substr(0, dash), "column"));
  if (dash == std::string::npos) return;
  const std::string tail = rest.substr(dash + 1);
  if (const size_t c2 = tail.find(':'); c2 != std::string::npos) {
    args.set("endLine", number(tail.substr(0, c2), "end line"));
    args.set("to", number(tail.substr(c2 + 1), "column"));
  } else {
    args.set("to", number(tail, "column"));
  }
}

inline void applyOptions(const Args& a, json::Value& args) {
  for (const char* key : {"flags", "occurrence", "context", "limit", "length", "to", "from"}) {
    const auto it = a.opts.find(key);
    if (it == a.opts.end()) continue;
    const bool numeric = std::string(key) != "flags";
    args.set(key, numeric ? json::Value(number(it->second, key)) : json::Value(it->second));
  }
  if (const auto it = a.opts.find("end-line"); it != a.opts.end()) args.set("endLine", number(it->second, "end-line"));
  if (a.flags.count("regex")) args.set("regex", true);
  if (a.flags.count("ignore-case")) args.set("ignoreCase", true);
  if (a.flags.count("first")) args.set("all", false);
  if (a.flags.count("dry-run")) args.set("dryRun", true);
  if (a.flags.count("no-keep-indent")) args.set("keepIndent", false);
  if (a.flags.count("raw")) args.set("raw", true);
}

inline json::Value withBatch(const Args& a, const char* key) {
  if (a.pos.size() < 2) throw UsageError("missing JSON file (use - for stdin)");
  const std::string source = a.pos[1] == "-" ? readStream(std::cin) : readFile(a.pos[1]);
  json::Value parsed = json::Value::parse(source);
  json::Value args = json::Value::object();
  if (parsed.isArray()) {
    args.set(key, std::move(parsed));
  } else if (parsed.isObject()) {
    args = std::move(parsed);
  } else {
    throw UsageError(std::string("JSON must be an array or an object with \"") + key + "\"");
  }
  if (a.flags.count("dry-run")) args.set("dryRun", true);
  return args;
}

inline json::Value buildArgs(const std::string& tool, const Args& a) {
  if (tool == "multi_replace") return withBatch(a, "edits");
  if (tool == "multi_get") return withBatch(a, "reads");

  if (a.pos.size() < 2) throw UsageError("missing file path");
  json::Value args = json::Value::object();
  args.set("path", a.pos[1]);

  if (tool == "replace_content") {
    args.set("search", textArg(a, 2, "SEARCH"));
    args.set("replace", textArg(a, 3, "REPLACE", true));
  } else if (tool == "replace_lines") {
    if (a.pos.size() < 3) throw UsageError("missing LINE or START-END");
    parseLines(a.pos[2], args, false);
    args.set("content", textArg(a, 3, "CONTENT", true));
  } else if (tool == "delete_lines") {
    if (a.pos.size() < 3) throw UsageError("missing LINE or START-END");
    parseLines(a.pos[2], args, false);
  } else if (tool == "replace_range") {
    if (a.pos.size() < 3) throw UsageError("missing LINE:FROM[-TO]");
    parseRange(a.pos[2], args);
    args.set("content", textArg(a, 3, "CONTENT", true));
  } else if (tool == "get_content") {
    args.set("search", textArg(a, 2, "SEARCH"));
  } else if (tool == "get_lines") {
    if (a.pos.size() > 2) parseLines(a.pos[2], args, true);
  } else if (tool == "get_range") {
    if (a.pos.size() < 3) throw UsageError("missing LINE:FROM[-TO]");
    parseRange(a.pos[2], args);
  }
  applyOptions(a, args);
  return args;
}

inline std::string toolName(std::string command) {
  for (char& c : command) {
    if (c == '-') c = '_';
  }
  return command;
}

inline std::string selfPath(const char* argv0) {
  std::error_code ec;
  std::filesystem::path p = std::filesystem::read_symlink("/proc/self/exe", ec);
  if (ec) p = std::filesystem::canonical(argv0, ec);
  if (ec) p = std::filesystem::absolute(argv0, ec);
  return p.string();
}

inline std::string configFor(const std::string& agent, const std::string& binary, const std::string& name,
                             const std::string& root) {
  const auto q = [](const std::string& s) { return json::Value(s).dump(); };
  if (agent == "opencode") {
    std::string command = "          " + q(binary);
    if (!root.empty()) command += ",\n          \"--root\",\n          " + q(root);
    return "{\n  \"mcp\": {\n    \"servers\": {\n      " + q(name) + ": {\n        \"type\": \"local\",\n" +
           "        \"command\": [\n" + command + "\n        ]\n      }\n    }\n  }\n}";
  }
  if (agent == "claude" || agent == "mcpservers") {
    const std::string args = root.empty() ? "[]" : "[\"--root\", " + q(root) + "]";
    return "{\n  \"mcpServers\": {\n    " + q(name) + ": {\n      \"command\": " + q(binary) + ",\n      \"args\": " +
           args + "\n    }\n  }\n}";
  }
  if (agent == "codex") {
    std::string out = "[mcp_servers." + name + "]\ncommand = " + q(binary) + "\n";
    if (!root.empty()) out += "args = [\"--root\", " + q(root) + "]\n";
    return out;
  }
  throw UsageError("unknown --agent " + agent + " (opencode, claude, codex)");
}

inline std::filesystem::path homeDir() {
  const char* home = std::getenv("HOME");
#ifdef _WIN32
  if (!home) home = std::getenv("USERPROFILE");
#endif
  if (!home) throw UsageError("HOME is not set");
  return home;
}

inline int installSkill(const Args& a) {
  namespace fs = std::filesystem;
  const auto opt = [&](const char* key, const char* fallback) {
    const auto it = a.opts.find(key);
    return it == a.opts.end() ? std::string(fallback) : it->second;
  };
  std::vector<fs::path> dirs;
  if (a.opts.count("dir")) {
    dirs.push_back(edit::expandHome(a.opts.at("dir")) / skill::kName);
  } else {
    const std::string target = opt("target", "all");
    const bool claude = target == "all" || target == "claude";
    const bool agents = target == "all" || target == "agents" || target == "opencode" || target == "pi" || target == "codex";
    if (!claude && !agents) throw UsageError("unknown --target " + target + " (all, claude, agents)");
    const fs::path base = a.flags.count("project") ? fs::current_path() : homeDir();
    if (claude) dirs.push_back(base / ".claude" / "skills" / skill::kName);
    if (agents) dirs.push_back(base / ".agents" / "skills" / skill::kName);
  }
  for (const fs::path& dir : dirs) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) throw UsageError("cannot create " + dir.string() + ": " + ec.message());
    std::ofstream out(dir / "SKILL.md", std::ios::binary | std::ios::trunc);
    out << skill::kText;
    if (!out) throw UsageError("cannot write " + (dir / "SKILL.md").string());
    std::cout << "installed: " << (dir / "SKILL.md").string() << '\n';
  }
  return 0;
}

inline std::string help() {
  return std::string(meta::kTitle) + " v" + meta::kVersion +
         " — local MCP file editor (replace by content, line, position; batch; read)\n"
         "\n"
         "usage:\n"
         "  edit-mcp [serve] [--root DIR]              run as MCP server over stdio (default)\n"
         "  edit-mcp --help                            this help\n"
         "  edit-mcp config [--agent A] [--name N] [--root DIR]\n"
         "                                             MCP config for this binary (A: opencode | claude | codex)\n"
         "  edit-mcp install-skill [--target T] [--project] [--dir D]\n"
         "                                             install the agent skill (T: all | claude | agents)\n"
         "  edit-mcp skill                             print the skill (SKILL.md)\n"
         "  edit-mcp tools                             list tools\n"
         "  edit-mcp <command> FILE ARGS [options]     run a tool directly\n"
         "\n"
         "commands:\n"
         "  replace-content FILE SEARCH REPLACE        text replace (all matches)\n"
         "  replace-lines   FILE LINE|START-END TEXT   replace whole lines, indent kept automatically\n"
         "  delete-lines    FILE LINE|START-END        delete lines (newline included)\n"
         "  replace-range   FILE L:C[-TO|-EL:TO] TEXT  replace by line:column (may cross lines)\n"
         "  multi-replace   JSON                       batch of edits, JSON file or - for stdin\n"
         "  get-content     FILE SEARCH                find with line:col and context\n"
         "  get-lines       FILE [LINE|START-END]      read lines\n"
         "  get-range       FILE L:C[-TO|-EL:TO]       read a character range\n"
         "  multi-get       JSON                       batch of reads\n"
         "\n"
         "options:\n"
         "  -r, --regex          SEARCH is a regex ($1, $& in REPLACE)\n"
         "  -i, --ignore-case    case-insensitive search\n"
         "      --first          replace only the first match\n"
         "      --occurrence N   replace only the Nth match\n"
         "      --flags s        regex across lines (whole file at once)\n"
         "      --length N       range: replace N chars (0 = insert)\n"
         "      --no-keep-indent lines: do not inherit the indentation\n"
         "      --context N      get-content: lines of context\n"
         "      --raw            get-lines: no line numbers\n"
         "  -n, --dry-run        preview without writing\n"
         "  -e, --escapes        interpret \\n \\t \\\\ in text arguments\n"
         "      --root DIR       confine paths to DIR (default: current directory)\n"
         "      --stdin          read the last text argument (REPLACE / TEXT) from stdin\n"
         "\n"
         "examples:\n"
         "  edit-mcp replace-content app.js 'const ee = require(\"path\")' 'const path = require(\"path\")'\n"
         "  edit-mcp replace-lines app.js 2-3 -e 'console.log(1)\\nconsole.log(2)'\n"
         "  edit-mcp replace-range app.js 1:5-8 renamed\n"
         "\n" +
         meta::credit() + "\n";
}

inline int run(int argc, char** argv) {
  std::vector<std::string> raw(argv + 1, argv + argc);
  try {
    Args a = parse(raw);
    tools::Ctx ctx;
    if (const auto it = a.opts.find("root"); it != a.opts.end()) {
      std::error_code ec;
      ctx.root = std::filesystem::absolute(it->second, ec).lexically_normal().string();
      const bool configOnly = !a.pos.empty() && a.pos[0] == "config";
      if (!configOnly && !std::filesystem::is_directory(ctx.root, ec)) {
        throw UsageError("root is not a directory: " + it->second);
      }
    }

    if (a.flags.count("version")) {
      std::cout << meta::credit() << '\n';
      return 0;
    }
    if (a.flags.count("help")) {
      std::cout << help();
      return 0;
    }
    if (a.pos.empty() || a.pos[0] == "serve") {
      if (EDIT_ISATTY(0)) std::cerr << "edit-mcp: MCP server on stdio (Ctrl+D to quit, --help for usage)\n";
      return server::serve(ctx);
    }

    const std::string& command = a.pos[0];
    if (command == "help") {
      std::cout << help();
      return 0;
    }
    if (command == "tools") {
      for (const tools::Tool& t : tools::all()) std::cout << t.name << "\n  " << t.description << "\n";
      return 0;
    }
    if (command == "config") {
      const auto name = a.opts.find("name");
      const auto agent = a.opts.find("agent");
      std::cout << configFor(agent == a.opts.end() ? "opencode" : agent->second, selfPath(argv[0]),
                             name == a.opts.end() ? "edit" : name->second,
                             a.opts.count("root") ? ctx.root : std::string())
                << '\n';
      return 0;
    }
    if (command == "skill") {
      std::cout << skill::kText;
      return 0;
    }
    if (command == "install-skill") return installSkill(a);

    const std::string tool = toolName(command);
    const auto& known = tools::all();
    const bool exists = std::any_of(known.begin(), known.end(), [&](const tools::Tool& t) { return t.name == tool; });
    if (!exists) throw UsageError("unknown command: " + command + " (try --help)");

    const tools::Result result = tools::execute(tool, buildArgs(tool, a), ctx);
    (result.isError ? std::cerr : std::cout) << result.text << '\n';
    return result.isError ? 1 : 0;
  } catch (const UsageError& err) {
    std::cerr << "error: " << err.what() << '\n';
    return 2;
  } catch (const std::exception& err) {
    std::cerr << "error: " << err.what() << '\n';
    return 1;
  }
}

}  // namespace cli
