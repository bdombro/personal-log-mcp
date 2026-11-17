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

> **Client compatibility:** MCP **2026-07-28 only**; legacy clients cannot connect.
> The workspace has been exercised with OpenCode v2.2.6 using explicit July
> configuration. Cursor's July support is deferred; Claude Code July runtime
> acceptance is not yet verified. Shipping a plugin manifest is not proof of
> client compatibility.

## Try It

After setup, ask your agent:

> When was the boiler last serviced? Include the original source.

## Features

- Search and retrieve dated entries, inspect mining runs, and run bounded read-only SQL.
- Add, update, merge, or import entries; use embedded recall and convention resources.
- Run the scheduled mining pipeline and create Drive backups.
- Small footprint: a standalone binary and SQLite database, with no Node/V8 MCP runtime.

## Install

```bash
brew tap bdombro/personal-log-mcp https://github.com/bdombro/personal-log-mcp
brew install bdombro/personal-log-mcp/personal-log-mcp
```

Ordinary log search/edit tools need no Google credentials or mining
configuration. Mining and Drive backup are opt-in; see
[Optional Mining and Backup](#optional-mining-and-backup).

## Usage

Start the Homebrew service:

```bash
brew services start personal-log-mcp
brew services info personal-log-mcp
```

The HTTP server binds `127.0.0.1:10002` (`http://127.0.0.1:10002/mcp`;
pass `--host 0.0.0.0` to expose) and logs to
`$(brew --prefix)/var/log/personal-log-mcp.log` (rotated at 10 MiB, keeping one
`.1`). The database defaults to `$(brew --prefix)/var/personal-log-mcp/log.db`
(directory mode `0700`); set `PERSONAL_LOG_DB` to use another file. Existing
data is opened in place, with no migration.

Add this server to OpenCode v2.2.6's `opencode.jsonc` (merge it with any
existing servers):

```json
{
  "mcp": {
    "servers": {
      "personal-log-mcp": {
        "type": "remote",
        "url": "http://127.0.0.1:10002/mcp",
        "protocol": "2026-07-28"
      }
    }
  }
}
```

Use either an explicit server entry or a plugin manifest, not both.
Alternative client schemas are in the
[contributing guide](docs/contributing.md), or run the direct CLI
(`personal-log-mcp <tool> ['{"arg":"val"}']`).

## Tools & CLI Commands

Every tool is exposed over Streamable HTTP (`personal-log-mcp http` at `http://127.0.0.1:10002/mcp`) and the CLI (`personal-log-mcp <tool> ['{"arg":"val"}']`). Existing `personal-log://` resource URIs are unchanged.

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
personal-log-mcp search '{"query":"boiler service","from":"2025-01-01","tags":["home"]}'

# Add an entry
personal-log-mcp add '{"date":"2026-11-10","summary":"Ordered replacement air filter","tags":["home","maintenance"]}'

# Run a read-only SQL aggregation
personal-log-mcp sql '{"sql":"SELECT tag, COUNT(*) AS n FROM entry_tags GROUP BY tag ORDER BY n DESC LIMIT 10"}'

# Inspect recent mining runs
personal-log-mcp runs '{"limit":5}'

# Dry-run the mining job
personal-log-mcp mine '{"dryRun":true}'

# Online backup to Google Drive
personal-log-mcp backup
```

## Optional Mining and Backup

Mining is disabled until explicitly configured. It can read Gmail, Calendar
and WhatsApp, call an LLM, and write approved log/task/contact/note destinations.
Use a separate private suggestion calendar; never write source calendars.
A mining dry run is not an offline check.

When `schedule` is configured and enabled in `mine.json`, the HTTP server runs
an internal daily timer (`onServe`) with startup missed-run catch-up (via `runs`)
and optional post-mine `backup`, without power assertions or `caffeinate`.

Follow the [mining and backup guide](docs/mining.md) and its private example
configuration. Ordinary log tools do not require that setup.


## Environment Variables

Configure service environment variables in
`$(brew --prefix)/etc/personal-log-mcp/env` (`chmod 600`, `KEY=VALUE`):

| Name | Required | Default | Purpose |
| --- | --- | --- | --- |
| `PERSONAL_LOG_DB` | Optional | `$(brew --prefix)/var/personal-log-mcp/log.db` | SQLite + FTS5 log database path |
| `PERSONAL_LOG_MINE_CONFIG` | Optional | `$(brew --prefix)/etc/personal-log-mcp/mine.json` | Private mining configuration (needed only for `mine`) |
| `PERSONAL_LOG_SUGGEST_CALENDAR_ID` | Optional | configured `suggestCalendarId` | Private suggestion calendar override |
| `PERSONAL_LOG_BACKUP_FOLDER` | Optional | `Backups/personal-log` | Drive folder for `backup` |
| `ASSISTANT_LLM_DEFAULT` | Optional | `deepseek-v4-pro` | Mining drafting model |
| `ASSISTANT_LLM_FAST` | Optional | `deepseek-flash` | Mining triage model |
| `ASSISTANT_LLM_DEFAULT_FALLBACK` | Optional | `deepseek-v4-pro` | Fallback drafting model (`none` disables) |
| `ASSISTANT_LLM_FAST_FALLBACK` | Optional | `deepseek-flash` | Fallback triage model (`none` disables) |
| `BUGSINK_DSN` | Optional | unset (disabled) | Error reporting DSN (`""` disables reporting) |
| `HOMEBREW_PREFIX` | Optional | `brew --prefix` | Overrides the prefix used for the default `var` and `etc` paths |

## Troubleshooting

- **Client cannot connect:** confirm it supports July MCP, the service is
  running, and the client URL matches the configured port. OpenCode needs
  `"protocol": "2026-07-28"`; old `initialize` clients will fail.
- **Port occupied:** inspect the listener and stop only the service/process
  you own, or deliberately choose another port and update the client URL.
- **Works in a terminal, not as a service:** the service does not inherit
  `~/.zshrc`. Put service settings in `$(brew --prefix)/etc/personal-log-mcp/env`
  and inspect `$(brew --prefix)/var/log/personal-log-mcp.log` for errors.
- **Mining configuration fails:** validate the private file against the example before invoking mining. A dry run still reads authorized sources and may call an LLM.

## Development

See [docs/contributing.md](docs/contributing.md) for local builds, testing, and releases.
