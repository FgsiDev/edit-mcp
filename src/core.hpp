#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "json.hpp"

namespace edit {

namespace fs = std::filesystem;
using std::size_t;
using std::string;
using std::vector;

struct EditError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

[[noreturn]] inline void fail(const string& message) { throw EditError(message); }

struct Edit {
  string type;
  std::optional<string> search, replace, content, flags;
  bool regex = false;
  bool ignoreCase = false;
  bool keepIndent = true;
  bool raw = false;
  bool remove = false;
  std::optional<bool> all;
  std::optional<long> occurrence, startLine, endLine, line, from, to, length, limit, context;
};

struct Position {
  size_t line = 1;
  size_t col = 1;
};

struct Range {
  size_t start = 0;
  size_t end = 0;
  string text;
  string kind;
  int edit = 0;
  size_t shown = 0;
  bool removal = false;
};

struct Change {
  int edit = 0;
  string kind;
  size_t startLine = 0, endLine = 0, newStartLine = 0, newEndLine = 0;
  vector<string> before, after;
};

struct EditResult {
  string input;
  bool changed = false;
  bool written = false;
  bool dryRun = false;
  vector<Change> changes;
};

struct Doc {
  string input;
  fs::path file;
  bool bom = false;
  bool crlf = false;
  string src;
  vector<size_t> starts;
  size_t count = 0;

  void index() {
    starts.assign(1, 0);
    for (size_t i = 0; i < src.size(); ++i) {
      if (src[i] == '\n') starts.push_back(i + 1);
    }
    count = (!src.empty() && src.back() == '\n') ? starts.size() - 1 : starts.size();
  }

  size_t lineEnd(size_t i) const { return i + 1 < starts.size() ? starts[i + 1] - 1 : src.size(); }

