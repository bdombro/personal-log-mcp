#pragma once

#include <string_view>

namespace personal_log {

inline constexpr std::string_view CONVENTIONS_MD_EMBEDDED = R"GUIDE(# Log conventions

The personal log is a SQLite database. Read with `search`, `get` and `sql`; write
with `add`, `update`, `merge` and `delete`. Do not create parallel markdown logs.

## Entry shape

- **date:** `YYYY.MM.DD`, or `YYYY.MM` when the day is unknown. Use the first day
  for multi-day events and explain the range in a detail.
- **title:** a short factual summary.
- **tags:** person or group tags, then one or two topic tags. Follow the user's
  existing vocabulary; ask before inventing new tags.
- **details:** concise facts and attributed, dated quotes. Mark inferences and
  uncertainty explicitly. Do not turn secondhand medical claims into diagnoses.
- **people:** names of people involved; use a full name or disambiguating context.
  Set `contact` to a known Google Contacts resource name, not a guessed identifier.
- **sources:** original `cal` event/series IDs, `gm` thread IDs and `wa` message
  IDs. Search the same date and event before adding older entries without sources.
- **links:** `email` for Gmail threads and `file` for relevant documents.

Represent a routine recurring series once, with its pattern, count and end date.
Log individual occurrences only when something notable happened. Update an
existing event rather than duplicating it; use `merge` for confirmed duplicates.

## What belongs here

Log meaningful dated events, appointments, trips, activities and milestones.
Keep current reference information in the user's notes or contacts instead.
Skip routine delivery notices, chores, payment reminders, ordinary work meetings,
automatically generated holidays, cancelled events and speculative future events.
Mark a future booking as booked rather than implying it already happened.

Never record passwords, access codes, payment details, private account identifiers,
or unnecessary sensitive information about third parties. Respect the user's
configured sources, destinations, retention policy and consent boundaries.

## Local customization

An optional private `conventions.md` in the user's `personal-log` XDG configuration
directory replaces this guide at resource-read time. Keep household-specific tags,
names and rules there, not in the published repository.
)GUIDE";

inline constexpr std::string_view RECALL_MD_EMBEDDED = R"GUIDE(# Recall

Answer personal-memory questions read-only. Do not edit records or send messages
unless explicitly instructed. Resolve an ambiguous person or thing before
searching for facts; never merge people on a first name alone.

## Choose the source

| Question | Start here |
| --- | --- |
| When an event happened | personal-log `search`, with dates, tags and person filters |
| Counts and frequency | personal-log `schema`, then read-only `sql` |
| Current reference information | The user's configured notes and contacts |
| Recent activity not yet logged | Authorized Gmail or WhatsApp sources |
| Open deadlines | The task list selected in the private mining configuration |
| A previously visited website | Browser history, if connected and authorized |

Entries include source IDs and links. Follow those to the original message or
event when the summary is insufficient. Search uses keywords: try synonyms and
the language likely used in the original message.

## Answering

- Answer first, then give supporting details.
- Cite the entry date, note or original source and date.
- Preserve uncertainty, surface conflicting evidence and identify stale records.
- Say what you did not find and where you looked. Never fill gaps with guesses.
- Format dates as `YYYY.MM.DD`.

An optional private `recall.md` in the user's `personal-log` XDG configuration
directory replaces this guide at resource-read time. Store private routing
instructions and note locations there rather than in public documentation.
)GUIDE";

} // namespace personal_log
