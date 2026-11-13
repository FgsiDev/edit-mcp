#pragma once

#include <string>

namespace meta {

inline constexpr const char* kName = "edit-mcp";
inline constexpr const char* kTitle = "Edit MCP";
inline constexpr const char* kVersion = "1.0.0";
inline constexpr const char* kAuthor = "FgsiDev";
inline constexpr const char* kProfile = "https://github.com/FgsiDev";

inline std::string credit() {
  return std::string(kTitle) + " v" + kVersion + " — by " + kAuthor + " (" + kProfile + ")";
}

}  // namespace meta