  string line(size_t n) const { return src.substr(starts[n - 1], lineEnd(n - 1) - starts[n - 1]); }
};

inline bool isContinuation(char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }

inline size_t countChars(const string& s, size_t from, size_t to) {
  size_t n = 0;
  for (size_t i = from; i < to; ++i) {
    if (!isContinuation(s[i])) ++n;
  }
  return n;
}

inline std::optional<size_t> advance(const string& s, size_t pos, size_t n) {
  while (n > 0) {
    if (pos >= s.size()) return std::nullopt;
    ++pos;
    while (pos < s.size() && isContinuation(s[pos])) ++pos;
    --n;
  }
  return pos;
}

inline string clipChars(const string& s, size_t max) {
  const auto cut = advance(s, 0, max);
  if (!cut || *cut >= s.size()) return s;
  return s.substr(0, *cut) + "…";
}

inline bool validUtf8(const string& s) {
  size_t i = 0;
  const size_t n = s.size();
  while (i < n) {
    const auto c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) {
      ++i;
      continue;
    }
    size_t len;
    uint32_t cp;
    if ((c >> 5) == 0x6) {
      len = 2;
      cp = c & 0x1F;
    } else if ((c >> 4) == 0xE) {
      len = 3;
      cp = c & 0x0F;
    } else if ((c >> 3) == 0x1E) {
      len = 4;
      cp = c & 0x07;
    } else {
      return false;
    }
    if (i + len > n) return false;
    for (size_t k = 1; k < len; ++k) {
      const auto cc = static_cast<unsigned char>(s[i + k]);
      if ((cc & 0xC0) != 0x80) return false;
      cp = (cp << 6) | (cc & 0x3F);
    }
    const bool overlong = (len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000);
    if (overlong || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
    i += len;
  }
  return true;
}

inline string replaceAll(string s, const string& from, const string& to) {
  if (from.empty()) return s;
  size_t pos = 0;
  while ((pos = s.find(from, pos)) != string::npos) {
    s.replace(pos, from.size(), to);
    pos += to.size();
  }
  return s;
}

inline size_t countOf(const string& s, const string& needle) {
  size_t n = 0;
  for (size_t pos = s.find(needle); pos != string::npos; pos = s.find(needle, pos + needle.size())) ++n;
  return n;
}

inline string normalizeText(const string& s) { return replaceAll(s, "\r\n", "\n"); }

inline vector<string> splitLines(const string& s) {
  vector<string> out;
  size_t pos = 0;
  for (;;) {
    const size_t nl = s.find('\n', pos);
    if (nl == string::npos) {
      out.push_back(s.substr(pos));
      return out;
    }
    out.push_back(s.substr(pos, nl - pos));
    pos = nl + 1;
  }
}

inline string joinLines(const vector<string>& lines) {
  string out;
  for (size_t i = 0; i < lines.size(); ++i) {
    if (i) out.push_back('\n');
    out += lines[i];
  }
  return out;
}

inline string leadingSpace(const string& line) { return line.substr(0, line.find_first_not_of(" \t") == string::npos ? line.size() : line.find_first_not_of(" \t")); }

inline bool isBlank(const string& line) { return line.find_first_not_of(" \t\r") == string::npos; }

inline string commonPrefix(const string& a, const string& b) {
  size_t i = 0;
  while (i < a.size() && i < b.size() && a[i] == b[i]) ++i;
  return a.substr(0, i);
}

inline string describeCode(const std::error_code& ec) {
  if (ec == std::errc::no_such_file_or_directory) return "file not found";
  if (ec == std::errc::permission_denied) return "permission denied";
  if (ec == std::errc::not_a_directory) return "not a directory";
  return ec.message();
}

inline fs::path expandHome(const string& input) {
  if (input == "~" || input.rfind("~/", 0) == 0) {
    const char* home = std::getenv("HOME");
#ifdef _WIN32
    if (!home) home = std::getenv("USERPROFILE");
#endif
    if (home) return fs::path(home) / (input.size() > 2 ? input.substr(2) : string());
  }
  return fs::path(input);
}

inline fs::path absoluteKey(const string& input, const string& root) {
  if (input.empty()) fail("path must be a non-empty string");
  fs::path abs = expandHome(input);
  if (abs.is_relative()) abs = (root.empty() ? fs::current_path() : fs::path(root)) / abs;
  return abs.lexically_normal();
}

inline fs::path realFile(const string& input, const string& root) {
  std::error_code ec;
  const fs::path real = fs::canonical(absoluteKey(input, root), ec);
  if (ec) fail(describeCode(ec) + ": " + input);
  if (fs::is_directory(real, ec)) fail("is a directory: " + input);
  if (!root.empty()) {
    const fs::path base = fs::weakly_canonical(fs::path(root), ec);
    const fs::path rel = real.lexically_relative(base);
    if (rel.empty() || rel.begin()->string() == "..") fail("path is outside the root: " + input);
  }
  return real;
}

inline Doc readDoc(const string& input, const string& root) {
  Doc d;
  d.input = input;
  d.file = realFile(input, root);
  std::ifstream in(d.file, std::ios::binary);
  if (!in) fail("cannot open: " + input);
  std::ostringstream buffer;
  buffer << in.rdbuf();
  string text = buffer.str();
  if (text.find('\0') != string::npos) fail("binary file: " + input);
  if (!validUtf8(text)) fail("not valid UTF-8: " + input);
  if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) {
    d.bom = true;
    text.erase(0, 3);
  }
  const size_t crlf = countOf(text, "\r\n");
  d.crlf = crlf > 0 && crlf == countOf(text, "\n");
  d.src = d.crlf ? normalizeText(text) : std::move(text);
  d.index();
  return d;
}

inline Position locate(const Doc& d, size_t offset) {
  const auto it = std::upper_bound(d.starts.begin(), d.starts.end(), offset);
  const size_t idx = static_cast<size_t>(it - d.starts.begin()) - 1;
  return {idx + 1, countChars(d.src, d.starts[idx], offset) + 1};
}

inline size_t posInt(const std::optional<long>& v, const char* name, long min = 1) {
  if (!v) fail(string(name) + " is required");
  if (*v < min) fail(string(name) + " must be >= " + std::to_string(min));
  return static_cast<size_t>(*v);
}

inline string requireText(const std::optional<string>& v, const char* name) {
  if (!v) fail(string(name) + " must be a string");
  return normalizeText(*v);
}

inline void checkLine(const Doc& d, size_t n, const char* name) {
  if (n > d.count) {
    fail(string(name) + " " + std::to_string(n) + " is out of range (file has " + std::to_string(d.count) +
         (d.count == 1 ? " line)" : " lines)"));
  }
}

struct Match {
  size_t start = 0;
  size_t end = 0;
  vector<std::optional<string>> groups;
};

inline bool hasFlag(const Edit& e, char c) { return e.flags && e.flags->find(c) != string::npos; }

inline string lowerAscii(string s) {
  for (char& c : s) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return s;
}

