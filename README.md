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

## `.mcp.json` Configuration

In the `assistant2` workspace, `personal-log-mcp` is mounted into the unified `build/bin/assistant-mcp` gateway under the `log_` prefix (`log_search`, `log_get`, `log_add`, `log_update`, `log_merge`, `log_delete`, `log_sql`, `log_schema`, `log_runs`, `log_import`, `log_backup`, `log_mine`, `log_status`).

To run `personal-log-mcp` as a standalone MCP server in `.mcp.json`:

```json
{
  "mcpServers": {
    "personal-log-mcp": {
      "command": "/absolute/path/to/personal-log-mcp",
      "args": ["mcp"],
      "env": {
        "PERSONAL_LOG_DB": "/absolute/path/to/log.db"
      }
    }
  }
}
```

## App Architecture

The standalone MCP, JSON CLI, and scheduled mining command are implemented in C++26 on `slimr-cpp`; data lives in the existing SQLite + FTS5 database.

```text
plugins/personal-log-mcp/
├── vcpkg.json                # Package dependencies installed under build/deps/
├── CMakeLists.txt            # App/test targets, imported dependencies, safety settings
├── justfile                  # CMake/Ninja configure, test, build, lint, test-mcp
├── README.md                 # User & operator documentation
├── AGENTS.md                 # Maintainer & AI agent conventions
├── docs/
│   ├── conventions.md        # Log entry & tag conventions (`personal-log://conventions`)
│   └── recall.md             # Multi-source recall playbook (`personal-log://recall`)
├── src/
│   ├── db.hpp                # `AppDb` over `sqlite.hpp`: schema v1, FTS5 `entries_fts`, CRUD, merge, read-only `sql`, online `backup`
│   ├── import.hpp            # `parseYearNote` parser for legacy Obsidian `YYYY.md` yearly notes
│   ├── mine.hpp              # Scheduled mining pipeline (Gmail, Calendar, WhatsApp -> log, Deadlines, AI-Suggests, Meds, Contacts, Expenses) & Drive `backupLog`
│   ├── docs_embed.hpp        # Compile-time embedded `conventions.md` and `recall.md` resources
│   ├── app.hpp               # `personal_log::make_personal_log_app()` registering all 13 tools, resources, and opt-in error reporting
│   └── main.cpp              # Standalone CLI & stdio MCP entrypoint
├── tests/
│   ├── test_db.hpp           # tests covering `AppDb`, FTS5, `parseYearNote`, CLI & MCP
│   ├── test_mine.hpp         # mock tests covering mining pipeline, LLM fallback, sanitizers, follow-ups & backup pruning
│   └── test_main.cpp         # Combined test runner (all suites)
└── build/bin/
    └── personal-log-mcp      # Standalone native Release binary
```

### Libraries Used

- **`<mcp.hpp>` & `<reflect.hpp>`:** Typed MCP tool definitions, compile-time JSON Schema generation, embedded resources (`personal-log://recall`, `personal-log://conventions`), and narrow JSON CLI.
- **`<sqlite.hpp>` (`-lsqlite3`):** WAL mode, FTS5 (`entries_fts`), 256-statement LRU cache, nested savepoints (`merge`), `iterateLimited` (`SQLITE_OPEN_READONLY` + `PRAGMA query_only = ON` for `log_sql`), and online `sqlite3_backup_init` (`log_backup`).
- **`<curl.hpp>` (`-lcurl`):** LLM calls (DeepSeek, Gemini AI Studio) with retry/backoff (`fetchWithRetry`).
- **`<json.hpp>` (`glaze`):** Fast JSON serialization and LLM structured output parsing.
- **`<bugsink.hpp>` (`-lcurl`):** Automatic error and fallback warning reporting to an explicitly configured Bugsink endpoint (with PII scrubbed via `scrub()`).
- **`<test.hpp>` (`doctest`):** Full unit and pipeline test suite compiled with `-fsanitize=address,undefined`.

## Tools & CLI Commands

Every tool is exposed over MCP (`personal-log-mcp mcp` or via `assistant-mcp` under the `log_` prefix) and on the CLI (`personal-log-mcp <tool> ['{"arg":"val"}']`). Existing `personal-log://` resources and data paths are unchanged.

