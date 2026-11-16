class PersonalLogMcp < Formula
  desc "SQLite + FTS5 personal family log MCP server and CLI"
  homepage "https://github.com/bdombro/personal-log-mcp"
  license "ISC"
  version "2.0.0"

  on_macos do
    url "https://github.com/bdombro/personal-log-mcp/releases/download/v2.0.0/personal-log-mcp-arm64-macos.tar.gz"
    sha256 "0000000000000000000000000000000000000000000000000000000000000000"
  end

  on_linux do
    url "https://github.com/bdombro/personal-log-mcp/releases/download/v2.0.0/personal-log-mcp-x64-linux.tar.gz"
    sha256 "0000000000000000000000000000000000000000000000000000000000000000"
  end

  def install
    bin.install "personal-log-mcp"
    chmod 0755, bin/"personal-log-mcp"
    pkgshare.install "docs" if File.directory?("docs")
  end

  def post_install
    (var/"personal-log-mcp").mkpath
    chmod 0700, var/"personal-log-mcp"
    (var/"log").mkpath
  end

  service do
    run ["/bin/sh", "-c", "[ -f #{etc}/personal-log-mcp/env ] && set -a && . #{etc}/personal-log-mcp/env; exec #{opt_bin}/personal-log-mcp http --log-file #{var}/log/personal-log-mcp.log"]
    keep_alive true
    working_dir var/"personal-log-mcp"
    log_path var/"log/personal-log-mcp.log"
    error_log_path var/"log/personal-log-mcp.log"
    environment_variables PATH: "#{HOMEBREW_PREFIX}/bin:/usr/bin:/bin"
  end

  test do
    assert_match version.to_s, shell_output("#{bin}/personal-log-mcp --version")
    discover = '{"jsonrpc":"2.0","id":1,"method":"server/discover","params":{"_meta":{"io.modelcontextprotocol/protocolVersion":"2026-07-28","io.modelcontextprotocol/clientCapabilities":{}}}}'
    out = pipe_output("#{bin}/personal-log-mcp mcp --skip-checks", "#{discover}\n")
    assert_match "personal-log-mcp", out
  end
end