inline vector<Match> findMatches(const Doc& d, const Edit& e) {
  const string search = requireText(e.search, "search");
  if (search.empty()) fail("search must not be empty");
  vector<Match> out;

  if (!e.regex) {
    const bool fold = e.ignoreCase || hasFlag(e, 'i');
    const string hay = fold ? lowerAscii(d.src) : string();
    const string& haystack = fold ? hay : d.src;
    const string needle = fold ? lowerAscii(search) : search;
    for (size_t p = haystack.find(needle); p != string::npos; p = haystack.find(needle, p + needle.size())) {
      out.push_back({p, p + needle.size(), {d.src.substr(p, needle.size())}});
    }
    return out;
  }

  auto flags = std::regex::ECMAScript;
  if (e.ignoreCase || hasFlag(e, 'i')) flags |= std::regex::icase;
  std::regex re;
  try {
    re = std::regex(search, flags);
  } catch (const std::regex_error& err) {
    fail(string("invalid regex: ") + err.what());
  }

  const auto collect = [&](const string& hay, size_t base) {
    for (std::sregex_iterator it(hay.begin(), hay.end(), re), last; it != last; ++it) {
      Match m;
      m.start = base + static_cast<size_t>(it->position(0));
      m.end = m.start + static_cast<size_t>(it->length(0));
      for (size_t g = 0; g < it->size(); ++g) {
        m.groups.push_back((*it)[g].matched ? std::optional<string>((*it)[g].str()) : std::nullopt);
      }
      out.push_back(std::move(m));
    }
  };

  try {
    if (hasFlag(e, 's')) {
      collect(d.src, 0);
    } else {
      for (size_t i = 0; i < d.count; ++i) collect(d.line(i + 1), d.starts[i]);
    }
  } catch (const std::regex_error& err) {
    fail(string("regex failed: ") + err.what());
  }
  return out;
}

inline vector<Match> selectMatches(vector<Match> found, const Edit& e) {
  if (e.occurrence) {
    const size_t n = posInt(e.occurrence, "occurrence");
    if (n > found.size()) {
      fail("occurrence " + std::to_string(n) + " requested but only " + std::to_string(found.size()) +
           (found.size() == 1 ? " match found" : " matches found"));
    }
    return {found[n - 1]};
  }
  if (e.all && !*e.all) found.resize(1);
  return found;
}

inline string expandTemplate(const string& tpl, const Match& m) {
  const auto group = [&](size_t g) { return m.groups[g] ? *m.groups[g] : string(); };
  string out;
  for (size_t i = 0; i < tpl.size(); ++i) {
    if (tpl[i] != '$' || i + 1 >= tpl.size()) {
      out.push_back(tpl[i]);
      continue;
    }
    const char n = tpl[i + 1];
    if (n == '$') {
      out.push_back('$');
      ++i;
    } else if (n == '&') {
      out += group(0);
      ++i;
    } else if (n >= '0' && n <= '9') {
      const size_t one = static_cast<size_t>(n - '0');
      const bool twoDigits = i + 2 < tpl.size() && tpl[i + 2] >= '0' && tpl[i + 2] <= '9';
      const size_t two = twoDigits ? one * 10 + static_cast<size_t>(tpl[i + 2] - '0') : 0;
      if (twoDigits && two > 0 && two < m.groups.size()) {
        out += group(two);
        i += 2;
      } else if (one > 0 && one < m.groups.size()) {
        out += group(one);
        ++i;
      } else {
        out.push_back('$');
      }
    } else {
      out.push_back('$');
    }
  }
  return out;
}

inline vector<Range> contentRanges(const Doc& d, const Edit& e) {
  const vector<Match> found = findMatches(d, e);
  if (found.empty()) fail("no match for " + json::Value(clipChars(*e.search, 60)).dump());
  const string tpl = requireText(e.replace, "replace");
  vector<Range> out;
  for (const Match& m : selectMatches(found, e)) {
    out.push_back({m.start, m.end, e.regex ? expandTemplate(tpl, m) : tpl, "content", 0, m.start});
  }
  return out;
}

inline string reindent(const string& body, const string& indent) {
  vector<string> lines = splitLines(body);
  std::optional<string> common;
  for (const string& line : lines) {
    if (isBlank(line)) continue;
    const string lead = leadingSpace(line);
    common = common ? commonPrefix(*common, lead) : lead;
  }
  const size_t strip = common ? common->size() : 0;
  for (string& line : lines) line = isBlank(line) ? string() : indent + line.substr(strip);
  return joinLines(lines);
}

