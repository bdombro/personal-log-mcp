# Optional Mining and Backup

## Scheduled Mining Pipeline (`src/mine.hpp`)

Mining is opt-in. Before running it, create a **private** configuration file:

1. Copy [`docs/mine.example.json`](mine.example.json) to
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
