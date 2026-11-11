# personal-log-mcp

```
    .--------.
   /  TODAY  /|
  /--------/ |
 | events |  |
 | notes  | /
 |________|/
```

Use this MCP to search a timeline of family and personal events, with the original context available when it matters. Search by date, tag, or source; add and update entries; and use the recall and convention guides to keep answers grounded.

The MCP also runs scheduled mining across Gmail, Calendar, and WhatsApp, and can back up the log to Drive. It opens the existing SQLite database in place, so moving to this version does not require a data migration.

## Features

- Search and retrieve dated entries, inspect mining runs, and run bounded read-only SQL.
- Add, update, merge, or import entries; use embedded recall and convention resources.
- Run the scheduled mining pipeline and create Drive backups.
- Small footprint: a standalone binary and SQLite database, with no Node/V8 MCP runtime.

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

## Tools & CLI Commands

Every tool is exposed over Streamable HTTP (`personal-log-mcp http` at `http://127.0.0.1:8082/mcp`), stdio MCP (`personal-log-mcp mcp`), and the CLI (`personal-log-mcp <tool> ['{"arg":"val"}']`). Existing `personal-log://` resources and data paths are unchanged.

| Tool | Mutating | Thread-Safe HTTP | Description |
| --- | --- | --- | --- |
| `search` | No | Yes | Full-text BM25 search (`entries_fts`) combined with `from`/`to` date range, `tags` (all must match), `person`, and `limit` filters. |
| `get` | No | Yes | Fetch a log entry by `id` or by `source` (`{kind, id}` such as `cal`, `gm`, `wa`). |
| `add` | Yes | Serialized | Add dated `entries` (`date` as `YYYY.MM.DD` or `YYYY-MM-DD`, `title`, optional `details`, normalized `tags`, `people`, `links`, and `sources`). Deduplicates on existing `source` keys via `addKeepingEntry`. |
| `update` | Yes | Serialized | Update `date`, `title`, `details`, tags, people, links, or sources on an existing entry `id`. |
| `merge` | Yes | Serialized | Merge a duplicate entry (`mergeId`) into a target entry (`keepId`), unioning tags, people, links, and sources inside a transaction before deleting `mergeId`. |
| `delete` | Yes | Serialized | Delete an entry by `id`. |
| `sql` | No | Yes | Run a read-only `SELECT` / `WITH` `query` against `log.db` using `iterateLimited` (`SQLITE_OPEN_READONLY` + `PRAGMA query_only = ON`, row `limit` default 200 / max 1000). |
| `schema` | No | Yes | Returns the DDL and column documentation for `entries`, `entry_tags`, `entry_sources`, `entries_fts`, and `runs`. |
| `runs` | No | Yes | List recent mining job runs (`started_at`, `finished_at`, `status`, `window_since`, `window_until`, `stats`, `error`). |
| `import` | Yes | Serialized | Parse and import legacy Obsidian `YYYY.md` yearly notes (`parseYearNote`), extracting dates, inline `#tags`, `[[wikilinks]]`, markdown links, and `(cal:...)`/`(gm:...)`/`(wa:...)` source tags. Supports `dryRun`. |
| `backup` | Yes | Serialized | Perform an online SQLite backup (`sqlite3_backup_init`) of `log.db`, compress with `gzip`, upload to Google Drive (`Backups/personal-log/`), and prune older Drive backups (or copy locally via `destPath`). |
| `mine` | Yes | Serialized | Run the scheduled multi-stage mining pipeline across Gmail, Google Calendar, and WhatsApp (supports `since`, `until`, `dryRun`, `noBackup`). |
| `status` | No | Yes | Returns plugin version. |

### Embedded MCP Resources

Compiled directly into the binary (`src/docs_embed.hpp`) and served via `resources/read` and `mcp_resource_read`:
- `personal-log://recall` (`docs/recall.md`) — playbook for answering family/personal questions by combining `personal-log`, `obsidian`, `whatsapp`, `google-gws-mcp`, and `brave-history`.
- `personal-log://conventions` (`docs/conventions.md`) — rules for entry dates, summaries, normalized tags, source IDs (`cal:`, `gm:`, `wa:`), and what belongs in the log vs. Obsidian notes.

### CLI Examples