inline Range lineRange(const Doc& d, const Edit& e) {
  const size_t first = posInt(e.startLine, "startLine");
  const size_t last = e.endLine ? posInt(e.endLine, "endLine") : first;
  if (last < first) fail("endLine is before startLine");
  checkLine(d, first, "startLine");
  checkLine(d, last, "endLine");
  if (e.remove) {
    size_t start = d.starts[first - 1];
    size_t end = d.lineEnd(last - 1);
    Range r{start, end, string(), "lines", 0, start, true};
    if (end < d.src.size()) ++r.end;
    else if (start > 0) --r.start;
    return r;
  }
  string body = requireText(e.content, "content");
  if (!body.empty() && body.back() == '\n') body.pop_back();
  string indent;
  for (size_t n = first; n <= last; ++n) {
    const string line = d.line(n);
    if (!isBlank(line)) {
      indent = leadingSpace(line);
      break;
    }
  }
  const size_t start = d.starts[first - 1];
  return {start, d.lineEnd(last - 1), e.keepIndent ? reindent(body, indent) : body, "lines", 0, start};
}

inline std::pair<size_t, size_t> positionOffsets(const Doc& d, const Edit& e) {
  const size_t line = posInt(e.line, "line");
  checkLine(d, line, "line");
  const size_t from = posInt(e.from, "from");
  const size_t lineStart = d.starts[line - 1];
  const size_t lineStop = d.lineEnd(line - 1);

  const auto first = advance(d.src, lineStart, from - 1);
  if (!first || *first > lineStop) {
    fail("from " + std::to_string(from) + " is past the end of line " + std::to_string(line) + " (" +
         std::to_string(countChars(d.src, lineStart, lineStop)) + " chars)");
  }
  const size_t start = *first;

  std::optional<size_t> end;
  if (e.endLine) {
    const size_t endLine = posInt(e.endLine, "endLine");
    checkLine(d, endLine, "endLine");
    if (endLine < line) fail("endLine is before line");
    const size_t eStart = d.starts[endLine - 1];
    const size_t eLength = countChars(d.src, eStart, d.lineEnd(endLine - 1));
    const size_t to = e.to ? posInt(e.to, "to", 0) : eLength;
    if (to > eLength) {
      fail("to " + std::to_string(to) + " is past the end of line " + std::to_string(endLine) + " (" +
           std::to_string(eLength) + " chars)");
    }
    end = advance(d.src, eStart, to);
  } else if (e.length) {
    end = advance(d.src, start, posInt(e.length, "length", 0));
  } else if (e.to) {
    end = advance(d.src, lineStart, posInt(e.to, "to", 0));
  } else {
    end = advance(d.src, start, 1);
  }
  if (!end) fail("range runs past the end of the file");
  if (*end < start) fail("`to` is before `from`");
  return {start, *end};
}

inline Range positionRange(const Doc& d, const Edit& e) {
  const auto [start, end] = positionOffsets(d, e);
  return {start, end, requireText(e.content, "content"), "range", 0, start};
}

inline string inferType(const Edit& e) {
  if (e.search) return "content";
  if (e.startLine) return "lines";
  if (e.line) return "range";
  return string();
}

inline vector<Range> resolveEdit(const Doc& d, const Edit& e) {
  const string type = e.type.empty() ? inferType(e) : e.type;
  if (type == "content") return contentRanges(d, e);
  if (type == "lines") return {lineRange(d, e)};
  if (type == "range") return {positionRange(d, e)};
  fail("unknown edit type " + json::Value(type).dump() + " (use content, lines or range)");
}

inline Change describeChange(const Doc& d, const Doc& next, const Range& r, size_t outStart) {
  Change c;
  c.edit = r.edit;
  c.kind = r.kind;
  c.startLine = locate(d, r.shown).line;
  c.endLine = locate(d, std::max(r.shown, r.end > 0 ? r.end - 1 : 0)).line;
  const bool removed = r.removal;
  c.newStartLine = locate(next, outStart).line;
  c.newEndLine = locate(next, std::max(outStart, r.text.empty() ? outStart : outStart + r.text.size() - 1)).line;
  for (size_t n = c.startLine; n <= c.endLine; ++n) c.before.push_back(d.line(n));
  if (!removed) {
    for (size_t n = c.newStartLine; n <= c.newEndLine; ++n) c.after.push_back(next.line(n));
  }
  return c;
}

struct Applied {
  string src;
  vector<Change> changes;
};

inline Applied applyRanges(const Doc& d, vector<Range> ranges) {
  std::stable_sort(ranges.begin(), ranges.end(), [](const Range& a, const Range& b) {
    return a.start != b.start ? a.start < b.start : a.end < b.end;
  });
  for (size_t i = 1; i < ranges.size(); ++i) {
    if (ranges[i].start < ranges[i - 1].end) {
      fail("edits #" + std::to_string(ranges[i - 1].edit + 1) + " and #" + std::to_string(ranges[i].edit + 1) +
           " overlap near line " + std::to_string(locate(d, ranges[i].start).line));
    }
  }
  Applied out;
  vector<size_t> outStarts;
  size_t cursor = 0;
  for (const Range& r : ranges) {
    out.src.append(d.src, cursor, r.start - cursor);
    outStarts.push_back(out.src.size());
    out.src += r.text;
    cursor = r.end;
  }
  out.src.append(d.src, cursor, string::npos);

  Doc next;
  next.src = out.src;
  next.index();
  for (size_t i = 0; i < ranges.size(); ++i) out.changes.push_back(describeChange(d, next, ranges[i], outStarts[i]));
  return out;
}

