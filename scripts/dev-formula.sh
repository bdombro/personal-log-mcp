#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FORMULA_FILE="$ROOT_DIR/Formula/personal-log-mcp.rb"
STAGING_DIR="$ROOT_DIR/Formula/.staging"
BACKUP_FILE="$STAGING_DIR/personal-log-mcp.rb.bak"
TARBALL="$STAGING_DIR/personal-log-mcp-local.tar.gz"
BINARY="$ROOT_DIR/build/bin/personal-log-mcp"

usage() {
  echo "Usage: $0 {stage|reset}" >&2
  exit 2
}

stage_formula() {
  if [[ ! -x "$BINARY" ]]; then
    echo "Missing built Release binary: $BINARY (run 'just build' first)" >&2
    exit 1
  fi
  if [[ ! -f "$FORMULA_FILE" ]]; then
    echo "Missing release formula: $FORMULA_FILE" >&2
    exit 1
  fi

  mkdir -p "$STAGING_DIR"
  if [[ ! -f "$BACKUP_FILE" ]]; then
    cp "$FORMULA_FILE" "$BACKUP_FILE"
  fi

  local stage_src="$STAGING_DIR/src"
  rm -rf "$stage_src"
  mkdir -p "$stage_src"
  cp "$BINARY" "$stage_src/personal-log-mcp"
  tar -czf "$TARBALL" -C "$STAGING_DIR" src
  local sha
  sha="$(shasum -a 256 "$TARBALL" | awk '{print $1}')"

  cat > "$FORMULA_FILE" <<EOF
class PersonalLogMcp < Formula
  desc "SQLite + FTS5 personal family log MCP server and CLI (local staged build)"
  homepage "https://github.com/bdombro/personal-log-mcp"
  url "file://$TARBALL"
  version "2.0.0"
  sha256 "$sha"
  license "ISC"

  def install
    bin.install "personal-log-mcp"
  end

  service do
    run [opt_bin/"personal-log-mcp", "http", "--host", "127.0.0.1", "--port", "8082"]
    keep_alive true
    log_path var/"log/personal-log-mcp.log"
    error_log_path var/"log/personal-log-mcp.log"
  end

  test do
    ENV["BUGSINK_DSN"] = ""
    out = shell_output("#{bin}/personal-log-mcp status")
    assert_match "2.0.0", out
  end
end
EOF
  ruby -c "$FORMULA_FILE" >/dev/null
  echo "Staged local Homebrew formula using $TARBALL (sha256=$sha)"
}

reset_formula() {
  if [[ -f "$BACKUP_FILE" ]]; then
    mv "$BACKUP_FILE" "$FORMULA_FILE"
    echo "Restored release formula: $FORMULA_FILE"
  fi
  rm -rf "$STAGING_DIR"
}

case "${1:-}" in
  stage)
    stage_formula
    ;;
  reset)
    reset_formula
    ;;
  *)
    usage
    ;;
esac
