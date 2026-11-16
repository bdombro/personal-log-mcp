class PersonalLogMcp < Formula
  desc "SQLite + FTS5 personal family log MCP server and CLI"
  homepage "https://github.com/bdombro/personal-log-mcp"
  license "ISC"
  version "3.1.0"

  on_macos do
    url "https://github.com/bdombro/personal-log-mcp/releases/download/v3.1.0/personal-log-mcp-arm64-macos.tar.gz"
    sha256 "98e41104cedaf839f69715429f0bc10a664ac2ca68bf5d52c81baafb58d09ba3"
  end

  on_linux do
    url "https://github.com/bdombro/personal-log-mcp/releases/download/v3.1.0/personal-log-mcp-x64-linux.tar.gz"
    sha256 "dc1159a9c9413fd0e79273cad273de5d2c778b1f166ae741c209b8d436c2e719"
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
    require "net/http"
    assert_match version.to_s, shell_output("#{bin}/personal-log-mcp --version")
    r, w = IO.pipe
    pid = spawn("#{bin}/personal-log-mcp", "--skip-checks", "http", "--host", "127.0.0.1", "--port", "0", err: w)
    w.close
    begin
      port = nil
      r.each_line { |line| break if (port = line[%r{listening on http://[\d.]+:(\d+)/mcp}, 1]) }
      assert port, "server did not report a listening port"
      discover = '{"jsonrpc":"2.0","id":1,"method":"server/discover","params":{"_meta":{"io.modelcontextprotocol/protocolVersion":"2026-07-28","io.modelcontextprotocol/clientCapabilities":{}}}}'
      res = Net::HTTP.post(URI("http://127.0.0.1:#{port}/mcp"), discover,
                           "Content-Type"         => "application/json",
                           "Accept"               => "application/json, text/event-stream",
                           "MCP-Protocol-Version" => "2026-07-28",
                           "Mcp-Method"           => "server/discover")
      assert_match "personal-log-mcp", res.body
    ensure
      Process.kill("TERM", pid)
      Process.wait(pid)
    end
  end
end
