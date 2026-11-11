class PersonalLogMcp < Formula
  desc "SQLite + FTS5 personal family log MCP server and CLI"
  homepage "https://github.com/bdombro/personal-log-mcp"
  url "https://github.com/bdombro/personal-log-mcp/archive/refs/tags/v2.0.0.tar.gz"
  sha256 "0000000000000000000000000000000000000000000000000000000000000000"
  license "ISC"
  head "https://github.com/bdombro/personal-log-mcp.git", branch: "main"

  depends_on "cmake" => :build
  depends_on "ninja" => :build
  depends_on "pkgconf" => :build
  depends_on "vcpkg" => :build

  def install
    vcpkg_root = ENV["VCPKG_ROOT"] || Formula["vcpkg"].opt_libexec.to_s
    deps_dir = buildpath/"build/deps"
    triplet = Hardware::CPU.arm? ? "arm64-osx" : "x64-osx"

    vcpkg_args = [
      "install",
      "--triplet=#{triplet}",
      "--x-install-root=#{deps_dir}",
      "--x-wait-for-lock",
    ]
    if (overlay = ENV["VCPKG_OVERLAY_PORTS"]) && !overlay.empty?
      vcpkg_args << "--overlay-ports=#{overlay}"
    end
    system "vcpkg", *vcpkg_args

    system "cmake", "-S", ".", "-B", "build/cmake/Release", "-G", "Ninja",
           "-DCMAKE_BUILD_TYPE=Release",
           "-DCMAKE_TOOLCHAIN_FILE=#{vcpkg_root}/scripts/buildsystems/vcpkg.cmake",
           "-DVCPKG_MANIFEST_MODE=OFF",
           "-DVCPKG_INSTALLED_DIR=#{deps_dir}",
           "-DVCPKG_TARGET_TRIPLET=#{triplet}",
           "-DENABLE_SANITIZERS=OFF"
    system "cmake", "--build", "build/cmake/Release"
    bin.install "build/bin/personal-log-mcp"
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
