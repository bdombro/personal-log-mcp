# Changelog

## Unreleased

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
