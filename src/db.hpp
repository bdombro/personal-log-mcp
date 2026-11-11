#pragma once

#include "json.hpp"
#include "sqlite.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace personal_log {

// ============================================================================
// Data Types
// ============================================================================

/// A person on an entry.
struct Person {
    std::string name;
    std::optional<std::string> contact = std::nullopt;

    bool operator==(const Person&) const = default;
};

/// Where an entry came from; (kind, id) is unique across the log.
struct Source {
    std::string kind; // "cal", "gm", "wa"
    std::string id;

    bool operator==(const Source&) const = default;
};

/// A link attached to an entry.
struct Link {
    std::string kind; // "email", "file", "link"
    std::string label;
    std::string url;

    bool operator==(const Link&) const = default;
};

/// Fields for a new entry.
struct NewEntry {
    std::string date;
    std::string title;
    std::vector<std::string> tags;
    std::vector<std::string> details;
    std::vector<Person> people;
    std::vector<Source> sources;
    std::vector<Link> links;

    bool operator==(const NewEntry&) const = default;
};

/// A stored entry.
struct Entry {
    int64_t id = 0;
    std::string date;
    std::string title;
    std::vector<std::string> tags;
    std::vector<std::string> details;
    std::vector<Person> people;
    std::vector<Source> sources;
    std::vector<Link> links;

    bool operator==(const Entry&) const = default;
};

/// An entry skipped during add because one of its sources is already logged.
struct SkippedEntry {
    int64_t index = 0;
    std::string title;
    int64_t existingId = 0;
    std::string source;

    bool operator==(const SkippedEntry&) const = default;
};

/// Result of adding a batch of entries.
struct AddResult {
    std::vector<int64_t> added;
    std::vector<SkippedEntry> skipped;

    bool operator==(const AddResult&) const = default;
};

/// Result of addKeepingEntry.
struct AddKeepingResult {
    int64_t id = 0;
    std::vector<std::string> droppedSources;

    bool operator==(const AddKeepingResult&) const = default;
};

/// Filters for search.
struct SearchFilters {
    std::optional<std::string> query = std::nullopt;
    std::optional<std::string> from = std::nullopt;
    std::optional<std::string> to = std::nullopt;
    std::vector<std::string> tags;
    std::optional<std::string> person = std::nullopt;
    std::optional<int64_t> limit = std::nullopt;

    bool operator==(const SearchFilters&) const = default;
};

/// Changes for update.
struct EntryPatch {
    std::optional<std::string> date = std::nullopt;
    std::optional<std::string> title = std::nullopt;
    std::optional<std::vector<std::string>> details = std::nullopt;
    std::vector<std::string> addTags;
    std::vector<std::string> removeTags;
    std::vector<Person> addPeople;
    std::vector<std::string> removePeople;
    std::vector<Source> addSources;
    std::vector<Source> removeSources;
    std::vector<Link> addLinks;
    std::vector<std::string> removeLinks;

    bool operator==(const EntryPatch&) const = default;
};

/// One mining run.
struct Run {
    int64_t id = 0;
    std::string startedAt;
    std::optional<std::string> finishedAt = std::nullopt;
    std::string since;
    std::string until;
    std::string status; // "running" | "ok" | "error"
    slimr::Json stats = slimr::Json::object_t{};
    std::optional<std::string> error = std::nullopt;

    bool operator==(const Run& o) const {
        return id == o.id && startedAt == o.startedAt && finishedAt == o.finishedAt &&
               since == o.since && until == o.until && status == o.status &&
               error == o.error && slimr::write_json(stats) == slimr::write_json(o.stats);
    }
};

/// SQL result row set.
struct SqlResult {
    std::vector<slimr::Json> rows;
    bool truncated = false;

    bool operator==(const SqlResult&) const = default;
};

// ============================================================================
// Helpers & Normalizers
// ============================================================================

/// Normalizes dates: converts ISO dashes to dots (e.g. 2026-11-04 -> 2026.11.04).
inline std::string normalizeDate(std::string_view date) {
    std::string s(date);
    for (char& c : s) {
        if (c == '-') c = '.';
    }
    return s;
}

/// Normalizes tags: removes leading '#' and lowercases (e.g. #Health/Meds -> health/meds).
inline std::string normalizeTag(std::string_view tag) {
    if (tag.starts_with('#')) {
        tag.remove_prefix(1);
    }
    std::string s(tag);
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

/// ISO timestamp string for current UTC instant.
inline std::string format_iso_now() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return std::string(buf);
}

