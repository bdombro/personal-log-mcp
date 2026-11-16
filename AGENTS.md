# AGENTS.md (`personal-log-mcp`)

Written for: AI coding agents working in the `personal-log-mcp` C++26 plugin repository.

`personal-log-mcp` is a C++26 MCP plugin (`2026-07-28` stdio + Streamable HTTP on port `10002`) and CLI built on `slimr-mcp` and `slimr-sqlite` that manages a personal log in SQLite + FTS5 (`$(brew --prefix)/var/personal-log-mcp/log.db`), serves the `personal-log://recall` and `personal-log://conventions` resources, backs up the database to Google Drive, and runs the scheduled mining pipeline (`mine`).

<!-- cpp-templates:begin -->
## Tooling alignment

Align this repository via the `cpp-templates` skill (`just check <template> .`). Keep repository-specific rules outside `<!-- cpp-templates:begin -->` and `<!-- cpp-templates:end -->`. Do not hand-edit verbatim template files or template recipes (repositories may add extra recipes or variables).

## Edit-don't-rewrite

- Make targeted edits and move files with `git mv`; never regenerate files from scratch or retype them from memory.
- Template files arrive through `just new` or `just sync`.
- Never delete, skip, or loosen tests to pass; if a test must change, explain why in the commit message.

## C++ Memory Safety

- Immediately wrap owned C handles, allocations, descriptors, and temporary resources in RAII. Make owning wrappers noncopyable unless copying deliberately creates independent ownership; moves must leave the source safely destructible.
- Borrow views, pointers, and references only for the current call by default. Store owning values unless lifetime/invalidation guarantees are explicit. Deferred callbacks require explicit ownership-safe captures; no escaping reference captures without a demonstrated lifetime guarantee.
- Initialize scalar/pointer members at declaration and local values before use. Compiler zero-initialization is defense in depth, not permission for uninitialized reads.
- Check range/overflow before narrowing lengths/counts at C API boundaries; casts alone are not validation. Report failures through existing error mechanisms.
- Never let C++ exceptions escape C callbacks. Translate them into the C API's error/abort mechanism and propagate failures at the C++ boundary; do not silently swallow them.
- Preserve libc++ hardening, zero auto-initialization, ASan/UBSan with fatal reports, and required lint/tests. These checks do not prove memory safety.

## C++ style

- Favor modern C++26 and the standard library over legacy patterns; write for readability; comment what is not obvious and why.
- Prefer values over pointers, `const` by default, and no raw `new`/`delete`.
- Use `std::expected` for expected failures; use exceptions only at process and callback boundaries.
- Structure code with one folder per feature under `src/`, mirrored under `tests/`. Keep one test TU per executable (`tests/test_main.cpp`).
- Name what you use (`using slimr::Json;` or `slimr::Json`, never `using namespace`). Use PascalCase for types and camelCase for functions and variables (review guidance, not lint).
- Put file-local helpers in an unnamed namespace in `.cpp` files, or in `detail::` in headers.
- Follow KISS: inline single-use helpers, avoid speculative abstractions, keep diffs lean.
- Apply style rules to new and touched code; ports do not rewrite working code just for style.
- `.clang-tidy` enforces bug-finding checks (`clang-analyzer-core.*`, `clang-analyzer-cplusplus.*`, and bug-risk `bugprone-*` checks; pending legacy findings are temporarily disabled with per-check counts in `.clang-tidy`); fix findings or suppress inline with a reason.
- `INTERFACE_SOURCES` caveat: linking a header-only library that ships an implementation TU (such as `slimr-yaml`) into both a library and an executable that links that library compiles the TU twice and causes duplicate symbols.

## Testing

Iterate with `just test "<case>"` (Debug build with ASan + UBSan, `BUGSINK_DSN=""`), then finish with a full `just test`. `just build` compiles Release only and runs no tests.

## Recipes

- Core: `build`, `clean`, `configure`, `deps`, `doctor`, `fmt`, `lint`, `release`, `test [filter]`, `upgrade-deps`
- `homebrew-*`: `install`, `uninstall`
- `homebrew-service` and `homebrew-mcp`: `daemon-logs`, `daemon-restart`, `daemon-start`, `daemon-status`, `daemon-stop`
- `homebrew-mcp`: `test-mcp`
- Dropped tooling (do not re-add): foreground runner, local tap/overlay symlinking, GitHub-branch/local-publish/dry-run helpers, standalone formula check, and JS formula helpers

## Changelog

Add one line under `## Unreleased` in `CHANGELOG.md` for every change (use `Internal: ...` for internal changes).

## Paths, env vars, and README

- `<name>` in Homebrew paths is always the formula name, never a short XDG name: app data lives in `$(brew --prefix)/var/<name>`, config in `$(brew --prefix)/etc/<name>/`, and service secrets in `$(brew --prefix)/etc/<name>/env` (`chmod 600`, never in the public formula).
- Secrets are env vars only.
- Every `README.md` is end-user first: what it does, install, usage, env var table (name, required or optional, purpose), then links into `docs/` (`docs/development.md`, `docs/architecture.md`) for contributor workflow and architecture.