```bash
# Full-text search with date range and tag filter
./build/bin/personal-log-mcp search '{"query":"boiler service","from":"2025-01-01","tags":["home"]}'

# Add an entry
./build/bin/personal-log-mcp add '{"date":"2026-11-10","summary":"Ordered replacement air filter","tags":["home","maintenance"]}'

# Run a read-only SQL aggregation
./build/bin/personal-log-mcp sql '{"sql":"SELECT tag, COUNT(*) AS n FROM entry_tags GROUP BY tag ORDER BY n DESC LIMIT 10"}'

# Inspect recent mining runs
./build/bin/personal-log-mcp runs '{"limit":5}'

# Dry-run the mining job
./build/bin/personal-log-mcp mine '{"dryRun":true}'

# Online backup to Google Drive
./build/bin/personal-log-mcp backup
```

## Scheduled Mining Pipeline (`src/mine.hpp`)

Mining is opt-in. Before running it, create a **private** configuration file:

1. Copy [`docs/mine.example.json`](docs/mine.example.json) to
   `$XDG_CONFIG_HOME/personal-log/mine.json`, or `~/.config/personal-log/mine.json`
   when `XDG_CONFIG_HOME` is unset.
2. Replace every synthetic identity, address, calendar/task-list identifier,
   chat name, note path and document folder with destinations you explicitly
   authorize. Use a separate private suggestion calendar, never a source
   calendar or `primary`.
3. Restrict the file to your user (`chmod 600`). Never commit it.

`PERSONAL_LOG_MINE_CONFIG` selects an explicit file instead. All example fields
are required and strictly typed; unknown fields, missing files and invalid
values fail before mining opens the database or calls any source. Configuration
is read at mining invocation, not MCP startup or discovery. Other tools do not
require it. A dry run still reads authorized sources and uses the configured
LLM; it is not an offline configuration check.

The JSON contains `calendars` (source ID → label), `suggestCalendarId`,
`deadlinesTasklist`, `whatsappChats` (`name`, `kind`, `direct`), `keepDomains`,
`gmailAccount`, `selfName`, `timeZone`, `eventSuggestionNote`, `deadlineNote`,
`medicalExpensesNote`, `medicalExpensesSince` (`YYYY.MM.DD`), `medsNotes`
(person tag → note), `kidsDrive` (person tag → folder), `kidsSchoolFolder`,
`contactGroups`, `whoNames` (tag → display name), `familyAliases` and
`familySurname` (normalized lowercase aliases/surname excluded from contact
creation). Empty collections select no sources or targets for that collection.
Medical examples are fictional; supply only information you are authorized to
process.

Optional private `conventions.md` and `recall.md` files in the same XDG
`personal-log` directory replace the corresponding public MCP resources.
Keep household guidance, personal history and private note locations there.

1. **Window selection:** Reads `lastSuccessfulUntil` from the `runs` table (defaulting to a lookback window) up to `now`.
2. **Source gathering:**
   - **Gmail & Calendar:** Fetches messages, threads, and calendar events in the window via `gws` (filtering email noise via Jev scores and keep-domains).
   - **WhatsApp:** Exports message chunks across configured chats since the window start.
3. **Two-tier LLM pipeline (`ASSISTANT_LLM_FAST` & `ASSISTANT_LLM_DEFAULT`):**
   - **Fast model** triages email and WhatsApp chunks to filter noise and newsletters.
   - **Default model** drafts structured log entries (`add` / `update` / `merge`), `Deadlines` Google Tasks, `AI-Suggests` private calendar events (never writing source calendars), and follow-up updates to `Meds`, `Medical Expenses`, Kids Drive documents, and `Contacts`.
   - Automatic fallback (`ASSISTANT_LLM_FAST_FALLBACK`, `ASSISTANT_LLM_DEFAULT_FALLBACK`) if the primary provider hits rate limits or errors, recording a warning in `runs` and reporting to Bugsink.

### LLM & Environment Configuration

| Variable | Default | Purpose |
| --- | --- | --- |
| `PERSONAL_LOG_DB` | `~/.local/share/personal-log/log.db` | Path to the SQLite + FTS5 log database |
| `PERSONAL_LOG_MINE_CONFIG` | XDG `personal-log/mine.json` | Required private mining configuration |
| `PERSONAL_LOG_SUGGEST_CALENDAR_ID` | Configured `suggestCalendarId` | Suggestion destination override; validated before mining side effects |
| `ASSISTANT_LLM_DEFAULT` | `deepseek-v4-pro` | Model for drafting log entries, Meds, and Contacts (`agy:<model>`, `gemini-...`, or DeepSeek) |
| `ASSISTANT_LLM_FAST` | `deepseek-flash` | Model for high-volume email/WhatsApp chunk triage |
| `ASSISTANT_LLM_DEFAULT_FALLBACK` | `deepseek-v4-pro` | Fallback when `ASSISTANT_LLM_DEFAULT` fails (`none` disables) |
| `ASSISTANT_LLM_FAST_FALLBACK` | `deepseek-flash` | Fallback when `ASSISTANT_LLM_FAST` fails (`none` disables) |
| `DEEPSEEK_API_KEY` | — | API key for DeepSeek models |
| `GEMINI_API_KEY` | — | API key for Gemini AI Studio models |
| `BUGSINK_DSN` | Disabled | Error reporting DSN (unset or `""` disables) |