/// Default database path: $PERSONAL_LOG_DB or ~/.local/share/personal-log/log.db.
inline std::string defaultDbPath() {
    const char* env = std::getenv("PERSONAL_LOG_DB");
    if (env && *env) return std::string(env);
    const char* home = std::getenv("HOME");
    if (!home || !*home) return "log.db";
    return std::string(home) + "/.local/share/personal-log/log.db";
}

/// Turns free text into a safe FTS5 query: words ANDed, "phrases" kept, OR kept, trailing * kept.
inline std::string ftsQuery(std::string_view text) {
    std::vector<std::string> tokens;
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) {
            ++i;
        }
        if (i >= text.size()) break;

        if (text[i] == '"') {
            size_t j = text.find('"', i + 1);
            if (j != std::string_view::npos) {
                tokens.emplace_back(text.substr(i, j - i + 1));
                i = j + 1;
                continue;
            }
        }

        size_t j = i;
        while (j < text.size() && !std::isspace(static_cast<unsigned char>(text[j]))) {
            ++j;
        }
        tokens.emplace_back(text.substr(i, j - i));
        i = j;
    }

    std::vector<std::string> parts;
    for (const auto& token : tokens) {
        if (token == "OR") {
            parts.push_back("OR");
            continue;
        }
        if (token.starts_with('"') && token.ends_with('"') && token.size() >= 2) {
            std::string phrase;
            for (size_t k = 1; k + 1 < token.size(); ++k) {
                if (token[k] != '"') {
                    phrase += token[k];
                }
            }
            size_t start = 0;
            while (start < phrase.size() && std::isspace(static_cast<unsigned char>(phrase[start]))) ++start;
            size_t end = phrase.size();
            while (end > start && std::isspace(static_cast<unsigned char>(phrase[end - 1]))) --end;
            std::string trimmed = phrase.substr(start, end - start);
            if (!trimmed.empty()) {
                parts.push_back("\"" + trimmed + "\"");
            }
            continue;
        }

        bool prefix = token.ends_with('*');
        std::string word;
        for (char c : token) {
            if (c != '"') word += c;
        }
        while (!word.empty() && word.back() == '*') {
            word.pop_back();
        }
        if (!word.empty()) {
            parts.push_back("\"" + word + "\"" + (prefix ? "*" : ""));
        }
    }

    while (!parts.empty() && parts.front() == "OR") {
        parts.erase(parts.begin());
    }
    while (!parts.empty() && parts.back() == "OR") {
        parts.pop_back();
    }

    std::string out;
    for (size_t k = 0; k < parts.size(); ++k) {
        if (k > 0) out += " ";
        out += parts[k];
    }
    return out;
}

/// Checks if SQL query starts with SELECT or WITH (case-insensitive, ignoring leading whitespace).
inline bool is_select_or_with(std::string_view query) {
    size_t i = 0;
    while (i < query.size() && std::isspace(static_cast<unsigned char>(query[i]))) {
        ++i;
    }
    std::string_view remaining = query.substr(i);
    auto starts_with_ci_word = [](std::string_view s, std::string_view prefix) {
        if (s.size() < prefix.size()) return false;
        for (size_t k = 0; k < prefix.size(); ++k) {
            if (std::tolower(static_cast<unsigned char>(s[k])) != std::tolower(static_cast<unsigned char>(prefix[k]))) {
                return false;
            }
        }
        if (s.size() > prefix.size() && (std::isalnum(static_cast<unsigned char>(s[prefix.size()])) || s[prefix.size()] == '_')) {
            return false;
        }
        return true;
    };
    return starts_with_ci_word(remaining, "select") || starts_with_ci_word(remaining, "with");
}

// ============================================================================
// Schema Migration V1
// ============================================================================