## Remote tests

Never run `test.yml` manually before `just release` (because `release` runs `test.yml` first). Run `test.yml` manually only when asked or to reproduce a Linux-only failure.
<!-- cpp-templates:end -->

## Documentation Upkeep

When a change conflicts with `README.md`, update the affected README and its
linked user/developer documentation in the same change. Keep examples,
configuration, install/build commands and compatibility claims consistent
with implemented behavior; do not claim unverified release or client support.

## Commands

| Task | Command |
| --- | --- |
| Run unit & pipeline tests (ASan + UBSan) | `just test` |
| Build release binary (`build/bin/personal-log-mcp`, no tests) | `just build` |
| Static analysis | `just lint` |
| Isolated MCP startup/discovery | `just test-mcp` |
| Check tools and toolchain | `just doctor` |
| Clean build artifacts | `just clean` |

## Module Boundaries

| Path | Owns | Must Not |
| --- | --- | --- |
| `src/main.cpp` | Entrypoint: `--log-file` parsing, `slimr::enable_http(..., {.port = 10002})`, then `personal_log::make_personal_log_app().run(argc, argv)` | Business logic |
| `src/log_file.hpp` | Verbatim template: `--log-file` redirection and 10 MiB rotation | Local edits |
| `src/app.hpp` | `make_personal_log_app()` registering all 13 tools (`add`, `get`, `search`, `update`, `merge`, `delete`, `sql`, `schema`, `runs`, `import`, `backup`, `mine`, `status`), resources, thread-safety annotations, lazy mutex-guarded DB initialization, and opt-in Bugsink reporting tagged `personal-log-mcp@<VERSION>` | Direct SQL or LLM logic |
| `src/db.hpp` | `AppDb` over `slimr::Database` (`PRAGMA user_version = 1`, WAL + `synchronous=NORMAL` for file DBs, per-DB mutex across transactions and LRU statement cache), FTS5 `ftsQuery`, date/tag normalization, CRUD, `merge`, read-only `sql` (`iterateLimited`), and `runs` tracking | Change schema v1 incompatibly with live `log.db` |
| `src/import.hpp` | `parseYearNote` parser for legacy Obsidian `YYYY.md` notes | Mutate vault files |
| `src/mine.hpp` | Scheduled mining pipeline (`runMine`), `ToolRunner`, LLM triage/draft/fallback (`modelFor`, `fetchWithRetry` via `cpp-httplib`), follow-ups (`addExpenseRows`, `replaceMedsSection`, Contacts, Kids Drive docs), PII scrubber (`scrub`), and `backupLog` / `prune` | Write source calendars or leak PII in Bugsink warnings |
| `src/mine_config.hpp` | Strict, lazy private JSON mining configuration and `$(brew --prefix)/etc/personal-log-mcp` paths | Embed private defaults or echo configuration values in errors |
| `src/docs_embed.hpp` | Embedded `docs/conventions.md` and `docs/recall.md` | — |
| `tests/` | `test_db.hpp` and `test_mine.hpp` using `TempDirGuard` SQLite DBs and mock `ToolRunner` | Touch the live `$(brew --prefix)/var/personal-log-mcp/log.db` or live APIs |

## Safety & App Conventions

1. **In-place database compatibility:** `src/db.hpp` opens `$(brew --prefix)/var/personal-log-mcp/log.db` (or `PERSONAL_LOG_DB`) at `PRAGMA user_version = 1`. Never alter existing table/column/trigger definitions incompatibly with the live database.
2. **Calendar isolation in `mine.hpp`:** Mining only writes suggested events to the private `AI-Suggests` calendar (`suggestCalendarId`, validated against `^[\w.@-]+$`). Never write source calendars; the suggestion destination must be explicitly configured and separate.
3. **PII Scrubbing:** All warnings and error messages sent to Bugsink from `mine.hpp` must pass through `warningCategory()` / `scrub()` so email addresses, phone numbers, URLs, and message bodies are never transmitted.
4. **Tests use temp DBs only:** Never run tests against the live database. Always use `:memory:` or `slimr::testing::TempDirGuard`, and isolate paths with `PERSONAL_LOG_DB`, `PERSONAL_LOG_MINE_CONFIG` and `HOMEBREW_PREFIX`.

## Mining Configuration

Mining requires a private JSON file (`PERSONAL_LOG_MINE_CONFIG` or `$(brew --prefix)/etc/personal-log-mcp/mine.json`). Load and validate it before opening the database or calling tools. Discovery and non-mining tools must work without it. Public tests use only `tests/fixtures/mine.json`. Optional private conventions and recall guides live in the same `etc` directory. Never commit user configuration or copied live records. Bugsink is disabled unless `BUGSINK_DSN` is explicitly set.
