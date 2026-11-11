# personal-log-mcp: Developer Reference

User installation, configuration and compatibility status are in the
[README](../README.md). This reference preserves implementation details,
alternative transport schemas and validation commands. A client configuration
example is not a claim of verified July/OAuth interoperability.

Current HTTP source builds use local slimr and libhv overlays. The pinned
published registry baseline predates these HTTP changes; do not claim that
baseline builds the current HTTP app without overrides. CI configuration is
not evidence that a runner/platform/client has passed.

## App Architecture

The standalone MCP (stdio + Streamable HTTP), JSON CLI, and scheduled mining command are implemented in C++26 on `slimr-cpp`; data lives in the SQLite + FTS5 database.

```text
personal-log-mcp/
├── Formula/
│   └── personal-log-mcp.rb   # Homebrew formula + loopback HTTP service definition
├── .claude-plugin/           # Claude Code plugin & marketplace manifests (HTTP 127.0.0.1:8082)
├── .cursor-plugin/           # Cursor plugin manifest (HTTP 127.0.0.1:8082)
├── vcpkg.json                # Package dependencies installed under build/deps/
├── CMakeLists.txt            # App/test targets, imported dependencies, safety settings
├── justfile                  # CMake/Ninja configure, test, build, lint, test-mcp, Homebrew lifecycle
├── README.md                 # User & operator documentation
├── AGENTS.md                 # Maintainer & AI agent conventions
├── docs/
│   ├── conventions.md        # Log entry & tag conventions (`personal-log://conventions`)
│   └── recall.md             # Multi-source recall playbook (`personal-log://recall`)
├── scripts/
│   ├── dev-formula.sh        # Safe stage/reset helper for local Homebrew testing
│   └── test-mcp.mjs          # July 2026-07-28 stdio + ephemeral HTTP smoke test
├── src/
│   ├── db.hpp                # `AppDb` over `sqlite.hpp`: schema v1, WAL, per-DB mutex, FTS5 `entries_fts`, CRUD, merge, read-only `sql`, online `backup`
│   ├── import.hpp            # `parseYearNote` parser for legacy Obsidian `YYYY.md` yearly notes
│   ├── mine.hpp              # Scheduled mining pipeline (Gmail, Calendar, WhatsApp -> log, Deadlines, AI-Suggests, Meds, Contacts, Expenses) & Drive `backupLog`
│   ├── mine_config.hpp       # Strict lazy loader for `PERSONAL_LOG_MINE_CONFIG` / XDG `mine.json`
│   ├── docs_embed.hpp        # Compile-time embedded `conventions.md` and `recall.md` resources
│   ├── app.hpp               # `personal_log::make_personal_log_app()` registering all 13 tools, resources, concurrency annotations, and opt-in error reporting
│   └── main.cpp              # Standalone CLI, stdio (`mcp`), and Streamable HTTP (`http` 127.0.0.1:8082) entrypoint
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

- **`<mcp.hpp>`, `<mcp_http.hpp>`, `<reflect.hpp>`:** Typed MCP tool definitions, compile-time JSON Schema generation, embedded resources (`personal-log://recall`, `personal-log://conventions`), narrow JSON CLI, and Streamable HTTP server (`libhv`).
- **`<sqlite.hpp>` (`-lsqlite3`):** WAL mode + `synchronous=NORMAL` (for file-backed DBs), per-`AppDb` mutex across transactions and 256-statement LRU cache, FTS5 (`entries_fts`), nested savepoints (`merge`), `iterateLimited` (`SQLITE_OPEN_READONLY` + `PRAGMA query_only = ON` for `sql`), and online `sqlite3_backup_init` (`backup`).
- **`<curl.hpp>` (`-lcurl`):** LLM calls (DeepSeek, Gemini AI Studio) with retry/backoff (`fetchWithRetry`).
- **`<json.hpp>` (`glaze`):** Fast JSON serialization and LLM structured output parsing.
- **`<bugsink.hpp>` (`-lcurl`):** Automatic error and fallback warning reporting to an explicitly configured Bugsink endpoint (with PII scrubbed via `scrub()`).
- **`<test.hpp>` (`doctest`):** Full unit and pipeline test suite compiled with `-fsanitize=address,undefined`.

## Transports & Client Configuration

`personal-log-mcp` is a standalone C++26 binary exposing three invocation modes:

- **Streamable HTTP (`http`):** `personal-log-mcp http` (default endpoint `http://127.0.0.1:8082/mcp`, `--host 127.0.0.1 --port 8082`; `--port 0` requires `--allow-ephemeral-port`; non-loopback binds are refused).
- **stdio MCP (`mcp`):** `personal-log-mcp mcp` (newline-delimited JSON-RPC over stdin/stdout).
- **Direct JSON CLI:** `personal-log-mcp <tool> ['{"arg":"val"}']`.

> **Breaking MCP `2026-07-28` contract:** Both `http` and `mcp` implement MCP protocol version `2026-07-28` (`server/discover` with `params._meta` and HTTP `MCP-Protocol-Version` / `Mcp-Method` / `Mcp-Name` headers). Legacy `initialize` is removed and returns `-32601` (HTTP `404`).

### Streamable HTTP (Recommended)

Run via Homebrew (`just install && just start` or `just install-local && just start`) or directly with `personal-log-mcp http`.

**Claude Code (`.mcp.json` or `.claude-plugin/plugin.json`):**

```json
{
  "mcpServers": {
    "personal-log-mcp": {
      "type": "http",
      "url": "http://127.0.0.1:8082/mcp"
    }
  }
}
```

**Cursor (`.cursor/mcp.json` or `.cursor-plugin/plugin.json`):**

```json
{
  "mcpServers": {
    "personal-log-mcp": {
      "url": "http://127.0.0.1:8082/mcp"
    }
  }
}
```

### Standalone stdio MCP

```json
{
  "mcpServers": {
    "personal-log-mcp": {
      "command": "/absolute/path/to/personal-log-mcp",
      "args": ["mcp"]
    }
  }
}
```

### Homebrew Service Lifecycle

`Formula/personal-log-mcp.rb` installs `personal-log-mcp` and configures a loopback `brew services` daemon (`http --host 127.0.0.1 --port 8082`, `keep_alive true`, logs in `var/log/personal-log-mcp.log`):

```bash
just install          # Tap and install from release formula
just install-local    # Build Release binary, stage local formula, install, and restore formula
just reinstall-local  # Stop service if running, rebuild, and reinstall local formula
just start            # Start brew service and verify http://127.0.0.1:8082/mcp readiness
just stop             # Stop brew service
just restart          # Restart brew service and verify readiness
just status           # Inspect brew service status and probe server/discover
just uninstall        # Stop service and uninstall formula
```

## Building & Testing

Build with Clang/C++26 and libc++ FAST hardening, CMake 3.31+, Ninja, [just](https://just.systems), [vcpkg](https://vcpkg.io), and a compatible clang-tidy. Set `VCPKG_ROOT` to a bootstrapped checkout and put `vcpkg` on `PATH`; configure the local overlay explicitly:

```bash
export VCPKG_OVERLAY_PORTS="$VCPKG_ROOT/local-ports"
```

`just configure`, `test`, `build`, and `lint` automatically run `deps`, installing manifest dependencies under `build/deps/`. CMake uses installed `slimr::mcp_http` (`slimr-mcp[http]`) and `slimr::sqlite` targets with static SQLite/FTS5; manifest auto-install is disabled. Local overlay ports must already be registered; no source-checkout include overrides or global executable installs are used. Registry publication is separate.

Debug sanitizers and Release IPO builds have separate `build/cmake/Debug/` and `build/cmake/Release/` graphs. Both enable libc++ hardening and zero automatic-variable initialization; Debug adds ASan and fatal UBSan. Set `CXX` before the first configure to override the default Apple compiler on macOS or `clang++` on Linux; subsequent configures preserve CMake's cached compiler even when `clang-tidy` is added to `PATH`. `just lint` also honors `CLANG_TIDY` (defaulting to `clang-tidy`). CTest runs only synthetic/mock suites with a finite timeout and isolated repo-local HOME/XDG/temporary directories. Release builds run those tests too.

```bash
just test     # Run DB, FTS5, import, HTTP, concurrency, and mock mining tests with ASan + UBSan
just build    # Build/test Release binary at build/bin/personal-log-mcp
just lint     # Analyze app and test translation units using compile_commands.json
just test-mcp # Verify July 2026-07-28 stdio + ephemeral HTTP discovery, tools/list, status, and legacy rejection
just run --help # Run the existing Release binary without rebuilding
```

`just test-mcp` runs the server with an isolated HOME/XDG environment, no external CLI PATH or credentials, and Bugsink disabled; it never invokes mining, cloud, or database tools. Linux/macOS PR CI uses the same commands and requires the tested full `VCPKG_REF` commit variable; slimr packages use the pinned public registry without a dependency token. Linux execution and remote merge-gate enforcement are not claimed by local macOS verification.
