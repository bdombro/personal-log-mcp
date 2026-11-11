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

## Installation

- **Prebuilt release:** the checked-in [Homebrew formula](Formula/personal-log-mcp.rb)
  currently has a placeholder checksum. It is not a ready-to-install release.
  Check [releases](https://github.com/bdombro/personal-log-mcp/releases) for published
  binaries and a finalized formula before using a release installation command.
- **Build from source:** the current path is [Development](#development).
  Local slimr overlays are for development, not a runtime requirement.
- **Already installed:** follow the setup and service steps below. Installing a
  Cursor/Claude manifest only configures an endpoint; it does not install or
  start the native service.


## Configure the Log

The default database is `~/.local/share/personal-log/log.db`; use
`PERSONAL_LOG_DB` in the server environment to select another database.
Ordinary log search/edit tools do not require Google credentials or a mining
configuration. Existing data is opened in place; do not rename or replace
your data directory as part of installation.

Mining and Drive backup are opt-in and need additional private configuration,
credentials and companion tools. They are not part of the basic setup.


## Start and Manage the Service

For an already-installed Homebrew formula:

```bash
brew services start bdombro/personal-log-mcp/personal-log-mcp
brew services info personal-log-mcp
brew services restart bdombro/personal-log-mcp/personal-log-mcp
brew services stop bdombro/personal-log-mcp/personal-log-mcp
```

The endpoint is `http://127.0.0.1:8082/mcp`; logs are under
`/opt/homebrew/var/log/`. To run in the foreground instead, use
`personal-log-mcp http` (or `./build/bin/personal-log-mcp http` from a source checkout).
Do not run the foreground server and service on the same port.
Keep the default loopback bind; this is not a shared remote-host deployment.


## Connect Your Agent

After starting the service, add this server to OpenCode v2.2.6's
`opencode.jsonc` (merge it with any existing servers):

```json
{
  "mcp": {
    "servers": {
      "personal-log-mcp": {
        "type": "remote",
        "url": "http://127.0.0.1:8082/mcp",
        "protocol": "2026-07-28"
      }
    }
  }
}
```

Use either an explicit server entry or a plugin manifest, not both.
Alternative client schemas and stdio configuration are in the
[developer reference](docs/development.md). The compatibility notice above
applies to every transport.


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

## Optional Mining and Backup

Mining is disabled until explicitly configured. It can read Gmail, Calendar
and WhatsApp, call an LLM, and write approved log/task/contact/note destinations.
Use a separate private suggestion calendar; never write source calendars.
A mining dry run is not an offline check.

Follow the [mining and backup guide](docs/mining.md) and its private example
configuration. Ordinary log tools do not require that setup.


## Troubleshooting

- **Client cannot connect:** confirm it supports July MCP, the service is
  running, and the client URL matches the configured port. OpenCode needs
  `"protocol": "2026-07-28"`; old `initialize` clients will fail.
- **Port occupied:** inspect the listener and stop only the service/process
  you own, or deliberately choose another port and update the client URL.
- **Works in a terminal, not as a service:** launchd does not inherit
  `~/.zshrc`. Configure the service environment separately or use the
  plugin's supported private configuration file; inspect
  `/opt/homebrew/var/log/` for errors.
- **Mining configuration fails:** validate the private file against the example before invoking mining. A dry run still reads authorized sources and may call an LLM.
- **Source build cannot find vcpkg or slimr ports:** load the development
  environment, bootstrap vcpkg, and register both slimr links and the pinned
  libhv overlay. Never repair this by editing installed headers.
- **Compiler/hardening check fails:** upgrade the Xcode toolchain; set
  `CXX` before first configure. CMake caches the compiler, so an intentional
  compiler change needs a fresh configuration tree.


## Development

This section is for building or changing the server, not configuring an
installed binary. [Architecture, transport reference and test details](docs/development.md)
are separate from the user setup flow.

<details>
<summary>macOS source-build setup (vcpkg and local slimr overlays)</summary>

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

</details>

After the source build succeeds, stage and install the local Homebrew formula:

```bash
just install-local
```

Installing a formula and starting its service are separate actions.
For a source-only foreground run, use
`./build/bin/personal-log-mcp http`.
