#!/bin/sh
# Edit MCP installer — by FgsiDev (https://github.com/FgsiDev)
set -eu

REPO="${EDIT_MCP_REPO:-FgsiDev/edit-mcp}"
VERSION="${EDIT_MCP_VERSION:-latest}"

if [ -n "${PREFIX:-}" ] && [ -d "$PREFIX/bin" ]; then DEST="$PREFIX/bin"; # Termux
elif [ -w /usr/local/bin ]; then DEST=/usr/local/bin
else DEST="$HOME/.local/bin"; mkdir -p "$DEST"; fi

OS="$(uname -s)"
ARCH="$(uname -m)"
if [ "$(uname -o 2>/dev/null || true)" = Android ]; then OS=Android; fi

case "$OS-$ARCH" in
  Android-aarch64|Android-arm64) ASSET=edit-mcp-android-arm64 ;;
  Android-armv7l|Android-armv8l|Android-arm) ASSET=edit-mcp-android-arm ;;
  Android-x86_64)                ASSET=edit-mcp-android-x86_64 ;;
  Android-i686|Android-i386)     ASSET=edit-mcp-android-x86 ;;
  Linux-x86_64)                  ASSET=edit-mcp-linux-x64 ;;
  Linux-aarch64|Linux-arm64)     ASSET=edit-mcp-linux-arm64 ;;
  Darwin-arm64)                  ASSET=edit-mcp-macos-arm64 ;;
  *) echo "unsupported platform: $OS-$ARCH (build from source: make)" >&2; exit 1 ;;
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
if ! "$DEST/edit-mcp" --version; then
  echo "the binary does not run here; build from source instead: make CXX=clang++" >&2
  exit 1
fi
"$DEST/edit-mcp" install-skill || echo "skill install skipped" >&2
echo "mcp config:"
"$DEST/edit-mcp" config
