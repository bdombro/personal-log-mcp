# personal-log-mcp: Contributing

User installation, configuration and compatibility status are in the
[README](../README.md). This reference preserves implementation details,
alternative transport schemas and validation commands. A client configuration
example is not a claim of verified July/OAuth interoperability.

<!-- cpp-templates:begin -->
## Contents

- [Develop and test](#develop-and-test)
- [Unreleased libraries](#unreleased-libraries)
- [Rules](#rules)
- [PR, CI, and release flow](#pr-ci-and-release-flow)
- [Maintainer setup](#maintainer-setup)
- [Dependency bumps](#dependency-bumps)

## Develop and test

```bash
just doctor
just test
just lint
```

Iterate on a single doctest case with `just test "<case>"` (Debug build with ASan + UBSan, `BUGSINK_DSN=""`). For `homebrew-mcp` repos, run `just test-mcp` as well.

## Unreleased libraries

vcpkg reads extra port dirs from `VCPKG_OVERLAY_PORTS`, and an overlay wins over the registry. Point it at a dir of symlinks, one per unreleased lib; manifests never change:

```sh
mkdir -p ~/.vcpkg/overlays/dev
ln -s ~/dev/slimr/<lib>/ports/slimr-<lib> ~/.vcpkg/overlays/dev/slimr-<lib>
export VCPKG_OVERLAY_PORTS=~/.vcpkg/overlays/dev
just test
```

Remove the symlink (or `unset VCPKG_OVERLAY_PORTS`) to build against the released lib again. Keep the export out of shell startup files.

## Rules

- Never push directly to `main`; all changes land through squash-merged pull requests.
- PRs touch no release files (`VERSION`, `CHANGELOG.md`, `versions/`, `Formula/*.rb`, `Casks/*.rb`).
- Write the PR title (plus optional body) as the changelog entry; squash-merge turns it into one commit on `main`.
- Sanitize everything (no secrets, tokens, DSNs, or personal data in commits, logs, or docs).
- Run `just test` (ASan + UBSan) and `just lint` before opening or updating a PR.

## PR, CI, and release flow

- Every PR runs `test.yml`: a `gate` job skips macOS/Linux builds on docs-only PRs, and a single `ci` summary job gates merge.
- Merging to `main` triggers `release.yml` (one release at a time). It collects squash commits since the last tag (skipping docs-only ranges), picks the bump from merged PR labels (`patch` default, or `release:minor` / `release:major`; agents must ask before adding labels), writes `VERSION`/`vcpkg.json`, `CHANGELOG.md` (and `vcpkg x-add-version` for libraries), commits as the GitHub App, tags `vX.Y.Z`, and publishes the GitHub Release (plus assets and `Formula/*.rb` / `Casks/*.rb` updates).
- Pre-releases are a known option (tag/channel), not currently built.

## Maintainer setup

Direct pushes to `main` are blocked; `release.yml` uses a dedicated GitHub App so it can commit release metadata and formula updates without giving humans or agents an admin bypass.

### 1. Create and install the release GitHub App

Create one GitHub App (Settings -> Developer settings -> GitHub Apps):

- **Homepage URL:** any URL (for example, the owner's GitHub profile)
- **Callback / Setup URLs:** blank; **Webhook Active:** unchecked
- **Repository permissions:** `Contents: Read and write`, `Metadata: Read-only`
- **Where can this GitHub App be installed?:** Any account

Record `<app-id>`, generate a private key, and save it locally if needed for manual recovery (it can always be regenerated):

```sh
chmod 600 ~/.ssh/<app-name>.private-key.pem
```

Install the App on each owner (org or user) and select the target repositories.

### 2. Store Actions secrets

Org-level (requires org owner and `admin:org` scope):

```sh
gh auth refresh -h github.com -s admin:org
gh secret set RELEASE_APP_ID --org <owner> --visibility all --body "<app-id>"
gh secret set RELEASE_APP_PRIVATE_KEY --org <owner> --visibility all < ~/.ssh/<app-name>.private-key.pem
gh secret list --org <owner>
```

Per-repo (for user-owned repos or selected repos):

```sh
gh secret set RELEASE_APP_ID -R <owner>/<repo> --body "<app-id>"
gh secret set RELEASE_APP_PRIVATE_KEY -R <owner>/<repo> < ~/.ssh/<app-name>.private-key.pem
gh secret list -R <owner>/<repo>
```

`gh secret list` prints secret names only.

### 3. Protect `main`

Create (or `PUT /repos/<owner>/<repo>/rulesets/<ruleset-id>` to update) the `main` ruleset with App-only bypass, required `ci` check, squash-only merges, and no force push or deletion, then enforce squash-only merges on the repo:

```sh
gh api -X POST /repos/<owner>/<repo>/rulesets --input - <<'JSON'
{
  "name": "main",
  "target": "branch",
  "enforcement": "active",
  "conditions": {
    "ref_name": { "include": ["~DEFAULT_BRANCH"], "exclude": [] }
  },
  "bypass_actors": [
    { "actor_id": <app-id>, "actor_type": "Integration", "bypass_mode": "always" }
  ],
  "rules": [
    { "type": "deletion" },
    { "type": "non_fast_forward" },
    {
      "type": "pull_request",
      "parameters": {
        "required_approving_review_count": 0,
        "dismiss_stale_reviews_on_push": false,
        "require_code_owner_review": false,
        "require_last_push_approval": false,
        "required_review_thread_resolution": false,
        "allowed_merge_methods": ["squash"]
      }
    },
    {
      "type": "required_status_checks",
      "parameters": {
        "strict_required_status_checks_policy": false,
        "required_status_checks": [{ "context": "ci" }]
      }
    }
  ]
}
JSON

gh api -X PATCH /repos/<owner>/<repo> \
  -F allow_squash_merge=true \
  -F allow_merge_commit=false \
  -F allow_rebase_merge=false

gh api /repos/<owner>/<repo>/rulesets
```

### 4. Act as the App (emergency fix or manual release step)

Mint a short-lived JWT (`RS256`, `iat` = now - 60s, `exp` = now + 540s, `iss` = `<app-id>`), exchange it for a 1-hour installation token, and keep all credentials in shell variables only (never echo, log, or commit them):

```sh
app_id=<app-id>
pem=~/.ssh/<app-name>.private-key.pem
b64url() { openssl base64 -e -A | tr '+/' '-_' | tr -d '='; }
now=$(date +%s)
header=$(printf '{"alg":"RS256","typ":"JWT"}' | b64url)
payload=$(printf '{"iat":%d,"exp":%d,"iss":"%s"}' $((now - 60)) $((now + 540)) "$app_id" | b64url)
sig=$(printf '%s.%s' "$header" "$payload" | openssl dgst -sha256 -sign "$pem" | b64url)
jwt="${header}.${payload}.${sig}"

inst_id=$(curl -fsSL -H "Authorization: Bearer $jwt" -H "Accept: application/vnd.github+json" \
  "https://api.github.com/repos/<owner>/<repo>/installation" | jq -r .id)
token=$(curl -fsSL -X POST -H "Authorization: Bearer $jwt" -H "Accept: application/vnd.github+json" \
  "https://api.github.com/app/installations/${inst_id}/access_tokens" | jq -r .token)
```

Use `$token` with `gh` or `git push`, committing as the App's bot identity (`gh api /users/<app-slug>%5Bbot%5D --jq .id` gives `<bot-user-id>`):

```sh
git -c user.name="<app-slug>[bot]" \
    -c user.email="<bot-user-id>+<app-slug>[bot]@users.noreply.github.com" \
    commit -m "fix: ..."
git push "https://x-access-token:${token}@github.com/<owner>/<repo>.git" HEAD:main
GH_TOKEN="$token" gh release create ...
```

Tokens expire after 1 hour; revoke early when done:

```sh
curl -fsSL -X DELETE -H "Authorization: Bearer $token" -H "Accept: application/vnd.github+json" \
  https://api.github.com/installation/token
unset jwt token
```

## Dependency bumps

Bump a library baseline in `vcpkg-configuration.json` only when a bug fix, new API, or security fix requires it; otherwise wait for the next `just upgrade-deps`.
<!-- cpp-templates:end -->

## Repository tips

### App Architecture

The standalone Streamable HTTP MCP, JSON CLI, and scheduled mining command are implemented in C++26 on the `slimr-*` libraries (`slimr-mcp`, `slimr-sqlite`); data lives in the SQLite + FTS5 database.

```text
personal-log-mcp/
├── Formula/
│   └── personal-log-mcp.rb   # Homebrew formula (prebuilt macOS/Linux binaries) + HTTP service definition
├── .claude-plugin/           # Claude Code plugin & marketplace manifests (HTTP 127.0.0.1:10002)
├── .cursor-plugin/           # Cursor plugin manifest (HTTP 127.0.0.1:10002)
├── VERSION                   # Release version compiled into the binary
├── CHANGELOG.md              # Unreleased and released changes
├── vcpkg.json                # Package dependencies installed under build/deps/
├── vcpkg-configuration.json  # Builtin + slimr-* git registries (baselines moved by `just upgrade-deps`)
├── triplets/                 # vcpkg overlay triplets (macOS 15.0 floor, Linux libc++)
├── CMakeLists.txt            # App/test targets, imported dependencies, safety settings
├── justfile                  # cpp-templates recipes: build, test, lint, test-mcp, release, Homebrew service lifecycle
├── README.md                 # User & operator documentation
├── AGENTS.md                 # Maintainer & AI agent conventions
├── docs/
│   ├── conventions.md        # Log entry & tag conventions (`personal-log://conventions`)
│   └── recall.md             # Multi-source recall playbook (`personal-log://recall`)
├── scripts/
│   ├── doctor.sh             # Toolchain and required-tool checks (`just doctor`)
│   ├── formula-shared.ts     # Formula rendering shared by release tooling
│   ├── release.ts            # Release workflow helper (`just release`)
│   └── test-mcp.ts           # July 2026-07-28 ephemeral HTTP smoke test
├── src/
│   ├── db.hpp                # `AppDb` over `sqlite.hpp`: schema v1, WAL, per-DB mutex, FTS5 `entries_fts`, CRUD, merge, read-only `sql`, online `backup`
│   ├── import.hpp            # `parseYearNote` parser for legacy Obsidian `YYYY.md` yearly notes
│   ├── mine.hpp              # Scheduled mining pipeline (Gmail, Calendar, WhatsApp -> log, Deadlines, AI-Suggests, Meds, Contacts, Expenses) & Drive `backupLog`
│   ├── mine_config.hpp       # Strict lazy loader for `PERSONAL_LOG_MINE_CONFIG` / `$(brew --prefix)/etc/personal-log-mcp/mine.json`
│   ├── docs_embed.hpp        # Compile-time embedded `conventions.md` and `recall.md` resources
│   ├── app.hpp               # `personal_log::make_personal_log_app()` registering all 13 tools, resources, concurrency annotations, and opt-in error reporting
│   ├── log_file.hpp          # `--log-file` redirection and 10 MiB rotation (verbatim template file)
│   └── main.cpp              # Standalone CLI and Streamable HTTP (`http` 127.0.0.1:10002, `--log-file`) entrypoint
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

- **`<mcp.hpp>`, `<mcp_http.hpp>`, `<reflect.hpp>`:** Typed MCP tool definitions, compile-time JSON Schema generation, embedded resources (`personal-log://recall`, `personal-log://conventions`), narrow JSON CLI, and Streamable HTTP server (`cpp-httplib`).
- **`<sqlite.hpp>` (`-lsqlite3`):** WAL mode + `synchronous=NORMAL` (for file-backed DBs), per-`AppDb` mutex across transactions and 256-statement LRU cache, FTS5 (`entries_fts`), nested savepoints (`merge`), `iterateLimited` (`SQLITE_OPEN_READONLY` + `PRAGMA query_only = ON` for `sql`), and online `sqlite3_backup_init` (`backup`).
- **`<httplib.h>` (`cpp-httplib[openssl]`):** HTTP client with retry/backoff (`fetchWithRetry`).
- **`<json.hpp>` (`glaze`):** Fast JSON serialization and LLM structured output parsing.
- **`<bugsink.hpp>` (`cpp-httplib[openssl]`):** Automatic error and fallback warning reporting to an explicitly configured Bugsink endpoint (with PII scrubbed via `scrub()`).
- **`<test.hpp>` (`doctest`):** Full unit and pipeline test suite compiled with `-fsanitize=address,undefined`.

## Transports & Client Configuration

`personal-log-mcp` is a standalone C++26 binary exposing two invocation modes:

- **Streamable HTTP (`http`):** `personal-log-mcp http` (binds `127.0.0.1:10002`, endpoint `http://127.0.0.1:10002/mcp`; `--host <addr>` and `--port <n>` override, `--port 0` picks an ephemeral port; `--log-file <path>` redirects output with 10 MiB rotation).
- **Direct JSON CLI:** `personal-log-mcp <tool> ['{"arg":"val"}']`.

> **Breaking MCP `2026-07-28` contract:** Streamable HTTP (`http`) implements MCP protocol version `2026-07-28` (`server/discover` with `params._meta` and HTTP `MCP-Protocol-Version` / `Mcp-Method` / `Mcp-Name` headers). Legacy `initialize` is removed and returns `-32601` (HTTP `404`).

### Streamable HTTP (Recommended)

Run via Homebrew (`just install`, which also starts the service) or directly with `personal-log-mcp http`.

**Claude Code (`.mcp.json` or `.claude-plugin/plugin.json`):**

```json
{
  "mcpServers": {
    "personal-log-mcp": {
      "type": "http",
      "url": "http://127.0.0.1:10002/mcp"
    }
  }
}
```

**Cursor (`.cursor/mcp.json` or `.cursor-plugin/plugin.json`):**

```json
{
  "mcpServers": {
    "personal-log-mcp": {
      "url": "http://127.0.0.1:10002/mcp"
    }
  }
}
```

### Homebrew Service Lifecycle

`Formula/personal-log-mcp.rb` installs the prebuilt `personal-log-mcp` binary and configures a `brew services` daemon (`/bin/sh -c` sourcing `$(brew --prefix)/etc/personal-log-mcp/env`, then `http --log-file $(brew --prefix)/var/log/personal-log-mcp.log`, `keep_alive true`, working dir `$(brew --prefix)/var/personal-log-mcp`):

```bash
just install          # Clean-install the released formula from GitHub, brew test it, start the service
just daemon-start     # Start the brew service and wait for server/discover
just daemon-stop      # Stop the brew service
just daemon-restart   # Restart the brew service and wait for readiness
just daemon-status    # Inspect brew service status and probe server/discover
just daemon-logs      # Follow the service log
just uninstall        # Stop the service, uninstall the formula, remove the tap
```

## Building & Testing

Build with Clang/C++26 and libc++ FAST hardening, CMake 3.31+, Ninja, [just](https://just.systems), [vcpkg](https://vcpkg.io) and Homebrew `llvm@23` (clang-format, clang-tidy). `just doctor` checks the toolchain and prints fixes. Set `VCPKG_ROOT` to a bootstrapped checkout and put `vcpkg` on `PATH`; `slimr-*` packages resolve from their pinned git registries in `vcpkg-configuration.json`, and `just upgrade-deps` moves every baseline at once.

`just configure`, `test`, `build`, and `lint` automatically run `deps`, installing manifest dependencies under `build/deps/`. CMake uses installed `slimr::mcp_http` (`slimr-mcp[http]`) and `slimr::sqlite` targets with static SQLite/FTS5; manifest auto-install is disabled.

Debug sanitizers and Release IPO builds have separate `build/cmake/Debug/` and `build/cmake/Release/` graphs. Both enable libc++ hardening and zero automatic-variable initialization; Debug adds ASan and fatal UBSan. macOS builds use `llvm@23` clang++ with static `libc++` and a 15.0 deployment floor; Linux builds use `llvm@23` clang++ with `libc++`. `CXX` overrides both; set it before the first configure, since CMake caches the compiler. Set `LLVM_BIN` to override the `llvm@23` binary directory for `clang-format` and `clang-tidy`. CTest runs only synthetic/mock suites with a finite timeout and isolated repo-local HOME, `HOMEBREW_PREFIX` and temporary directories.

```bash
just doctor   # Check required tools and C++26 support
just test     # Run DB, FTS5, import, HTTP, concurrency, log-file, and mock mining tests with ASan + UBSan
just test "<case>"  # Run only matching doctest cases (Debug, BUGSINK_DSN="")
just build    # Build the Release binary at build/bin/personal-log-mcp (no tests)
just lint     # clang-format check and clang-tidy bug-finding checks
just test-mcp # Verify July 2026-07-28 ephemeral HTTP discovery and tools/list
```

`just test` with no filter runs CTest, which points HOME, `HOMEBREW_PREFIX` and `TMPDIR` at scratch paths and `PERSONAL_LOG_MINE_CONFIG` at the test fixture. `just test "<case>"` runs the test binary directly with only `BUGSINK_DSN=` set, so path tests isolate their own env. `just test-mcp` runs the server with an isolated environment, no external CLI PATH or credentials, and Bugsink disabled; it never invokes mining, cloud, or database tools.

Run the direct binary on a separate dev port so it doesn't collide with the service:

```bash
PERSONAL_LOG_DB="$PWD/build/dev.db" ./build/bin/personal-log-mcp http --port 20002
```

On Linux (`ubuntu-24.04`), install `llvm@23` via Homebrew and enable lingering so `brew services` systemd user units survive logout (`loginctl enable-linger "$USER"`).
