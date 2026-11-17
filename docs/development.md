# personal-log-mcp: Developer Reference

User installation, configuration and compatibility status are in the
[README](../README.md). This reference preserves implementation details,
alternative transport schemas and validation commands. A client configuration
example is not a claim of verified July/OAuth interoperability.

## App Architecture

The standalone Streamable HTTP MCP, JSON CLI, and scheduled mining command are implemented in C++26 on the `slimr-*` libraries (`slimr-mcp`, `slimr-sqlite`); data lives in the SQLite + FTS5 database.

```text
personal-log-mcp/
├── Formula/
│   └── personal-log-mcp.rb   # Homebrew formula (prebuilt macOS/Linux binaries) + HTTP service definition
├── .claude-plugin/           # Claude Code plugin & marketplace manifests (HTTP 127.0.0.1:10002)
├── .cursor-plugin/           # Cursor plugin manifest (HTTP 127.0.0.1:10002)
├── VERSION                   # Release version compiled into the binary
├── CHANGELOG.md              # Unreleased and released changes
├── vcpkg.json                # Package dependencies installed under build/deps/
├── vcpkg-configuration.json  # Builtin + slimr-* git registries (baselines moved by `just upgrade-deps`)
├── triplets/                 # vcpkg overlay triplets (macOS 15.0 floor, Linux libc++)
├── CMakeLists.txt            # App/test targets, imported dependencies, safety settings
├── justfile                  # cpp-templates recipes: build, test, lint, test-mcp, release, Homebrew service lifecycle
├── README.md                 # User & operator documentation
├── AGENTS.md                 # Maintainer & AI agent conventions
├── docs/
│   ├── conventions.md        # Log entry & tag conventions (`personal-log://conventions`)
│   └── recall.md             # Multi-source recall playbook (`personal-log://recall`)
├── scripts/
│   ├── doctor.sh             # Toolchain and required-tool checks (`just doctor`)
│   ├── formula-shared.ts     # Formula rendering shared by release tooling
│   ├── release.ts            # Release workflow helper (`just release`)
│   └── test-mcp.ts           # July 2026-07-28 ephemeral HTTP smoke test
├── src/
│   ├── db.hpp                # `AppDb` over `sqlite.hpp`: schema v1, WAL, per-DB mutex, FTS5 `entries_fts`, CRUD, merge, read-only `sql`, online `backup`
│   ├── import.hpp            # `parseYearNote` parser for legacy Obsidian `YYYY.md` yearly notes
│   ├── mine.hpp              # Scheduled mining pipeline (Gmail, Calendar, WhatsApp -> log, Deadlines, AI-Suggests, Meds, Contacts, Expenses) & Drive `backupLog`
│   ├── mine_config.hpp       # Strict lazy loader for `PERSONAL_LOG_MINE_CONFIG` / `$(brew --prefix)/etc/personal-log-mcp/mine.json`
│   ├── docs_embed.hpp        # Compile-time embedded `conventions.md` and `recall.md` resources
│   ├── app.hpp               # `personal_log::make_personal_log_app()` registering all 13 tools, resources, concurrency annotations, and opt-in error reporting
│   ├── log_file.hpp          # `--log-file` redirection and 10 MiB rotation (verbatim template file)
│   └── main.cpp              # Standalone CLI and Streamable HTTP (`http` 127.0.0.1:10002, `--log-file`) entrypoint
├── tests/
│   ├── test_db.hpp           # Tests covering `AppDb`, FTS5, `parseYearNote`, CLI & MCP
│   ├── test_http.hpp         # Tests covering Streamable HTTP, July 2026-07-28 headers, CLI validation, WAL & multi-threaded concurrency
│   ├── test_mine.hpp         # Mock tests covering mining pipeline, LLM fallback, sanitizers, follow-ups & backup pruning
│   ├── test_mine_config.hpp  # Strict config & lazy-discovery isolation tests
│   └── test_main.cpp         # Combined test runner (all suites)
└── build/bin/
    └── personal-log-mcp      # Standalone native Release binary
```

### Libraries Used

- **`<mcp.hpp>`, `<mcp_http.hpp>`, `<reflect.hpp>`:** Typed MCP tool definitions, compile-time JSON Schema generation, embedded resources (`personal-log://recall`, `personal-log://conventions`), narrow JSON CLI, and Streamable HTTP server (`cpp-httplib`).
- **`<sqlite.hpp>` (`-lsqlite3`):** WAL mode + `synchronous=NORMAL` (for file-backed DBs), per-`AppDb` mutex across transactions and 256-statement LRU cache, FTS5 (`entries_fts`), nested savepoints (`merge`), `iterateLimited` (`SQLITE_OPEN_READONLY` + `PRAGMA query_only = ON` for `sql`), and online `sqlite3_backup_init` (`backup`).
- **`<httplib.h>` (`cpp-httplib[openssl]`):** HTTP client with retry/backoff (`fetchWithRetry`).
- **`<json.hpp>` (`glaze`):** Fast JSON serialization and LLM structured output parsing.
- **`<bugsink.hpp>` (`cpp-httplib[openssl]`):** Automatic error and fallback warning reporting to an explicitly configured Bugsink endpoint (with PII scrubbed via `scrub()`).
- **`<test.hpp>` (`doctest`):** Full unit and pipeline test suite compiled with `-fsanitize=address,undefined`.

## Transports & Client Configuration

`personal-log-mcp` is a standalone C++26 binary exposing two invocation modes:

