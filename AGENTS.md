# AGENTS.md (`personal-log-mcp`)

Written for: AI coding agents working in the `personal-log-mcp` C++26 plugin repository.

`personal-log-mcp` is a C++26 MCP plugin and CLI built on `slimr-cpp` that manages a personal log in SQLite + FTS5 (`~/.local/share/personal-log/log.db`), serves the `personal-log://recall` and `personal-log://conventions` resources, backs up the database to Google Drive, and runs the scheduled mining pipeline (`mine`).

## Commands

| Task | Command |
| --- | --- |
| Run unit & pipeline tests (ASan + UBSan) | `just test` |
| Build release binary (`build/bin/personal-log-mcp`) | `just build` |
| Static analysis | `just lint` |
| Isolated MCP startup/discovery | `just test-mcp` |
| Clean build artifacts | `just clean` |

## Module Boundaries

| Path | Owns | Must Not |
| --- | --- | --- |
| `src/main.cpp` | Entrypoint calling `personal_log::make_personal_log_app().run(argc, argv)` | Business logic |
| `src/app.hpp` | `make_personal_log_app()` registering all 13 tools (`add`, `get`, `search`, `update`, `merge`, `delete`, `sql`, `schema`, `runs`, `import`, `backup`, `mine`, `status`), resources, and opt-in Bugsink reporting | Direct SQL or LLM logic |
| `src/db.hpp` | `AppDb` over `slimr::Database` (`PRAGMA user_version = 1`), FTS5 `ftsQuery`, date/tag normalization, CRUD, `merge`, read-only `sql` (`iterateLimited`), and `runs` tracking | Change schema v1 incompatibly with live `log.db` |
| `src/import.hpp` | `parseYearNote` parser for legacy Obsidian `YYYY.md` notes | Mutate vault files |
| `src/mine.hpp` | Scheduled mining pipeline (`runMine`), `ToolRunner`, LLM triage/draft/fallback (`modelFor`, `fetchWithRetry`), follow-ups (`addExpenseRows`, `replaceMedsSection`, Contacts, Kids Drive docs), PII scrubber (`scrub`), and `backupLog` / `prune` | Write source calendars or leak PII in Bugsink warnings |
| `src/mine_config.hpp` | Strict, lazy private JSON mining configuration and XDG paths | Embed private defaults or echo configuration values in errors |
| `src/docs_embed.hpp` | Embedded `docs/conventions.md` and `docs/recall.md` | — |
| `tests/` | `test_db.hpp` and `test_mine.hpp` using `TempDirGuard` SQLite DBs and mock `ToolRunner` | Touch `~/.local/share/personal-log/log.db` or live APIs |

## Safety & App Conventions

1. **In-place database compatibility:** `src/db.hpp` opens `~/.local/share/personal-log/log.db` at `PRAGMA user_version = 1`. Never alter existing table/column/trigger definitions incompatibly with the live database.
2. **Calendar isolation in `mine.hpp`:** Mining only writes suggested events to the private `AI-Suggests` calendar (`suggestCalendarId`, validated against `^[\w.@-]+$`). Never write source calendars; the suggestion destination must be explicitly configured and separate.
3. **PII Scrubbing:** All warnings and error messages sent to Bugsink from `mine.hpp` must pass through `warningCategory()` / `scrub()` so email addresses, phone numbers, URLs, and message bodies are never transmitted.
4. **Tests use temp DBs only:** Never run tests against `~/.local/share/personal-log/log.db`. Always use `:memory:` or `slimr::testing::TempDirGuard`.

## Build Architecture

CMake declares targets/dependencies; Ninja owns incremental builds. Just orchestrates automatic `deps` before configure/build/test/lint; CMake manifest auto-install is disabled. Set `VCPKG_ROOT` and `VCPKG_OVERLAY_PORTS` in the environment. Dependencies install under `build/deps/`; Debug sanitizer and Release trees live under `build/cmake/`. Use installed slimr targets, never source-checkout include overrides. SQLite is statically linked with FTS5; no system SQLite CLI or server is required. `run` does not rebuild.

Use a Clang/C++26 toolchain with libc++ FAST hardening, CMake, Ninja, vcpkg, just, and compatible clang-tidy. Inspector uses pinned npm tooling and an isolated server environment; it only initializes and lists tools. Tests use synthetic databases and mocked mining APIs; never run mining, backup, or real cloud tools for validation.

## C++ Memory Safety

- Immediately wrap owned C handles, allocations, descriptors, and temporary resources in RAII. Make owning wrappers noncopyable unless copying creates independent ownership; moves must leave the source safely destructible.
- Borrow views, pointers, and references only for the current call by default. Store owning values unless lifetime/invalidation guarantees are explicit. Deferred callbacks require explicit ownership-safe captures; no escaping reference captures without a demonstrated lifetime guarantee.
- Initialize scalar/pointer members at declaration and local values before use. Compiler auto-initialization is defense in depth, not permission for uninitialized reads.
- Check range/overflow before narrowing lengths/counts at C API boundaries; casts alone are not validation. Report invalid inputs using existing error mechanisms.
- Never let C++ exceptions escape C callbacks. Translate them into the C API's error/abort mechanism and propagate failures at the C++ boundary; do not silently swallow them.
- Preserve hardening, zero auto-initialization, ASan/UBSan with fatal reports, and required lint/tests. These checks do not prove memory safety.

Mining requires a private JSON file (`PERSONAL_LOG_MINE_CONFIG` or the XDG `personal-log/mine.json`). Load and validate it before opening the database or calling tools. Discovery and non-mining tools must work without it. Public tests use only `tests/fixtures/mine.json`. Optional private conventions and recall guides live in the same XDG directory. Never commit user configuration or copied live records. Bugsink is disabled unless `BUGSINK_DSN` is explicitly set.
