#pragma once

// Agent Skill (SKILL.md, the shared Agent Skills format) embedded in the binary.
// `edit-mcp skill` prints it, `edit-mcp install-skill` writes it. Credit: FgsiDev.

namespace skill {

inline constexpr const char* kName = "edit-mcp";

inline constexpr const char* kText = R"SKILL(---
name: edit-mcp
description: Precise local file editing with the edit-mcp tools or CLI. Replace text, replace or delete lines by number, replace by line:column, apply many edits in one call, and read lines or ranges with line numbers. Use when changing code in place, when exact line or column control is needed, or when several edits must land in one file without recalculating line numbers.
metadata:
  author: FgsiDev
  source: https://github.com/FgsiDev
---

# edit-mcp

Local file editor by FgsiDev. It runs either as an MCP server (tools below) or as a CLI (`edit-mcp <command>`). Same engine, same behaviour. Tool names may carry a server prefix in your client (`edit_replace_lines`, `mcp__edit__replace_lines`, ...).

Use the MCP tools when they are listed. If they are not, and you have a shell, use the CLI (see the end).

## Pick the tool

| goal | tool |
|---|---|
| rename or swap known text (`ee` -> `path`) | `replace_content` |
| rewrite or insert code at known line numbers | `replace_lines` |
| remove lines | `delete_lines` |
| change part of a line, or a span that crosses lines | `replace_range` |
| 2+ edits in one file, or edits across files | `multi_replace` |
| look before editing | `get_lines`, `get_content`, `get_range`, `multi_get` |

## Rules

1. Read first. Get real line numbers from `get_lines` or `get_content`; never guess them.
2. Numbers are 1-based. Columns count characters, not bytes.
3. `replace_lines` copies the indentation of the first replaced line onto your content. Write content WITHOUT the block's base indentation; relative indentation inside multi-line content is kept. Pass `keepIndent:false` only when you need the exact whitespace you wrote.
4. `replace_lines` content replaces the whole line(s). A single trailing newline in content is ignored. Empty content leaves an empty line; use `delete_lines` to remove lines.
5. More than one edit in the same file: send ONE `multi_replace`. Every line and column in it refers to the file as it was BEFORE the call, so do no offset math. Separate single-edit calls shift the numbers below the edit; re-read before the next one.
6. Edits in one `multi_replace` must not overlap (it errors and writes nothing for that file). A file is all-or-nothing.
7. For anything risky or large, run with `dryRun:true` first. The result is a before/after diff, and `-> now lines X-Y` shows where the new text landed.
8. `replace_content` replaces ALL matches by default. Use `occurrence:N` or `all:false` when you mean one. It errors when nothing matches, so a missing target is never silent.

## Recipes

- Replace line 5: `replace_lines {path, startLine:5, content:"new text"}`
- Replace lines 3-7 with new code: `replace_lines {startLine:3, endLine:7, content:"a()\nb()"}`
- Delete lines 10-12: `delete_lines {startLine:10, endLine:12}`
- Insert before line N: `replace_range {line:N, from:1, length:0, content:"new line\n"}`
- Append after the last line L (length C): `replace_range {line:L, from:C+1, length:0, content:"\nnew line"}`
- Replace columns 5-8 of line 1: `replace_range {line:1, from:5, to:8, content:"x"}`
- Replace from line 1 col 5 to line 3 col 2: `replace_range {line:1, from:5, endLine:3, to:2, content:"x"}`
- `to` without `endLine` counts from the line start and may run past the line end into the next lines; a newline counts as 1 char. `length` counts from `from`.
- Regex: `replace_content {search:"foo\\((\\d+)\\)", replace:"bar($1)", regex:true}`. ECMAScript syntax, matched per line; add `flags:"s"` to match across lines. `$1`, `$&` expand; write `$$` for a literal `$`.
- Batch: `multi_replace {edits:[{path, type:"content", search, replace}, {path, type:"lines", startLine, endLine, content}, {path, type:"lines", startLine, remove:true}, {path, type:"range", line, from, to, content}]}`. `type` can be omitted: `search` means content, `startLine` lines, `line` range.

## Errors

- `no match for "..."`: the text is not in the file; re-read it, do not retry blindly.
- `startLine N is out of range (file has M lines)`: the file changed or the number is wrong; re-read.
- `edits #A and #B overlap near line N`: merge them into one edit.
- `path is outside the root`: the server is confined with `--root`; stay inside it.
- `binary file` / `not valid UTF-8`: this tool edits text only.

## CLI (no MCP in your client)

```sh
edit-mcp get-lines app.js 10-30                 # numbered lines
edit-mcp get-content app.js 'TODO' --context 2
edit-mcp replace-content app.js 'ee' 'path' -n  # -n = dry run
edit-mcp replace-lines app.js 5 'new text'
edit-mcp replace-lines app.js 3-4 -e 'a()\nb()' # -e turns \n \t into real characters
edit-mcp delete-lines app.js 10-12
edit-mcp replace-range app.js 1:5-8 x           # LINE:FROM-TO or LINE:FROM-ENDLINE:TO
edit-mcp replace-range app.js 1:1 --length 0 'HEAD '
edit-mcp multi-replace edits.json               # same JSON as the tool; - reads stdin
printf '  A\n  B' | edit-mcp replace-lines app.js 5-6 --stdin
```

Other flags: `-r` regex, `-i` ignore case, `--first`, `--occurrence N`, `--flags s`, `--no-keep-indent`, `--raw`, `--root DIR`. Exit code 0 ok, 1 edit failed (message on stderr), 2 usage error. Prefer `--stdin` or a JSON file over shell-quoted multi-line text.
)SKILL";

}  // namespace skill
