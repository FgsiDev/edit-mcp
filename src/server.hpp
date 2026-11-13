#pragma once

#include <iostream>
#include <optional>
#include <string>

#include "meta.hpp"
#include "tools.hpp"

namespace server {

inline constexpr const char* kVersions[] = {"2025-11-25", "2025-06-18", "2025-03-26", "2024-11-05"};

inline constexpr const char* kInstructions =
    "Local file editor. Line and column numbers are 1-based. Read with get_lines or get_content before editing. "
    "replace_lines keeps the indentation of the replaced line automatically. "
    "In multi_replace every line and column refers to the file as it was before the call.";

struct RpcError : std::runtime_error {
  int code;
  RpcError(int c, const std::string& message) : std::runtime_error(message), code(c) {}
};

inline json::Value replyBase(const json::Value* id) {
  json::Value r = json::Value::object();
  r.set("jsonrpc", "2.0");
  r.set("id", id ? *id : json::Value());
  return r;
}

inline json::Value resultReply(const json::Value* id, json::Value result) {
  json::Value r = replyBase(id);
  r.set("result", std::move(result));
  return r;
}

inline json::Value errorReply(const json::Value* id, int code, const std::string& message) {
  json::Value error = json::Value::object();
  error.set("code", code);
  error.set("message", message);
  json::Value r = replyBase(id);
  r.set("error", std::move(error));
  return r;
}

inline json::Value initialize(const json::Value& params) {
  std::string version = kVersions[0];
  if (const json::Value* asked = params.find("protocolVersion"); asked && asked->isString()) {
    for (const char* known : kVersions) {
      if (asked->text == known) version = known;
    }
  }
  json::Value tools = json::Value::object();
  tools.set("listChanged", false);
  json::Value capabilities = json::Value::object();
  capabilities.set("tools", std::move(tools));
  json::Value info = json::Value::object();
  info.set("name", meta::kName);
  info.set("title", std::string(meta::kTitle) + " — by " + meta::kAuthor);
  info.set("version", meta::kVersion);

  json::Value r = json::Value::object();
  r.set("protocolVersion", version);
  r.set("capabilities", std::move(capabilities));
  r.set("serverInfo", std::move(info));
  r.set("instructions", kInstructions);
  return r;
}

inline json::Value toolList() {
  json::Value list = json::Value::array();
  for (const tools::Tool& t : tools::all()) {
    json::Value entry = json::Value::object();
    entry.set("name", t.name);
    entry.set("description", t.description);
    entry.set("inputSchema", t.schema);
    json::Value hints = json::Value::object();
    hints.set("readOnlyHint", t.readOnly);
    if (!t.readOnly) hints.set("destructiveHint", true);
    entry.set("annotations", std::move(hints));
    list.push(std::move(entry));
  }
  json::Value r = json::Value::object();
  r.set("tools", std::move(list));
  return r;
}

inline json::Value callTool(const json::Value& params, const tools::Ctx& ctx) {
  const json::Value* name = params.find("name");
  if (!name || !name->isString()) throw RpcError(-32602, "Missing tool name");
  const json::Value* args = params.find("arguments");
  tools::Result result;
  try {
    result = tools::execute(name->text, args ? *args : json::Value::object(), ctx);
  } catch (const tools::UnknownTool& err) {
    throw RpcError(-32602, err.what());
  }
  json::Value block = json::Value::object();
  block.set("type", "text");
  block.set("text", result.text);
  json::Value content = json::Value::array();
  content.push(std::move(block));
  json::Value r = json::Value::object();
  r.set("content", std::move(content));
  if (result.isError) r.set("isError", true);
  return r;
}

inline json::Value route(const std::string& method, const json::Value& params, const tools::Ctx& ctx) {
  if (method == "initialize") return initialize(params);
  if (method == "ping") return json::Value::object();
  if (method == "tools/list") return toolList();
  if (method == "tools/call") return callTool(params, ctx);
  if (method.rfind("notifications/", 0) == 0) return json::Value();
  throw RpcError(-32601, "Method not found: " + method);
}

inline std::optional<json::Value> dispatch(const json::Value& msg, const tools::Ctx& ctx) {
  const json::Value* id = msg.isObject() ? msg.find("id") : nullptr;
  const json::Value* method = msg.isObject() ? msg.find("method") : nullptr;
  if (!method || !method->isString()) {
    if (msg.isObject() && (msg.find("result") || msg.find("error"))) return std::nullopt;
    return errorReply(id, -32600, "Invalid Request");
  }
  const json::Value* params = msg.find("params");
  const json::Value none = json::Value::object();
  try {
    json::Value result = route(method->text, params && params->isObject() ? *params : none, ctx);
    if (!id) return std::nullopt;
    return resultReply(id, std::move(result));
  } catch (const RpcError& err) {
    if (!id) return std::nullopt;
    return errorReply(id, err.code, err.what());
  } catch (const std::exception& err) {
    if (!id) return std::nullopt;
    return errorReply(id, -32603, err.what());
  }
}

inline void emit(const json::Value& message) { std::cout << message.dump() << '\n' << std::flush; }

inline int serve(const tools::Ctx& ctx) {
  std::string line;
  while (std::getline(std::cin, line)) {
    if (line.find_first_not_of(" \t\r\n") == std::string::npos) continue;
    json::Value msg;
    try {
      msg = json::Value::parse(line);
    } catch (const json::ParseError& err) {
      emit(errorReply(nullptr, -32700, std::string("Parse error: ") + err.what()));
      continue;
    }
    if (msg.isArray()) {
      json::Value out = json::Value::array();
      for (const json::Value& m : msg.items) {
        if (auto reply = dispatch(m, ctx)) out.push(std::move(*reply));
      }
      if (!out.items.empty()) emit(out);
    } else if (auto reply = dispatch(msg, ctx)) {
      emit(*reply);
    }
  }
  return 0;
}

}  // namespace server