| Tool | Gateway Name | Mutating | Description |
| --- | --- | --- | --- |
| `search` | `log_search` | No | Full-text BM25 search (`entries_fts`) combined with `from`/`to` date range, `tags` (all must match), `source` prefix (`cal:`, `gm:`, `wa:`), and `limit` filters. |
| `get` | `log_get` | No | Fetch one or more log entries by `id` (or `ids`) or by `source` reference (`cal:...`, `gm:...`, `wa:...`). |
| `add` | `log_add` | Yes | Add a dated entry (`date` as `YYYY-MM-DD`, `summary`, optional `details`, normalized `tags`, `links`, and `sources`). Deduplicates on existing `source` keys via `addKeepingEntry`. |
| `update` | `log_update` | Yes | Update `date`, `summary`, `details`, `tags`, `links`, or `sources` on an existing entry ID. |
| `merge` | `log_merge` | Yes | Merge a duplicate entry (`fromId`) into a target entry (`intoId`), unioning `tags`, `links`, and `sources` inside a transaction before deleting `fromId`. |
| `delete` | `log_delete` | Yes | Delete an entry by `id`. |
| `sql` | `log_sql` | No | Run a read-only `SELECT` / `WITH` query against `log.db` using `iterateLimited` (`SQLITE_OPEN_READONLY` + `PRAGMA query_only = ON`, row limit default 200 / max 1000). |
| `schema` | `log_schema` | No | Returns the DDL and column documentation for `entries`, `entry_tags`, `entry_sources`, `entries_fts`, and `runs`. |
| `runs` | `log_runs` | No | List recent mining job runs (`started_at`, `finished_at`, `status`, `window_since`, `window_until`, `stats`, `error`). |
| `import` | `log_import` | Yes | Parse and import legacy Obsidian `YYYY.md` yearly notes (`parseYearNote`), extracting dates, inline `#tags`, `[[wikilinks]]`, markdown links, and `(cal:...)`/`(gm:...)`/`(wa:...)` source tags. Supports `dryRun`. |
| `backup` | `log_backup` | Yes | Perform an online SQLite backup (`sqlite3_backup_init`) of `log.db`, compress with `gzip`, upload to Google Drive (`Backups/personal-log/`), and prune older Drive backups according to the retention policy. |
| `mine` | `log_mine` | Yes | Run the scheduled multi-stage mining pipeline across Gmail, Google Calendar, and WhatsApp (supports `since`, `until`, `dryRun`). |
| `status` | `log_status` | No | Returns plugin version and active database path. |

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

## Building & Testing

Build with Clang/C++26 and libc++ FAST hardening, CMake 3.31+, Ninja, [just](https://just.systems), [vcpkg](https://vcpkg.io), and a compatible clang-tidy. Set `VCPKG_ROOT` to a bootstrapped checkout and put `vcpkg` on `PATH`; configure the local overlay explicitly:

```bash
export VCPKG_OVERLAY_PORTS="$VCPKG_ROOT/local-ports"
```

`just configure`, `test`, `build`, and `lint` automatically run `deps`, installing manifest dependencies under `build/deps/`. CMake uses installed `slimr::mcp` and `slimr::sqlite` targets with static SQLite/FTS5; manifest auto-install is disabled. Local overlay ports must already be registered; no source-checkout include overrides or global executable installs are used. Registry publication is separate.

Debug sanitizers and Release IPO builds have separate `build/cmake/Debug/` and `build/cmake/Release/` graphs. Both enable libc++ hardening and zero automatic-variable initialization; Debug adds ASan and fatal UBSan. Set `CXX` before the first configure to override the default Apple compiler on macOS or `clang++` on Linux; subsequent configures preserve CMake's cached compiler even when clang-tidy is added to `PATH`. CTest runs only synthetic/mock suites with a finite timeout and isolated repo-local HOME/XDG/temporary directories. Release builds run those tests too.

```bash
just test     # Run unit and pipeline tests covering DB, FTS5, import, and mining pipeline with ASan + UBSan
just build    # Build/test Release binary at build/bin/personal-log-mcp
just lint     # Analyze app and test translation units using compile_commands.json
just test-mcp # Inspector 2.14.0 initialize + tools/list only (requires compatible Node/npm)
just run --help # Run the existing Release binary without rebuilding
```

Inspector runs the server with an isolated HOME/XDG environment, no external CLI PATH or credentials, and Bugsink disabled; it never invokes mining, cloud, or database tools. Linux/macOS PR CI uses the same commands and requires the tested full `VCPKG_REF` commit variable; slimr packages use the pinned public registry without a dependency token. Linux execution and remote merge-gate enforcement are not claimed by local macOS verification.