inline const std::vector<std::string> LOG_MIGRATIONS = {
    R"sql(
    CREATE TABLE entries (
      id INTEGER PRIMARY KEY,
      date TEXT NOT NULL,
      title TEXT NOT NULL,
      details TEXT NOT NULL DEFAULT '[]',
      created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now')),
      updated_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%SZ', 'now'))
    );
    CREATE INDEX entries_date ON entries(date);

    CREATE TABLE entry_tags (
      entry_id INTEGER NOT NULL REFERENCES entries(id) ON DELETE CASCADE,
      tag TEXT NOT NULL,
      PRIMARY KEY (entry_id, tag)
    );
    CREATE INDEX entry_tags_tag ON entry_tags(tag);

    CREATE TABLE entry_people (
      entry_id INTEGER NOT NULL REFERENCES entries(id) ON DELETE CASCADE,
      name TEXT NOT NULL,
      contact TEXT,
      PRIMARY KEY (entry_id, name)
    );
    CREATE INDEX entry_people_name ON entry_people(name);

    CREATE TABLE entry_sources (
      entry_id INTEGER NOT NULL REFERENCES entries(id) ON DELETE CASCADE,
      kind TEXT NOT NULL CHECK (kind IN ('cal', 'gm', 'wa')),
      source_id TEXT NOT NULL,
      UNIQUE (kind, source_id)
    );
    CREATE INDEX entry_sources_entry ON entry_sources(entry_id);

    CREATE TABLE entry_links (
      entry_id INTEGER NOT NULL REFERENCES entries(id) ON DELETE CASCADE,
      kind TEXT NOT NULL CHECK (kind IN ('email', 'file', 'link')),
      label TEXT NOT NULL,
      url TEXT NOT NULL,
      PRIMARY KEY (entry_id, url)
    );

    CREATE VIRTUAL TABLE entries_fts USING fts5(title, body, tokenize = 'porter unicode61 remove_diacritics 2');

    CREATE TABLE runs (
      id INTEGER PRIMARY KEY,
      started_at TEXT NOT NULL,
      finished_at TEXT,
      since TEXT NOT NULL,
      until TEXT NOT NULL,
      status TEXT NOT NULL CHECK (status IN ('running', 'ok', 'error')),
      stats TEXT NOT NULL DEFAULT '{}',
      error TEXT
    );

    CREATE VIEW v_entries AS
      SELECT e.id, e.date, e.title, e.details,
        (SELECT group_concat(tag, ' ') FROM entry_tags t WHERE t.entry_id = e.id) AS tags,
        (SELECT group_concat(name, '; ') FROM entry_people p WHERE p.entry_id = e.id) AS people,
        (SELECT group_concat(kind || ':' || source_id, ' ') FROM entry_sources s WHERE s.entry_id = e.id) AS sources
      FROM entries e;
    )sql"
};

// ============================================================================
// Schema Notes for `schema` command
// ============================================================================

inline constexpr std::string_view SCHEMA_NOTES =
R"(Read-only SQL (SQLite). Prefer the view v_entries.
v_entries(id, date, title, details, tags, people, sources)
  date: 'YYYY.MM.DD' or 'YYYY.MM' (sorts as text); details: JSON array of lines (use json_each);
  tags: space-separated, no '#'; people: '; '-separated; sources: 'kind:id' space-separated.
entries(id, date, title, details, created_at, updated_at)
entry_tags(entry_id, tag)            -- tag like 'alex', 'health/meds'
entry_people(entry_id, name, contact) -- contact = Google Contacts resourceName or URL, may be null
entry_sources(entry_id, kind, source_id) -- kind in cal|gm|wa; (kind, source_id) unique
entry_links(entry_id, kind, label, url)  -- kind in email|file|link
entries_fts(title, body)  -- FTS5, rowid = entries.id; body = details, #tags, people, link labels
runs(id, started_at, finished_at, since, until, status, stats, error) -- mining runs
Examples:
  SELECT substr(date,1,4) y, count(*) FROM v_entries WHERE tags LIKE '%health/therapy%' GROUP BY y;
  SELECT e.date, e.title FROM entries_fts f JOIN entries e ON e.id = f.rowid WHERE entries_fts MATCH '"ear tube"' ORDER BY e.date;)";

// ============================================================================
// AppDb: SQLite Database Layer
// ============================================================================

class AppDb {
public:
    explicit AppDb(std::string path = defaultDbPath())
        : path_(std::move(path)) {
        slimr::SqliteOptions opts;
        opts.migrations = LOG_MIGRATIONS;
        db_ = std::make_unique<slimr::SqliteDb>(path_, std::move(opts));
    }

    static AppDb open(std::string path = defaultDbPath()) {
        return AppDb(std::move(path));
    }

    void close() {
        if (db_) {
            db_->close();
        }
    }

