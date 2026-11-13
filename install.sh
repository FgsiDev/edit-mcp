#!/bin/sh
# Edit MCP installer — by FgsiDev (https://github.com/FgsiDev)
set -eu

REPO="${EDIT_MCP_REPO:-FgsiDev/edit-mcp}"
VERSION="${EDIT_MCP_VERSION:-latest}"

if [ -n "${PREFIX:-}" ] && [ -d "$PREFIX/bin" ]; then DEST="$PREFIX/bin"; # Termux
elif [ -w /usr/local/bin ]; then DEST=/usr/local/bin
else DEST="$HOME/.local/bin"; mkdir -p "$DEST"; fi

case "$(uname -s)-$(uname -m)" in
  Linux-x86_64)                 ASSET=edit-mcp-linux-x64 ;;
  Linux-aarch64|Linux-arm64)    ASSET=edit-mcp-linux-arm64 ;;
  Darwin-arm64)                 ASSET=edit-mcp-macos-arm64 ;;
  *) echo "unsupported platform: $(uname -s)-$(uname -m)" >&2; exit 1 ;;
esac

if [ "$VERSION" = latest ]; then URL="https://github.com/$REPO/releases/latest/download/$ASSET"
else URL="https://github.com/$REPO/releases/download/$VERSION/$ASSET"; fi

TMP="$(mktemp)"
trap 'rm -f "$TMP"' EXIT
if command -v curl >/dev/null 2>&1; then curl -fsSL "$URL" -o "$TMP"; else wget -qO "$TMP" "$URL"; fi
chmod +x "$TMP"
mv "$TMP" "$DEST/edit-mcp"
trap - EXIT

echo "installed: $DEST/edit-mcp"
"$DEST/edit-mcp" --version
"$DEST/edit-mcp" install-skill || echo "skill install skipped" >&2
echo "mcp config:"
"$DEST/edit-mcp" config
