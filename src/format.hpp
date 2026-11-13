#pragma once

#include <set>
#include <string>
#include <vector>

#include "core.hpp"

namespace edit {

inline constexpr size_t kMaxBlocks = 20;
inline constexpr size_t kMaxLines = 12;
inline constexpr size_t kMaxLineChars = 200;

inline string plural(size_t n, const string& word, const string& suffix = "s") {
  return std::to_string(n) + " " + word + (n == 1 ? "" : suffix);
}

inline string span(size_t a, size_t b) {
  return a == b ? "line " + std::to_string(a) : "lines " + std::to_string(a) + "-" + std::to_string(b);
}

inline string clipLine(const string& line) {
  const size_t total = countChars(line, 0, line.size());
  if (total <= kMaxLineChars) return line;
  return line.substr(0, *advance(line, 0, kMaxLineChars)) + "… (+" + std::to_string(total - kMaxLineChars) + " chars)";
}

inline vector<string> marked(const vector<string>& lines, const string& sign) {
  vector<string> out;
  for (size_t i = 0; i < lines.size() && i < kMaxLines; ++i) out.push_back(sign + " " + clipLine(lines[i]));
  if (lines.size() > kMaxLines) out.push_back(sign + " … " + plural(lines.size() - kMaxLines, "more line"));
  return out;
}

inline string numbered(const vector<string>& lines, size_t first, const std::set<size_t>* highlight = nullptr) {
  const size_t width = std::to_string(first + lines.size() - 1).size();
  string out;
  for (size_t i = 0; i < lines.size(); ++i) {
    const size_t n = first + i;
    if (i) out.push_back('\n');
    if (highlight) out += highlight->count(n) ? "> " : "  ";
    const string num = std::to_string(n);
    out += string(width - num.size(), ' ') + num + " | " + lines[i];
  }
  return out;
}

inline string join(const vector<string>& parts, const string& sep) {
  string out;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i) out += sep;
    out += parts[i];
  }
  return out;
}

inline string formatEdit(const EditResult& r) {
  const string state = r.dryRun ? "dry-run, nothing written" : r.written ? "written" : "no change";
  vector<string> blocks{r.input + ": " + plural(r.changes.size(), "change") + " — " + state};
  for (size_t i = 0; i < r.changes.size() && i < kMaxBlocks; ++i) {
    const Change& c = r.changes[i];
    string moved;
    if (c.newStartLine != c.startLine || c.newEndLine - c.newStartLine != c.endLine - c.startLine) {
      moved = " → now " + span(c.newStartLine, c.newEndLine);
    }
    vector<string> lines{"#" + std::to_string(c.edit + 1) + " " + c.kind + " @ " + span(c.startLine, c.endLine) + moved};
    for (string& l : marked(c.before, "-")) lines.push_back(std::move(l));
    for (string& l : marked(c.after, "+")) lines.push_back(std::move(l));
    blocks.push_back(join(lines, "\n"));
  }
  if (r.changes.size() > kMaxBlocks) blocks.push_back("… " + plural(r.changes.size() - kMaxBlocks, "more change"));
  return join(blocks, "\n\n");
}

inline string formatLines(const string& input, const LinesView& v, bool raw) {
  if (raw) return join(v.lines, "\n");
  return input + " (" + span(v.start, v.end) + " of " + std::to_string(v.total) + ")\n" + numbered(v.lines, v.start);
}

inline string formatRange(const string& input, const RangeView& v) {
  const string size = v.length == 0 ? "empty" : plural(v.length, "char");
  const string head = input + " " + std::to_string(v.from.line) + ":" + std::to_string(v.from.col) + " → " +
                      std::to_string(v.to.line) + ":" + std::to_string(v.to.col) + " (" + size + ")";
  return v.length == 0 ? head : head + "\n" + v.text;
}

inline string formatContent(const Doc& d, const ContentView& v) {
  if (v.total == 0) return d.input + ": no match for " + v.query;
  const string shown = v.matches.size() < v.total ? " (showing first " + std::to_string(v.matches.size()) + ")" : "";
  vector<string> blocks{d.input + ": " + plural(v.total, "match", "es") + " for " + v.query + shown};
  for (size_t i = 0; i < v.matches.size(); ++i) {
    const MatchView& m = v.matches[i];
    const size_t first = m.from.line > v.context ? m.from.line - v.context : 1;
    const size_t last = std::min(d.count, m.to.line + v.context);
    std::set<size_t> hits;
    for (size_t n = m.from.line; n <= m.to.line; ++n) hits.insert(n);
    string title = "#" + std::to_string(i + 1) + " " + std::to_string(m.from.line) + ":" + std::to_string(m.from.col) +
                   " → " + std::to_string(m.to.line) + ":" + std::to_string(m.to.col);
    if (!m.captures.empty()) {
      json::Value caps = json::Value::array();
      for (const auto& c : m.captures) caps.push(c ? json::Value(*c) : json::Value());
      title += "\n  captures: " + caps.dump();
    }
    vector<string> lines;
    for (size_t n = first; n <= last; ++n) lines.push_back(d.line(n));
    blocks.push_back(title + "\n" + numbered(lines, first, &hits));
  }
  return join(blocks, "\n\n");
}

}  // namespace edit
