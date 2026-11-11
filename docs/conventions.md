# Log conventions

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