- **Streamable HTTP (`http`):** `personal-log-mcp http` (binds `127.0.0.1:10002`, endpoint `http://127.0.0.1:10002/mcp`; `--host <addr>` and `--port <n>` override, `--port 0` picks an ephemeral port; `--log-file <path>` redirects output with 10 MiB rotation).
- **Direct JSON CLI:** `personal-log-mcp <tool> ['{"arg":"val"}']`.

> **Breaking MCP `2026-07-28` contract:** Streamable HTTP (`http`) implements MCP protocol version `2026-07-28` (`server/discover` with `params._meta` and HTTP `MCP-Protocol-Version` / `Mcp-Method` / `Mcp-Name` headers). Legacy `initialize` is removed and returns `-32601` (HTTP `404`).

### Streamable HTTP (Recommended)

Run via Homebrew (`just install`, which also starts the service) or directly with `personal-log-mcp http`.

**Claude Code (`.mcp.json` or `.claude-plugin/plugin.json`):**

```json
{
  "mcpServers": {
    "personal-log-mcp": {
      "type": "http",
      "url": "http://127.0.0.1:10002/mcp"
    }
  }
}
```

**Cursor (`.cursor/mcp.json` or `.cursor-plugin/plugin.json`):**

```json
{
  "mcpServers": {
    "personal-log-mcp": {
      "url": "http://127.0.0.1:10002/mcp"
    }
  }
}
```

### Homebrew Service Lifecycle

`Formula/personal-log-mcp.rb` installs the prebuilt `personal-log-mcp` binary and configures a `brew services` daemon (`/bin/sh -c` sourcing `$(brew --prefix)/etc/personal-log-mcp/env`, then `http --log-file $(brew --prefix)/var/log/personal-log-mcp.log`, `keep_alive true`, working dir `$(brew --prefix)/var/personal-log-mcp`):

```bash
just install          # Clean-install the released formula from GitHub, brew test it, start the service
just daemon-start     # Start the brew service and wait for server/discover
just daemon-stop      # Stop the brew service
just daemon-restart   # Restart the brew service and wait for readiness
just daemon-status    # Inspect brew service status and probe server/discover
just daemon-logs      # Follow the service log
just uninstall        # Stop the service, uninstall the formula, remove the tap
```

## Building & Testing

Build with Clang/C++26 and libc++ FAST hardening, CMake 3.31+, Ninja, [just](https://just.systems), [vcpkg](https://vcpkg.io) and Homebrew `llvm@23` (clang-format, clang-tidy). `just doctor` checks the toolchain and prints fixes. Set `VCPKG_ROOT` to a bootstrapped checkout and put `vcpkg` on `PATH`; `slimr-*` packages resolve from their pinned git registries in `vcpkg-configuration.json`, and `just upgrade-deps` moves every baseline at once.

`just configure`, `test`, `build`, and `lint` automatically run `deps`, installing manifest dependencies under `build/deps/`. CMake uses installed `slimr::mcp_http` (`slimr-mcp[http]`) and `slimr::sqlite` targets with static SQLite/FTS5; manifest auto-install is disabled.

Debug sanitizers and Release IPO builds have separate `build/cmake/Debug/` and `build/cmake/Release/` graphs. Both enable libc++ hardening and zero automatic-variable initialization; Debug adds ASan and fatal UBSan. macOS builds use `llvm@23` clang++ with static `libc++` and a 15.0 deployment floor; Linux builds use `llvm@23` clang++ with `libc++`. `CXX` overrides both; set it before the first configure, since CMake caches the compiler. Set `LLVM_BIN` to override the `llvm@23` binary directory for `clang-format` and `clang-tidy`. CTest runs only synthetic/mock suites with a finite timeout and isolated repo-local HOME, `HOMEBREW_PREFIX` and temporary directories.

```bash
just doctor   # Check required tools and C++26 support
just test     # Run DB, FTS5, import, HTTP, concurrency, log-file, and mock mining tests with ASan + UBSan
just test "<case>"  # Run only matching doctest cases (Debug, BUGSINK_DSN="")
just build    # Build the Release binary at build/bin/personal-log-mcp (no tests)
just lint     # clang-format check and clang-tidy bug-finding checks
just test-mcp # Verify July 2026-07-28 ephemeral HTTP discovery and tools/list
```

`just test` with no filter runs CTest, which points HOME, `HOMEBREW_PREFIX` and `TMPDIR` at scratch paths and `PERSONAL_LOG_MINE_CONFIG` at the test fixture. `just test "<case>"` runs the test binary directly with only `BUGSINK_DSN=` set, so path tests isolate their own env. `just test-mcp` runs the server with an isolated environment, no external CLI PATH or credentials, and Bugsink disabled; it never invokes mining, cloud, or database tools.

Run the direct binary on a separate dev port so it doesn't collide with the service:

```bash
PERSONAL_LOG_DB="$PWD/build/dev.db" ./build/bin/personal-log-mcp http --port 20002
```

On Linux (`ubuntu-24.04`), install `llvm@23` via Homebrew and enable lingering so `brew services` systemd user units survive logout (`loginctl enable-linger "$USER"`).

## Develop against an unreleased lib

Add `"overlay-ports"` to the consumer's `vcpkg-configuration.json`, then run `just test` or `just build` as usual (add one entry per unreleased lib):

```json
{
  "overlay-ports": [
    "<relative path to ~/dev/slimr/<name>/ports/slimr-<name>>"
  ]
}
```

Remove `"overlay-ports"` and bump the registry baseline once the lib is released. Never commit or release with `overlay-ports` set.

## Release

Add a line under `## Unreleased` in `CHANGELOG.md`, push to `main`, then run:

```bash
just release patch   # or minor / major
```

The release workflow runs `test.yml` on each platform, bumps `VERSION`, builds the macOS arm64 and Linux x64 binaries, updates the formula and tags the release.