    slimr::SqliteDb& sqlite() { return *db_; }
    const std::string& path() const noexcept { return path_; }

    void ping() {
        (void)db_->get("SELECT 1;");
    }

    // --- Core Operations ---

    /// Add entries in one transaction; entries with an already-logged source are skipped.
    AddResult add(const std::vector<NewEntry>& entries) {
        AddResult result;
        db_->transaction([&](slimr::Tx&) {
            for (size_t index = 0; index < entries.size(); ++index) {
                const auto& entry = entries[index];
                std::optional<std::pair<Source, int64_t>> clash = std::nullopt;
                for (const auto& s : entry.sources) {
                    auto owner = sourceOwner(s);
                    if (owner) {
                        clash = std::make_pair(s, *owner);
                        break;
                    }
                }
                if (clash) {
                    result.skipped.push_back(SkippedEntry{
                        .index = static_cast<int64_t>(index),
                        .title = entry.title,
                        .existingId = clash->second,
                        .source = clash->first.kind + ":" + clash->first.id,
                    });
                    continue;
                }

                std::string normDate = normalizeDate(entry.date);
                std::string detailsJson = slimr::write_json(entry.details);

                auto rr = db_->run("INSERT INTO entries (date, title, details) VALUES (?, ?, ?)",
                                   {normDate, entry.title, detailsJson});
                int64_t entryId = rr.lastInsertRowid;

                attachRelations(entryId, entry.tags, entry.people, entry.sources, entry.links);
                reindex(entryId);
                result.added.push_back(entryId);
            }
        });
        return result;
    }

    /// Add one entry, dropping (and returning) any source already owned by another entry; used by import.
    AddKeepingResult addKeepingEntry(const NewEntry& entry) {
        AddKeepingResult res;
        std::vector<Source> keptSources;
        for (const auto& s : entry.sources) {
            auto owner = sourceOwner(s);
            if (!owner) {
                keptSources.push_back(s);
            } else {
                res.droppedSources.push_back(s.kind + ":" + s.id);
            }
        }

        std::vector<Source> uniqueSources;
        std::set<std::string> seen;
        for (const auto& s : keptSources) {
            std::string key = s.kind + ":" + s.id;
            if (seen.insert(key).second) {
                uniqueSources.push_back(s);
            }
        }

        int64_t id = 0;
        db_->transaction([&](slimr::Tx&) {
            std::string normDate = normalizeDate(entry.date);
            std::string detailsJson = slimr::write_json(entry.details);
            auto rr = db_->run("INSERT INTO entries (date, title, details) VALUES (?, ?, ?)",
                               {normDate, entry.title, detailsJson});
            id = rr.lastInsertRowid;
            attachRelations(id, entry.tags, entry.people, uniqueSources, entry.links);
            reindex(id);
        });
        res.id = id;
        return res;
    }

    /// One entry by id, or std::nullopt.
    std::optional<Entry> get(int64_t id) {
        auto row = db_->get("SELECT id, date, title, details FROM entries WHERE id = ?", {id});
        if (!row) return std::nullopt;
        const auto& r = *row;
        int64_t eid = r.at("id").get<int64_t>();
        std::string date = r.at("date").get<std::string>();
        std::string title = r.at("title").get<std::string>();
        std::string details = r.at("details").get<std::string>();
        return hydrate(eid, date, title, details);
    }

    /// The entry owning a source, or std::nullopt.
    std::optional<Entry> getBySource(const Source& source) {
        auto owner = sourceOwner(source);
        if (!owner) return std::nullopt;
        return get(*owner);
    }