Error reporting is disabled by default; no reporting endpoint is bundled.

## macOS Build Prerequisites (vcpkg setup)

Install [Homebrew](https://brew.sh) and current Xcode Command Line Tools
(`xcode-select --install`; finish installation before continuing), or full Xcode.
Apple Clang 21 is the verified C++26/libc++ toolchain; older installations need
upgrading. These instructions assume Apple Silicon Homebrew at `/opt/homebrew`.

Add this environment snippet to `~/.zshrc`, then open a new terminal (or run
`source ~/.zshrc`). Use `~/.zshrc`, not `~/.zshenv`, to avoid changing
non-interactive zsh scripts:

`local-ports` is the dedicated vcpkg overlay directory for local development
linking: `just publish --local --all` registers symlinks to your slimr-cpp checkout,
overriding published packages without making a registry release. See the
[publishing guide on slimr-cpp](https://github.com/bdombro/slimr-cpp/blob/main/docs/publishing.md#local-environment).

```bash
export VCPKG_ROOT="$HOME/.local/share/vcpkg"
export VCPKG_OVERLAY_PORTS="$VCPKG_ROOT/local-ports"
# Use your existing workspace checkout instead when available.
export SLIMR_CPP="${SLIMR_CPP:-$HOME/dev/bdombro/slimr-cpp}"
export CXX="/usr/bin/clang++"
export CLANG_TIDY="/opt/homebrew/opt/llvm/bin/clang-tidy"
export PATH="$VCPKG_ROOT:/opt/homebrew/opt/ruby/bin:/opt/homebrew/bin:$PATH"
```

Then install and build from the repository root:

```bash
brew install cmake ninja just llvm node ruby autoconf automake libtool pkgconf
mkdir -p "$(dirname "$VCPKG_ROOT")"
if [ ! -d "$VCPKG_ROOT/.git" ]; then
  git clone https://github.com/microsoft/vcpkg.git "$VCPKG_ROOT"
fi
"$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics
mkdir -p "$(dirname "$SLIMR_CPP")"
if [ ! -d "$SLIMR_CPP/.git" ]; then
  git clone https://github.com/bdombro/slimr-cpp.git "$SLIMR_CPP"
fi
(cd "$SLIMR_CPP" && just publish --local --all)
ln -s "$SLIMR_CPP/ports/libhv" "$VCPKG_OVERLAY_PORTS/libhv"

"$CXX" --version
vcpkg version
BUGSINK_DSN="" just test
just build
```

The libhv link is a one-time step: if it exists, verify that it points to the
chosen checkout's `ports/libhv` before skipping the command. Never overwrite
a conflicting port directory or modify installed headers/caches. Current HTTP
builds need both local slimr overlays and the separately pinned libhv port;
the older published registry baseline alone is not an HTTP build substitute.
`just deps` installs dependencies under `build/deps/`, including static
SQLite/FTS5 and curl; separate Homebrew library installs are unnecessary.

Bootstrapping puts the executable at `$VCPKG_ROOT/vcpkg`, with no `bin/`
subdirectory. Keg-only Homebrew Ruby supplies `/opt/homebrew/opt/ruby/bin/ruby`.
Homebrew LLVM supplies lint tooling; select a different compatible `CXX`
before first configure if needed, since CMake caches it. For reproducible
vcpkg tooling, check out the tested full `VCPKG_REF` commit before bootstrapping;
package registry baselines pin resolution separately.
Mining/Drive backup need the companion plugins and an authenticated
[Google Workspace CLI](https://github.com/googleworkspace/cli)
(`npm install -g @googleworkspace/cli`); macOS already supplies `gzip`.
Private mining configuration and provider credentials are separate runtime
setup. These commands only run mock tests/builds: no mining, cloud
authorization, service startup, or scheduled job installation.

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
