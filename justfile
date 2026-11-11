set shell := ["zsh", "-f", "-eu", "-o", "pipefail", "-c"]
set positional-arguments

architecture := replace(replace(arch(), "aarch64", "arm64"), "x86_64", "x64")
system := if os() == "macos" { "osx" } else { os() }
triplet := f'{{architecture}}-{{system}}'
cxx := env("CXX", if os() == "macos" { "/usr/bin/clang++" } else { "clang++" })

_:
    @just --list

deps:
    vcpkg install --triplet={{triplet}} --x-install-root=build/deps --x-wait-for-lock

configure configuration="Debug": deps
    CXX="{{cxx}}" cmake -S . -B build/cmake/{{configuration}} -G Ninja \
        -DCMAKE_BUILD_TYPE={{configuration}} \
        -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
        -DVCPKG_MANIFEST_MODE=OFF \
        -DVCPKG_INSTALLED_DIR="{{justfile_directory()}}/build/deps" \
        -DVCPKG_TARGET_TRIPLET={{triplet}} \
        -DENABLE_SANITIZERS={{if configuration == "Debug" { "ON" } else { "OFF" }}}

test: (configure "Debug")
    cmake --build build/cmake/Debug --parallel 2
    ctest --test-dir build/cmake/Debug --output-on-failure --no-tests=error

build: (configure "Release")
    cmake --build build/cmake/Release --parallel 2
    ctest --test-dir build/cmake/Release --output-on-failure --no-tests=error

tap := "bdombro/personal-log-mcp"
formula := "bdombro/personal-log-mcp/personal-log-mcp"
endpoint := "http://127.0.0.1:8082/mcp"

lint: (configure "Debug")
    CLANG_TIDY="${CLANG_TIDY:-clang-tidy}" && "$CLANG_TIDY" -p build/cmake/Debug src/main.cpp tests/test_main.cpp

test-mcp: build
    node scripts/test-mcp.mjs build/bin/personal-log-mcp add,get,search,update,merge,delete,sql,schema,runs,import,backup,mine,status

install:
    #!/usr/bin/env bash
    set -euo pipefail
    if ! brew tap | grep -qx "{{tap}}"; then
      brew tap "{{tap}}" "https://github.com/bdombro/personal-log-mcp.git"
    fi
    HOMEBREW_NO_AUTO_UPDATE=1 brew install --build-from-source "{{formula}}"

_link-local-tap:
    #!/usr/bin/env bash
    set -euo pipefail
    repo_root="$(pwd)"
    tap_dir="$(brew --repository)/Library/Taps/bdombro/homebrew-personal-log-mcp"
    mkdir -p "$(dirname "$tap_dir")"
    if [[ -L "$tap_dir" ]]; then
      if [[ "$(readlink "$tap_dir")" != "$repo_root" ]]; then
        ln -sfn "$repo_root" "$tap_dir"
      fi
    elif [[ -e "$tap_dir" ]]; then
      echo "Existing non-symlink tap at $tap_dir; run 'brew untap {{tap}}' first for local installs." >&2
      exit 1
    else
      ln -s "$repo_root" "$tap_dir"
    fi

install-local: build _link-local-tap
    #!/usr/bin/env bash
    set -euo pipefail
    ./scripts/dev-formula.sh stage
    trap './scripts/dev-formula.sh reset' EXIT
    HOMEBREW_NO_AUTO_UPDATE=1 brew install --formula "{{formula}}"

reinstall-local: build _link-local-tap
    #!/usr/bin/env bash
    set -euo pipefail
    was_running=0
    if brew services list | awk '$1 == "personal-log-mcp" && $2 == "started" { found=1 } END { exit !found }'; then
      was_running=1
      brew services stop "{{formula}}"
    fi
    ./scripts/dev-formula.sh stage
    trap './scripts/dev-formula.sh reset' EXIT
    if brew list --formula personal-log-mcp >/dev/null 2>&1; then
      HOMEBREW_NO_AUTO_UPDATE=1 brew reinstall --formula "{{formula}}"
    else
      HOMEBREW_NO_AUTO_UPDATE=1 brew install --formula "{{formula}}"
    fi
    if [[ "$was_running" -eq 1 ]]; then
      brew services start "{{formula}}"
      just _wait-ready
    fi

_wait-ready:
    node -e 'const deadline = Date.now() + 10000; async function check() { while (Date.now() < deadline) { try { const r = await fetch("{{endpoint}}", { method: "POST", headers: { "Content-Type": "application/json", Accept: "application/json, text/event-stream", "MCP-Protocol-Version": "2026-07-28", "Mcp-Method": "server/discover" }, body: JSON.stringify({ jsonrpc: "2.0", id: 1, method: "server/discover", params: { _meta: { "io.modelcontextprotocol/protocolVersion": "2026-07-28", "io.modelcontextprotocol/clientCapabilities": {} } } }) }); if (r.ok) { const j = await r.json(); if (j.result?.supportedVersions?.includes("2026-07-28")) return; } } catch {} await new Promise(r => setTimeout(r, 250)); } console.error("Endpoint not ready at {{endpoint}}"); process.exit(1); } check();'

start: _ensure-installed
    brew services start "{{formula}}"
    just _wait-ready

_ensure-installed:
    brew list --formula "{{formula}}" >/dev/null

stop:
    #!/usr/bin/env bash
    set -euo pipefail
    if brew services list | awk '$1 == "personal-log-mcp" && $2 == "started" { found=1 } END { exit !found }'; then
      brew services stop "{{formula}}"
    fi

restart: _ensure-installed
    brew services restart "{{formula}}"
    just _wait-ready

status:
    brew services info "{{formula}}"
    node -e 'fetch("{{endpoint}}", { method: "POST", headers: { "Content-Type": "application/json", Accept: "application/json, text/event-stream", "MCP-Protocol-Version": "2026-07-28", "Mcp-Method": "server/discover" }, body: JSON.stringify({ jsonrpc: "2.0", id: 1, method: "server/discover", params: { _meta: { "io.modelcontextprotocol/protocolVersion": "2026-07-28", "io.modelcontextprotocol/clientCapabilities": {} } } }) }).then(async r => { console.log("HTTP", r.status, await r.text()); if (!r.ok) process.exit(1); }).catch(e => { console.error(e.message); process.exit(1); });'

uninstall: stop
    #!/usr/bin/env bash
    set -euo pipefail
    if brew list --formula personal-log-mcp >/dev/null 2>&1; then
      brew uninstall personal-log-mcp
    fi
    tap_dir="$(brew --repository)/Library/Taps/bdombro/homebrew-personal-log-mcp"
    if [[ -L "$tap_dir" && "$(readlink "$tap_dir")" == "$(pwd)" ]]; then
      rm "$tap_dir"
    fi

run *args:
    ./build/bin/personal-log-mcp "$@"

clean:
    rm -rf build