    /// Search entries: FTS keywords (relevance, then newest) or filters only (newest first).
    std::vector<Entry> search(const SearchFilters& filters) {
        std::vector<std::string> where;
        std::vector<slimr::SqlValue> params;
        std::string from = "entries e";
        std::string order = "e.date DESC, e.id DESC";

        std::string match;
        if (filters.query && !filters.query->empty()) {
            match = ftsQuery(*filters.query);
        }

        if (!match.empty()) {
            from = "entries_fts f JOIN entries e ON e.id = f.rowid";
            where.push_back("entries_fts MATCH ?");
            params.push_back(match);
            order = "bm25(entries_fts, 4.0, 1.0), e.date DESC";
        }
        if (filters.from && !filters.from->empty()) {
            where.push_back("e.date >= ?");
            params.push_back(normalizeDate(*filters.from));
        }
        if (filters.to && !filters.to->empty()) {
            where.push_back("e.date <= ?");
            params.push_back(normalizeDate(*filters.to) + "~");
        }
        for (const auto& raw_tag : filters.tags) {
            std::string tag = normalizeTag(raw_tag);
            where.push_back("EXISTS (SELECT 1 FROM entry_tags t WHERE t.entry_id = e.id AND (t.tag = ? OR t.tag LIKE ?))");
            params.push_back(tag);
            params.push_back(tag + "/%");
        }
        if (filters.person && !filters.person->empty()) {
            where.push_back("EXISTS (SELECT 1 FROM entry_people p WHERE p.entry_id = e.id AND p.name LIKE ?)");
            params.push_back("%" + *filters.person + "%");
        }

        int64_t limit = filters.limit.value_or(50);
        if (limit < 1) limit = 1;
        if (limit > 500) limit = 500;

        std::string sql = "SELECT e.id, e.date, e.title, e.details FROM " + from;
        if (!where.empty()) {
            sql += " WHERE ";
            for (size_t i = 0; i < where.size(); ++i) {
                if (i > 0) sql += " AND ";
                sql += where[i];
            }
        }
        sql += " ORDER BY " + order + " LIMIT " + std::to_string(limit);

        auto rows = db_->all(sql, params);
        std::vector<Entry> entries;
        entries.reserve(rows.size());
        for (const auto& r : rows) {
            int64_t id = r.at("id").get<int64_t>();
            std::string date = r.at("date").get<std::string>();
            std::string title = r.at("title").get<std::string>();
            std::string details = r.at("details").get<std::string>();
            entries.push_back(hydrate(id, date, title, details));
        }
        return entries;
    }

    /// Apply a patch to one entry; std::nullopt when it doesn't exist.
    std::optional<Entry> update(int64_t id, const EntryPatch& patch) {
        auto existing = get(id);
        if (!existing) return std::nullopt;

        db_->transaction([&](slimr::Tx&) {
            applyPatch(id, patch);
        });

        return get(id);
    }

    /// Fold mergeId into keepId, then delete it.
    std::optional<Entry> merge(int64_t keepId, int64_t mergeId) {
        auto keep = get(keepId);
        auto gone = get(mergeId);
        if (!keep || !gone || keepId == mergeId) return std::nullopt;

        db_->transaction([&](slimr::Tx&) {
            std::vector<std::string> details = keep->details;
            for (const auto& line : gone->details) {
                if (std::find(details.begin(), details.end(), line) == details.end()) {
                    details.push_back(line);
                }
            }
            db_->run("DELETE FROM entries WHERE id = ?", {mergeId});
            db_->run("DELETE FROM entries_fts WHERE rowid = ?", {mergeId});

            EntryPatch patch;
            patch.details = std::move(details);
            patch.addTags = gone->tags;
            patch.addPeople = gone->people;
            patch.addSources = gone->sources;
            patch.addLinks = gone->links;

            applyPatch(keepId, patch);
        });

        return get(keepId);
    }

    /// Delete one entry; false when it doesn't exist.
    bool deleteEntry(int64_t id) {
        bool deleted = false;
        db_->transaction([&](slimr::Tx&) {
            auto rr = db_->run("DELETE FROM entries WHERE id = ?", {id});
            deleted = rr.changes > 0;
            db_->run("DELETE FROM entries_fts WHERE rowid = ?", {id});
        });
        return deleted;
    }

    /// Run a read-only query (SELECT/WITH only) on a separate read-only connection; at most limit rows.
    SqlResult sql(const std::string& query, int64_t limit = 200) {
        if (!is_select_or_with(query)) {
            throw std::runtime_error("Only SELECT or WITH queries are allowed.");
        }
        slimr::IterateOptions opts;
        opts.limit = static_cast<size_t>(limit);
        opts.readOnlyConnection = true;
        auto res = db_->iterateLimited(query, opts);
        return SqlResult{.rows = std::move(res.rows), .truncated = res.truncated};
    }