inline void commit(const Doc& d, const string& src) {
  string text = d.crlf ? replaceAll(src, "\n", "\r\n") : src;
  if (d.bom) text.insert(0, "\xEF\xBB\xBF");

  const fs::path tmp = d.file.parent_path() /
                       ("." + d.file.filename().string() + "." +
                        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".tmp");
  std::error_code ec;
  bool ok = false;
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.flush();
    ok = static_cast<bool>(out);
  }
  if (ok) {
    fs::permissions(tmp, fs::status(d.file, ec).permissions(), ec);
    fs::rename(tmp, d.file, ec);
    ok = !ec;
  }
  if (!ok) {
    fs::remove(tmp, ec);
    std::ofstream out(d.file, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.flush();
    if (!out) fail("cannot write: " + d.input);
  }
}

inline EditResult editFile(const string& input, const vector<Edit>& edits, const string& root, bool dryRun,
                           const vector<int>* labels = nullptr) {
  const Doc d = readDoc(input, root);
  vector<Range> ranges;
  for (size_t i = 0; i < edits.size(); ++i) {
    const int label = labels ? (*labels)[i] : static_cast<int>(i);
    try {
      for (Range r : resolveEdit(d, edits[i])) {
        r.edit = label;
        ranges.push_back(std::move(r));
      }
    } catch (const EditError& err) {
      if (labels || edits.size() > 1) fail("edit #" + std::to_string(label + 1) + ": " + err.what());
      throw;
    }
  }
  if (ranges.empty()) fail("nothing to change");
  Applied applied = applyRanges(d, std::move(ranges));
  EditResult result;
  result.input = input;
  result.dryRun = dryRun;
  result.changed = applied.src != d.src;
  if (result.changed && !dryRun) {
    commit(d, applied.src);
    result.written = true;
  }
  result.changes = std::move(applied.changes);
  return result;
}

struct LinesView {
  size_t start = 0, end = 0, total = 0;
  vector<string> lines;
};

inline LinesView linesView(const Doc& d, const Edit& e) {
  LinesView v;
  v.start = e.startLine ? posInt(e.startLine, "startLine") : 1;
  v.end = e.endLine ? posInt(e.endLine, "endLine") : (e.startLine ? v.start : d.count);
  if (v.end < v.start) fail("endLine is before startLine");
  checkLine(d, v.start, "startLine");
  checkLine(d, v.end, "endLine");
  v.total = d.count;
  for (size_t n = v.start; n <= v.end; ++n) v.lines.push_back(d.line(n));
  return v;
}

struct RangeView {
  string text;
  size_t length = 0;
  Position from, to;
};

inline RangeView rangeView(const Doc& d, const Edit& e) {
  const auto [start, end] = positionOffsets(d, e);
  RangeView v;
  v.text = d.src.substr(start, end - start);
  v.length = countChars(d.src, start, end);
  v.from = locate(d, start);
  v.to = locate(d, std::max(start, end > 0 ? end - 1 : 0));
  return v;
}

struct MatchView {
  string text;
  Position from, to;
  vector<std::optional<string>> captures;
};

struct ContentView {
  size_t total = 0;
  vector<MatchView> matches;
  size_t context = 0;
  string query;
};

inline ContentView contentView(const Doc& d, const Edit& e) {
  const vector<Match> found = findMatches(d, e);
  const size_t limit = e.limit ? posInt(e.limit, "limit") : 50;
  ContentView v;
  v.total = found.size();
  v.context = e.context ? posInt(e.context, "context", 0) : 0;
  v.query = json::Value(clipChars(*e.search, 60)).dump() + (e.regex ? " (regex)" : "");
  for (size_t i = 0; i < found.size() && i < limit; ++i) {
    const Match& m = found[i];
    MatchView mv;
    mv.text = d.src.substr(m.start, m.end - m.start);
    mv.from = locate(d, m.start);
    mv.to = locate(d, std::max(m.start, m.end > 0 ? m.end - 1 : 0));
    mv.captures.assign(m.groups.begin() + 1, m.groups.end());
    v.matches.push_back(std::move(mv));
  }
  return v;
}

}  // namespace edit
