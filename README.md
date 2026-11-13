# edit-mcp

Local MCP server and CLI for editing files. One C++17 binary, zero dependencies, no build step beyond `make`.

Credit: **FgsiDev** — https://github.com/FgsiDev

## Build

```sh
make            # ./edit-mcp
make install    # /usr/local/bin/edit-mcp (PREFIX=... to change)
# or: cmake -B build && cmake --build build
```

Termux: `pkg install clang make && make CXX=clang++`.

## Install a release binary

Tagging `vX.Y.Z` builds static binaries (linux-x64, linux-arm64, macos-arm64) via GitHub Actions and attaches them to the release with `SHA256SUMS`. The linux-arm64 build is the one for Termux.

```sh
curl -fsSL https://raw.githubusercontent.com/FgsiDev/edit-mcp/main/install.sh | sh
```

`install.sh` picks the right asset, installs to `$PREFIX/bin` (Termux), `/usr/local/bin` or `~/.local/bin`, and prints the MCP config. Override with `EDIT_MCP_REPO` and `EDIT_MCP_VERSION`.

## Use as MCP

`edit-mcp` with no arguments speaks MCP over stdio. `edit-mcp config` prints the config for this binary (absolute path filled in):

```sh
edit-mcp config                    # opencode format
edit-mcp config --agent claude     # mcpServers JSON: Claude Code (.mcp.json), Cursor, Gemini CLI, ...
edit-mcp config --agent codex      # TOML for ~/.codex/config.toml
edit-mcp config --root ~/project --name edit
```

opencode (`opencode.json`):

```json
{
  "mcp": {
    "servers": {
      "edit": {
        "type": "local",
        "command": ["/path/to/edit-mcp"]
      }
    }
  }
}
```

Claude Code: `claude mcp add edit -- /path/to/edit-mcp`. Codex: add to `~/.codex/config.toml`:

```toml
[mcp_servers.edit]
command = "/path/to/edit-mcp"
```

Agents without MCP support (pi, as far as I know) use the CLI through their shell; the skill below teaches both.

Add `"--root", "/dir"` to `command` to confine every path to that directory (symlinks resolved). Without `--root`, paths are relative to the working directory, absolute, or `~/...`.

## Agent skill

The skill (`SKILL.md`, the shared Agent Skills format) teaches an agent which tool to pick, the coordinate rules, the indentation behaviour, recipes (insert, append, delete, regex, batch) and the CLI fallback. It is embedded in the binary:

```sh
edit-mcp install-skill                  # ~/.claude/skills/edit-mcp + ~/.agents/skills/edit-mcp
edit-mcp install-skill --project        # same, inside the current project
edit-mcp install-skill --target claude  # or: agents
edit-mcp install-skill --dir ~/my/skills
edit-mcp skill > SKILL.md               # print it
```

| agent | reads the skill from |
|---|---|
| Claude Code | `~/.claude/skills/`, `.claude/skills/` |
| opencode | `~/.agents/skills/`, `.agents/skills/` (also `~/.claude/skills/`, `.claude/skills/`) |
| Codex | `~/.agents/skills/`, `.agents/skills/` |
| pi | `~/.agents/skills/`, `.agents/skills/` (also `~/.pi/agent/skills/`, `.pi/skills/`) |

`--target all` (default) writes both `.claude` and `.agents`, which covers all four. For agents without skill support (Cursor rules, Gemini CLI, Aider, ...), paste the body of `skills/edit-mcp/SKILL.md` into `AGENTS.md` / `GEMINI.md` / the rules file.

`skills/edit-mcp/SKILL.md` in this repo is generated from the binary (`make skill`); the source of truth is `src/skill.hpp`.

## Tools

All line/column numbers are 1-based. Columns count characters, not bytes.

| tool | what |
|---|---|
| `replace_content` | search/replace. Literal by default, `regex` for ECMAScript with `$1`/`$&`. All matches unless `all:false` or `occurrence:N`. |
| `replace_lines` | replace `startLine`..`endLine` (default one line) with `content`. The indentation of the replaced line is applied automatically; relative indentation inside multi-line content is kept. `keepIndent:false` replaces the whitespace too. |
| `delete_lines` | delete `startLine`..`endLine` including the newline (no blank line left). |
| `replace_range` | replace by position: `line` + `from`, ending at `to` (inclusive column), `length`, or `endLine` + `to`. Without `endLine`, `to` counts from the line start and runs through newlines into following lines. `length:0` inserts. |
| `multi_replace` | many edits (content / lines / range mixed, several files). Every coordinate refers to the file **before** the call, so edits never shift each other. Per file all-or-nothing. Overlaps are rejected. |
| `get_content` | find with `line:col`, captures, `context` lines. |
| `get_lines` | read lines with numbers (`raw` for plain). |
| `get_range` | read a character range with coordinates. |
| `multi_get` | many reads in one call. |

Every write tool accepts `dryRun` and returns a before/after diff. CRLF line endings and BOM are preserved. Writes are atomic (temp file + rename). Binary and non-UTF-8 files are refused.

Regex: patterns match per line by default; `flags:"s"` runs the pattern over the whole file so it can span lines.

## CLI

```sh
edit-mcp replace-content app.js 'const ee = require("path")' 'const path = require("path")'
edit-mcp replace-content app.js 'ee' 'path' --first -n          # dry run
edit-mcp replace-content app.js 'foo(\d+)' 'bar_$1' -r
edit-mcp replace-lines   app.js 2 'console.log("fgsi")'          # indent follows line 2
edit-mcp replace-lines   app.js 2-4 -e 'a()\nb()'                # -e: \n \t escapes
edit-mcp delete-lines    app.js 10-12
edit-mcp replace-range   app.js 1:5-8 renamed                    # line 1, cols 5..8
edit-mcp replace-range   app.js 1:5-3:2 x                        # line 1 col 5 .. line 3 col 2
edit-mcp replace-range   app.js 1:1 --length 0 'HEAD '           # insert
edit-mcp get-lines       app.js 10-20
edit-mcp get-content     app.js 'TODO' --context 2
edit-mcp get-range       app.js 1:5-8
edit-mcp multi-replace   edits.json                              # or: ... | edit-mcp multi-replace -
cat new.txt | edit-mcp replace-lines app.js 5-9 --stdin
```

`edits.json`:

```json
[
  {"path": "app.js", "type": "content", "search": "ee", "replace": "path"},
  {"path": "app.js", "type": "lines", "startLine": 3, "endLine": 4, "content": "run()"},
  {"path": "app.js", "type": "range", "line": 5, "from": 5, "to": 8, "content": "x"}
]
```

Exit codes: `0` ok, `1` edit failed, `2` usage error. Run `edit-mcp --help` for all options.
