# Changelog

## Unreleased

- Internal: align build, CI, Homebrew formula, and documentation with cpp-templates
- HTTP accepts any --host and an ephemeral --port 0 (slimr-mcp update)
- Add `--log-file` redirection and 10 MiB log rotation for the HTTP service
- Move default database from ~/.local/share/personal-log to $(brew --prefix)/var/personal-log-mcp (mode 0700)
