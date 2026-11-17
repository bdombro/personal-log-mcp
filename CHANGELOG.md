# Changelog

## Unreleased

## v3.1.2 - 2026-11-17

- feat: adopt cpp-templates alignment, internal scheduler, and /tools HTTP surface (#1)
  * feat: use VCPKG_OVERLAY_PORTS and guard release
  * chore: adopt cpp-templates CI/CD and contributing docs
  * docs: maintainer setup for release App, secrets, rulesets
  * docs: soft alignment marker and release-bot contributing docs
  * feat: add internal onServe mining/backup scheduler with catch-up
  * feat: server-only CLI, /tools surface, single-flight mine/backup with progress
  * chore: bump slimr-mcp and slimr-serve vcpkg baselines
- chore: slow gh run watch polling to 30s

- Drop per-tool CLI invocation in favor of server-only `http` mode with default-on `/tools/<name>` HTTP endpoints
- Make `mine` and `backup` single-flight (`threadSafe`, HTTP `409` `already_running` on concurrent calls) and emit `mine` stage progress (`collect`, `triage`, `draft`, `apply`, `followUps`, `done`) over NDJSON `/tools/mine` and MCP `notifications/progress`
- Add internal `onServe` daily mining/backup scheduler with startup missed-run catch-up and `mine.json` `schedule` config (no power assertion or `caffeinate`)

## v3.1.1 - 2026-11-17

- Pin slimr-serve v0.3.0 (OpenAPI 3.1 `GET /openapi.json`, `--openapi` CLI dump, and default `127.0.0.1` bind) and slimr-mcp v0.2.1
- Internal: verify `GET /openapi.json` in `scripts/test-mcp.ts` and document `overlay-ports` dev workflow

## v3.1.0 - 2026-11-16

- Internal: remove temporary vcpkg overlay-ports and pin slimr-mcp v0.2.0 and slimr-serve v0.1.1
- Migrate to `slimr-serve` typed tool outputs (`outputSchema`/`structuredContent` on `backup` and `mine`) and drop stdio `mcp` mode
- Re-enable direct CLI tool calls (`personal-log-mcp <tool> '{...}'`), now opt-in since `slimr-mcp` v0.1.4
- Internal: replace `slimr-curl`/`libhv` with `cpp-httplib` and upgrade `slimr-mcp` / `slimr-bugsink`
- Internal: switch macOS builds to llvm@23 and static libc++

## v3.0.0 - 2026-11-16

- Internal: align build, CI, Homebrew formula, and documentation with cpp-templates
- HTTP accepts any --host and an ephemeral --port 0 (slimr-mcp update)
- Add `--log-file` redirection and 10 MiB log rotation for the HTTP service
- Move default database from ~/.local/share/personal-log to $(brew --prefix)/var/personal-log-mcp (mode 0700)
- Read mining config and private guides from $(brew --prefix)/etc/personal-log-mcp instead of ~/.config/personal-log
- Bind HTTP server to 0.0.0.0:10002 by default (pass --host 127.0.0.1 for loopback-only)
- Embed VERSION into binary and tag Bugsink events with personal-log-mcp@<version>
- Internal: resolve slimr registries from the `slimr-down-now` org