    /// Recent mining runs, newest first.
    std::vector<Run> runs(int64_t limit = 20) {
        auto rows = db_->all(
            "SELECT id, started_at, finished_at, since, until, status, stats, error FROM runs ORDER BY id DESC LIMIT ?",
            std::vector<slimr::SqlValue>{limit}
        );
        std::vector<Run> res;
        res.reserve(rows.size());
        for (const auto& r : rows) {
            Run run;
            run.id = r.at("id").get<int64_t>();
            run.startedAt = r.at("started_at").get<std::string>();
            if (r.contains("finished_at") && r.at("finished_at").is_string()) {
                run.finishedAt = r.at("finished_at").get<std::string>();
            }
            run.since = r.at("since").get<std::string>();
            run.until = r.at("until").get<std::string>();
            run.status = r.at("status").get<std::string>();
            std::string statsStr = r.at("stats").get<std::string>();
            auto parsedStats = slimr::parse_json<slimr::Json>(statsStr);
            run.stats = parsedStats ? *parsedStats : slimr::Json::object_t{};
            if (r.contains("error") && r.at("error").is_string()) {
                run.error = r.at("error").get<std::string>();
            }
            res.push_back(std::move(run));
        }
        return res;
    }

    /// Start a mining run; returns its id.
    int64_t runStart(const std::string& since, const std::string& until) {
        std::string now = format_iso_now();
        auto rr = db_->run(
            "INSERT INTO runs (started_at, since, until, status) VALUES (?, ?, ?, 'running')",
            std::vector<slimr::SqlValue>{now, since, until}
        );
        return rr.lastInsertRowid;
    }

    /// Finish a mining run.
    void runFinish(int64_t id, const std::string& status, const slimr::Json& stats, const std::optional<std::string>& error = std::nullopt) {
        std::string now = format_iso_now();
        std::string statsStr = slimr::write_json(stats);
        slimr::SqlValue errVal = error.has_value() ? slimr::SqlValue(*error) : slimr::SqlValue(std::monostate{});
        db_->run(
            "UPDATE runs SET finished_at = ?, status = ?, stats = ?, error = ? WHERE id = ?",
            std::vector<slimr::SqlValue>{now, status, statsStr, errVal, id}
        );
    }

    /// Window end of the last successful run, or std::nullopt.
    std::optional<std::string> lastSuccessfulUntil() {
        return db_->pluck<std::string>("SELECT until FROM runs WHERE status = 'ok' ORDER BY id DESC LIMIT 1");
    }

    /// Back up the database to destPath.
    void backup(const std::string& destPath) {
        db_->backup(destPath);
    }

    /// Look up entry owning source.
    std::optional<int64_t> sourceOwner(const Source& s) {
        return db_->pluck<int64_t>(
            "SELECT entry_id FROM entry_sources WHERE kind = ? AND source_id = ?",
            std::vector<slimr::SqlValue>{s.kind, s.id}
        );
    }

private:
    std::string path_;
    std::unique_ptr<slimr::SqliteDb> db_;

    void attachRelations(
        int64_t id,
        const std::vector<std::string>& tags,
        const std::vector<Person>& people,
        const std::vector<Source>& sources,
        const std::vector<Link>& links
    ) {
        for (const auto& raw_tag : tags) {
            std::string t = normalizeTag(raw_tag);
            if (!t.empty()) {
                db_->run("INSERT OR IGNORE INTO entry_tags (entry_id, tag) VALUES (?, ?)", {id, t});
            }
        }
        for (const auto& p : people) {
            if (!p.name.empty()) {
                slimr::SqlValue contactVal = p.contact.has_value() ? slimr::SqlValue(*p.contact) : slimr::SqlValue(std::monostate{});
                db_->run(
                    "INSERT INTO entry_people (entry_id, name, contact) VALUES (?, ?, ?) "
                    "ON CONFLICT (entry_id, name) DO UPDATE SET contact = COALESCE(excluded.contact, contact)",
                    {id, p.name, contactVal}
                );
            }
        }
        for (const auto& s : sources) {
            if (!s.kind.empty() && !s.id.empty()) {
                db_->run("INSERT OR IGNORE INTO entry_sources (entry_id, kind, source_id) VALUES (?, ?, ?)", {id, s.kind, s.id});
            }
        }
        for (const auto& l : links) {
            if (!l.url.empty()) {
                db_->run("INSERT OR IGNORE INTO entry_links (entry_id, kind, label, url) VALUES (?, ?, ?, ?)", {id, l.kind, l.label, l.url});
            }
        }
    }

    void applyPatch(int64_t id, const EntryPatch& patch) {
        if (patch.date) {
            db_->run("UPDATE entries SET date = ? WHERE id = ?", {normalizeDate(*patch.date), id});
        }
        if (patch.title) {
            db_->run("UPDATE entries SET title = ? WHERE id = ?", {*patch.title, id});
        }
        if (patch.details) {
            db_->run("UPDATE entries SET details = ? WHERE id = ?", {slimr::write_json(*patch.details), id});
        }
        for (const auto& t : patch.removeTags) {
            db_->run("DELETE FROM entry_tags WHERE entry_id = ? AND tag = ?", {id, normalizeTag(t)});
        }
        for (const auto& name : patch.removePeople) {
            db_->run("DELETE FROM entry_people WHERE entry_id = ? AND name = ?", {id, name});
        }
        for (const auto& s : patch.removeSources) {
            db_->run("DELETE FROM entry_sources WHERE entry_id = ? AND kind = ? AND source_id = ?", {id, s.kind, s.id});
        }
        for (const auto& url : patch.removeLinks) {
            db_->run("DELETE FROM entry_links WHERE entry_id = ? AND url = ?", {id, url});
        }
        for (const auto& s : patch.addSources) {
            auto owner = sourceOwner(s);
            if (owner && *owner != id) {
                throw std::runtime_error("Source " + s.kind + ":" + s.id + " already belongs to entry " + std::to_string(*owner) + ".");
            }
        }
        attachRelations(id, patch.addTags, patch.addPeople, patch.addSources, patch.addLinks);
        db_->run("UPDATE entries SET updated_at = strftime('%Y-%m-%dT%H:%M:%SZ', 'now') WHERE id = ?", {id});
        reindex(id);
    }

    void reindex(int64_t id) {
        auto entryOpt = get(id);
        db_->run("DELETE FROM entries_fts WHERE rowid = ?", {id});
        if (!entryOpt) return;
        const auto& entry = *entryOpt;
        std::vector<std::string> lines = entry.details;
        for (const auto& t : entry.tags) {
            lines.push_back("#" + t);
        }
        for (const auto& p : entry.people) {
            lines.push_back(p.name);
        }
        for (const auto& l : entry.links) {
            lines.push_back(l.label);
        }
        std::string body;
        for (size_t i = 0; i < lines.size(); ++i) {
            if (i > 0) body += "\n";
            body += lines[i];
        }
        db_->run("INSERT INTO entries_fts (rowid, title, body) VALUES (?, ?, ?)", {id, entry.title, body});
    }

    Entry hydrate(int64_t id, const std::string& date, const std::string& title, const std::string& detailsJson) {
        Entry entry;
        entry.id = id;
        entry.date = date;
        entry.title = title;
        auto parsedDetails = slimr::parse_json<std::vector<std::string>>(detailsJson);
        if (parsedDetails) {
            entry.details = *parsedDetails;
        }

        auto tagRows = db_->pluckAll<std::string>("SELECT tag FROM entry_tags WHERE entry_id = ? ORDER BY rowid", std::vector<slimr::SqlValue>{id});
        entry.tags = std::move(tagRows);

        auto peopleRows = db_->all("SELECT name, contact FROM entry_people WHERE entry_id = ? ORDER BY rowid", std::vector<slimr::SqlValue>{id});
        for (const auto& r : peopleRows) {
            Person p;
            p.name = r.at("name").get<std::string>();
            if (r.contains("contact") && r.at("contact").is_string()) {
                p.contact = r.at("contact").get<std::string>();
            }
            entry.people.push_back(std::move(p));
        }

        auto sourceRows = db_->all("SELECT kind, source_id FROM entry_sources WHERE entry_id = ? ORDER BY rowid", std::vector<slimr::SqlValue>{id});
        for (const auto& r : sourceRows) {
            Source s;
            s.kind = r.at("kind").get<std::string>();
            s.id = r.at("source_id").get<std::string>();
            entry.sources.push_back(std::move(s));
        }

        auto linkRows = db_->all("SELECT kind, label, url FROM entry_links WHERE entry_id = ? ORDER BY rowid", std::vector<slimr::SqlValue>{id});
        for (const auto& r : linkRows) {
            Link l;
            l.kind = r.at("kind").get<std::string>();
            l.label = r.at("label").get<std::string>();
            l.url = r.at("url").get<std::string>();
            entry.links.push_back(std::move(l));
        }
        return entry;
    }
};

} // namespace personal_log
