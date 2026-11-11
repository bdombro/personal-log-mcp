#pragma once

#include "db.hpp"
#include "mine_config.hpp"
#include "curl.hpp"
#include "json.hpp"
#include "sqlite.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <memory>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>
#include <unistd.h>

namespace personal_log {

// ============================================================================
// Constants & Configuration
// ============================================================================

inline constexpr std::string_view SUGGEST_CALENDAR = "AI-Suggests";
inline constexpr std::string_view SUGGEST_CALENDAR_ID_ENV = "PERSONAL_LOG_SUGGEST_CALENDAR_ID";

inline constexpr double JEV_DROP_BELOW = 0.1;

struct DefaultLlmConfig {
    std::string defaultModel = "deepseek-v4-pro";
    std::string fast = "deepseek-flash";
    std::string defaultFallback = "deepseek-v4-pro";
    std::string fastFallback = "deepseek-flash";
};

inline const DefaultLlmConfig DEFAULT_LLM{};

inline constexpr size_t CHUNK_EMAILS = 60;
inline constexpr size_t CHUNK_WHATSAPP_LINES = 1500;
inline constexpr size_t AGY_MAX_PROMPT_CHARS = 150000;

struct ChildEnvKeysMap {
    std::vector<std::string> google = {"GWS_BIN", "GWS_PROFILE"};
    std::vector<std::string> obsidian = {"OBSIDIAN_VAULT"};
    std::vector<std::string> whatsapp = {"WHATSAPP_MCP_DATA_DIR"};
    std::vector<std::string> agy = {};
};

inline const ChildEnvKeysMap CHILD_ENV_KEYS{};

// ============================================================================
// Data Types for Mining Pipeline
// ============================================================================

struct ModelChoice {
    std::string model;
    std::optional<std::string> fallback = std::nullopt;
    std::string effort; // "off" | "low"

    bool operator==(const ModelChoice&) const = default;
};

struct JevQuestion {
    std::string state;
    std::string statement;
};

struct GmailAttachment {
    std::string filename;
    std::string mimeType;
    int64_t size = 0;
    std::string attachmentId;
};

struct GmailMessage {
    std::string id;
    std::string threadId;
    std::string from;
    std::string to;
    std::string date;
    std::string subject;
    std::string body;
    std::string snippet;
    std::vector<GmailAttachment> attachments;
    std::vector<std::string> labelIds;
};

struct WhatsAppChat {
    std::string jid;
    std::string name;
    bool is_group = false;
    int64_t message_count = 0;
};

struct WhatsAppMessage {
    std::string id;
    std::string chat_jid;
    std::string chat_name;
    std::string sender_name;
    bool is_from_me = false;
    std::string ts_utc;
    std::string content;
};

struct LlmUsage {
    int64_t input = 0;
    int64_t output = 0;
    int64_t reasoning = 0;
};

struct LlmReply {
    std::string text;
    LlmUsage usage;
};

struct MinedEmail {
    std::string id;
    std::string threadId;
    std::string from;
    std::string to;
    std::string date;
    std::string subject;
    std::string body;
    std::vector<std::string> attachments;
    std::optional<int64_t> loggedEntryId = std::nullopt;
};

struct MinedEvent {
    std::string id;
    std::vector<std::string> calendars;
    std::string start;
    std::string end;
    std::string title;
    std::string location;
    std::string description;
    std::optional<int> seriesCount = std::nullopt;
    std::optional<std::string> seriesLast = std::nullopt;
};

struct WhatsAppChatLines {
    std::string chat;
    std::string kind;
    std::vector<std::string> lines;
};

struct OpenDeadline {
    std::string title;
    std::optional<std::string> due = std::nullopt;
};

struct NearbyEntry {
    int64_t id = 0;
    std::string date;
    std::string title;
    std::vector<std::string> tags;
    std::vector<std::string> sources;
};

struct Collected {
    std::string since;
    std::string until;
    std::vector<MinedEmail> emails;
    std::vector<WhatsAppChatLines> whatsapp;
    std::vector<MinedEvent> events;
    std::vector<MinedEvent> upcoming;
    std::vector<NearbyEntry> nearby;
    std::vector<OpenDeadline> openDeadlines;
    std::map<std::string, int64_t> stats;
    std::vector<std::string> warnings;
};

struct TriageIds {
    std::optional<std::string> gm = std::nullopt;
    std::vector<std::string> wa;
};

struct TriageExpense {
    std::string date;
    std::string who;
    std::string provider;
    std::string item;
    double amount = 0.0;
    std::string currency = "EUR";
    std::string qualifies = "ask";
};

struct TriageItem {
    std::string date;
    std::string title;
    std::vector<std::string> tags;
    std::vector<std::string> details;
    std::vector<std::string> people;
    std::optional<std::string> source = std::nullopt;
    TriageIds ids;
    std::optional<std::string> subject = std::nullopt;
    std::optional<std::string> quote = std::nullopt;
    std::vector<std::string> attachments;
    bool critical = false;
    std::optional<std::string> docFolder = std::nullopt;
    std::optional<slimr::Json> event = std::nullopt;
    std::optional<slimr::Json> deadline = std::nullopt;
    std::optional<TriageExpense> expense = std::nullopt;
};

struct DraftAdd {
    std::string date;
    std::string title;
    std::vector<std::string> tags;
    std::vector<std::string> details;
    std::vector<Person> people;
    std::vector<Source> sources;
    std::vector<Link> links;
};

struct DraftUpdate {
    int64_t id = 0;
    std::vector<std::string> addDetails;
    std::vector<std::string> addTags;
    std::vector<Person> addPeople;
    std::vector<Source> addSources;
};

struct DraftSuggestEvent {
    std::string title;
    std::string start;
    std::optional<std::string> end = std::nullopt;
    bool allDay = false;
    std::string location;
    std::string description;
};

struct DraftDeadline {
    std::string title;
    std::string due;
    std::string notes;
};

struct DraftOutput {
    std::vector<DraftAdd> add;
    std::vector<DraftUpdate> update;
    std::vector<DraftSuggestEvent> suggestEvents;
    std::vector<DraftDeadline> deadlines;
    std::vector<std::string> skipped;
};

struct ExpenseRow {
    std::string date; // YYYY.MM.DD
    std::string who;
    std::string provider;
    std::string item;
    double amount = 0.0;
    std::string qualifies = "ask"; // "yes" | "no" | "ask"
    std::string source;
};

struct MineOptions {
    std::optional<std::string> since = std::nullopt;
    std::optional<std::string> until = std::nullopt;
    bool dryRun = false;
};

struct MineResult {
    std::string since;
    std::string until;
    bool dryRun = false;
    std::vector<int64_t> added;
    std::vector<int64_t> updated;
    int64_t skippedDuplicates = 0;
    std::vector<std::string> deadlinesCreated;
    std::vector<std::string> eventsSuggested;
    std::vector<std::string> followUps;
    std::optional<DraftOutput> draft = std::nullopt;
    slimr::Json stats = slimr::Json::object_t{};
    std::vector<std::string> warnings;

    operator slimr::Json() const {
        slimr::Json::array_t addArr;
        for (int64_t a : added) addArr.push_back(a);
        slimr::Json::array_t upArr;
        for (int64_t u : updated) upArr.push_back(u);
        slimr::Json::array_t dlArr;
        for (const auto& dl : deadlinesCreated) dlArr.push_back(dl);
        slimr::Json::array_t evArr;
        for (const auto& ev : eventsSuggested) evArr.push_back(ev);
        slimr::Json::array_t fuArr;
        for (const auto& fu : followUps) fuArr.push_back(fu);
        slimr::Json::array_t wArr;
        for (const auto& w : warnings) wArr.push_back(w);

        return slimr::Json::object_t{
            {"since", since},
            {"until", until},
            {"dryRun", dryRun},
            {"added", addArr},
            {"updated", upArr},
            {"skippedDuplicates", skippedDuplicates},
            {"deadlinesCreated", dlArr},
            {"eventsSuggested", evArr},
            {"followUps", fuArr},
            {"stats", stats},
            {"warnings", wArr},
        };
    }
};

struct BackupResult {
    std::string file;
    bool created = false;
    std::vector<std::string> pruned;

    operator slimr::Json() const {
        slimr::Json::array_t prArr;
        for (const auto& p : pruned) prArr.push_back(p);
        return slimr::Json::object_t{
            {"file", file},
            {"created", created},
            {"pruned", prArr},
        };
    }
};

// ============================================================================
// ToolRunner Interface & Global Registry
// ============================================================================

class ToolRunner {
public:
    virtual ~ToolRunner() = default;
    virtual slimr::Json google(const slimr::Json& request) = 0;
    virtual slimr::Json driveUpload(const std::string& file, const std::string& name, const std::string& folderPath, const std::string& mimeType) = 0;
    virtual std::string gmailAttachment(const std::string& messageId, const std::string& attachmentId, const std::string& filename) = 0;
    virtual GmailMessage gmailMessage(const std::string& id) = 0;
    virtual std::vector<WhatsAppChat> whatsappChats() = 0;
    virtual std::vector<WhatsAppMessage> whatsappExport(const std::vector<std::string>& jids, const std::string& since, const std::string& until) = 0;
    virtual slimr::Json obsidian(const std::string& command, const slimr::Json& input) = 0;
    virtual LlmReply llm(const std::string& model, const std::string& effort, const std::string& system, const std::string& user) = 0;
    virtual std::optional<std::vector<double>> jev(const std::vector<JevQuestion>& questions) = 0;
};

inline std::shared_ptr<ToolRunner>& current_tools() {
    static std::shared_ptr<ToolRunner> runner = nullptr;
    return runner;
}

inline void setTools(std::shared_ptr<ToolRunner> runner) {
    current_tools() = runner;
}

// ============================================================================
// Core Utility Functions
// ============================================================================

/// Returns model, fallback model, and effort for a step.
inline ModelChoice modelFor(const std::string& step) {
    auto env = [](const char* name, const std::string& fallbackVal) -> std::string {
        const char* v = std::getenv(name);
        return (v && *v) ? std::string(v) : fallbackVal;
    };
    bool fast = (step == "triage");
    std::string model = fast ? env("ASSISTANT_LLM_FAST", DEFAULT_LLM.fast)
                             : env("ASSISTANT_LLM_DEFAULT", DEFAULT_LLM.defaultModel);
    std::string fallback = fast ? env("ASSISTANT_LLM_FAST_FALLBACK", DEFAULT_LLM.fastFallback)
                                : env("ASSISTANT_LLM_DEFAULT_FALLBACK", DEFAULT_LLM.defaultFallback);
    std::optional<std::string> fallbackOpt = (fallback == "none" || fallback == model)
                                            ? std::nullopt
                                            : std::optional<std::string>(fallback);
    return ModelChoice{
        .model = model,
        .fallback = fallbackOpt,
        .effort = fast ? "off" : "low"
    };
}

/// Minimal child environment: PATH, HOME and only the listed keys that are set.
inline std::map<std::string, std::string> childEnv(
    const std::vector<std::string>& keys = {},
    const std::map<std::string, std::string>& source = {}
) {
    std::map<std::string, std::string> env;
    auto get_val = [&](const std::string& k) -> std::optional<std::string> {
        if (!source.empty()) {
            auto it = source.find(k);
            if (it != source.end()) return it->second;
            return std::nullopt;
        }
        const char* val = std::getenv(k.c_str());
        if (val) return std::string(val);
        return std::nullopt;
    };

    std::vector<std::string> wanted = {"PATH", "HOME"};
    for (const auto& k : keys) {
        wanted.push_back(k);
    }
    for (const auto& k : wanted) {
        auto v = get_val(k);
        if (v) {
            env[k] = *v;
        }
    }
    return env;
}

/// Category of a mining warning: leading plain words cut at first colon, quote, digit or symbol.
inline std::string warningCategory(const std::string& warning) {
    size_t colon = warning.find(':');
    std::string prefix = (colon != std::string::npos) ? warning.substr(0, colon) : warning;

    std::vector<std::string> words;
    size_t i = 0;
    while (i < prefix.size()) {
        while (i < prefix.size() && std::isspace(static_cast<unsigned char>(prefix[i]))) ++i;
        if (i >= prefix.size()) break;
        size_t j = i;
        while (j < prefix.size() && !std::isspace(static_cast<unsigned char>(prefix[j]))) ++j;
        std::string word = prefix.substr(i, j - i);
        i = j;

        bool valid = !word.empty();
        for (size_t k = 0; k < word.size(); ++k) {
            unsigned char c = static_cast<unsigned char>(word[k]);
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '\'' || c == '-') {
                continue;
            }
            // UTF-8 right single quotation mark ’ (\xe2\x80\x99)
            if (k + 2 < word.size() && c == 0xe2 &&
                static_cast<unsigned char>(word[k + 1]) == 0x80 &&
                static_cast<unsigned char>(word[k + 2]) == 0x99) {
                k += 2;
                continue;
            }
            valid = false;
            break;
        }
        if (!valid || words.size() == 4) {
            break;
        }
        words.push_back(word);
    }

    if (words.empty()) return "warning";
    std::string out;
    for (size_t k = 0; k < words.size(); ++k) {
        if (k > 0) out += " ";
        out += words[k];
    }
    return out;
}

/// Scrub sensitive information: strips URLs, emails, phone numbers, secrets.
inline std::string scrub(std::string text) {
    text = std::regex_replace(text, std::regex(R"((?:https?:\/\/|www\.)\S+)", std::regex_constants::icase), "");
    text = std::regex_replace(text, std::regex(R"(\S+@\S+)"), "");
    text = std::regex_replace(text, std::regex(R"(\+?\d[\d\s().-]{5,}\d)"), "");
    text = std::regex_replace(text, std::regex(R"(\b(password|pin|ssn|pps)\b.*$)", std::regex_constants::icase), "");
    text = std::regex_replace(text, std::regex(R"(\s+)"), " ");
    text = std::regex_replace(text, std::regex(R"(\s+([,.;]))"), "$1");
    text = std::regex_replace(text, std::regex(R"([,;\s]+$)"), "");
    size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) ++start;
    return text.substr(start);
}

/// Keep only https:// links; dropped links add warnings.
inline std::vector<Link> sanitizeLinks(const std::vector<Link>& links, std::vector<std::string>& warnings) {
    std::vector<Link> kept;
    for (const auto& l : links) {
        bool ok = false;
        if (l.url.starts_with("https://")) {
            std::string_view rest = std::string_view(l.url).substr(8);
            if (!rest.empty() && rest.front() != '/' && rest.front() != ':') {
                ok = true;
            }
        }
        if (ok) {
            kept.push_back(l);
        } else {
            warnings.push_back("link dropped: not an https URL");
        }
    }
    return kept;
}

/// Accepts people/c<digits> only; dropped contacts add warnings.
inline std::optional<std::string> sanitizeContact(const std::optional<std::string>& contact, std::vector<std::string>& warnings) {
    if (!contact.has_value() || contact->empty()) {
        return std::nullopt;
    }
    static const std::regex contact_re(R"(^people/c\d+$)");
    if (std::regex_match(*contact, contact_re)) {
        return *contact;
    }
    warnings.push_back("contact dropped: not a people/c<digits> resource name");
    return std::nullopt;
}

/// POST with per-attempt timeout, retrying 429 and 5xx.
inline slimr::HttpResponse fetchWithRetry(
    const std::string& url,
    const slimr::HttpRequest& init,
    long timeoutMs = 10000,
    const std::vector<long>& delaysMs = {20000, 60000}
) {
    for (size_t attempt = 0; ; ++attempt) {
        slimr::HttpRequest req = init;
        req.url = url;
        req.timeoutMs = timeoutMs;
        try {
            slimr::HttpResponse res = slimr::request(req);
            if ((res.status == 429 || res.status >= 500) && attempt < delaysMs.size()) {
                // Retry
            } else {
                return res;
            }
        } catch (...) {
            if (attempt >= delaysMs.size()) {
                throw;
            }
        }
        if (attempt < delaysMs.size()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(delaysMs[attempt]));
        }
    }
}

/// YYYY-MM-DD plus n days.
inline std::string addDays(const std::string& iso, int days) {
    int y = 0, m = 0, d = 0;
    if (sscanf(iso.c_str(), "%d-%d-%d", &y, &m, &d) != 3) {
        return iso;
    }
    std::chrono::year_month_day ymd{
        std::chrono::year{y},
        std::chrono::month{static_cast<unsigned>(m)},
        std::chrono::day{static_cast<unsigned>(d)}
    };
    auto sys_days = std::chrono::sys_days{ymd} + std::chrono::days{days};
    std::chrono::year_month_day new_ymd{sys_days};
    std::ostringstream oss;
    oss << std::setfill('0')
        << std::setw(4) << static_cast<int>(new_ymd.year()) << "-"
        << std::setw(2) << static_cast<unsigned>(new_ymd.month()) << "-"
        << std::setw(2) << static_cast<unsigned>(new_ymd.day());
    return oss.str();
}

/// YYYY-MM-DD -> YYYY.MM.DD
inline std::string dotted(std::string_view iso) {
    return normalizeDate(iso);
}

/// Euro amount formatted as 1,000.00
inline std::string money(double n) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << n;
    std::string s = ss.str();
    size_t dot = s.find('.');
    int int_part_len = static_cast<int>(dot != std::string::npos ? dot : s.size());
    std::string res;
    for (int i = 0; i < int_part_len; ++i) {
        if (i > 0 && (int_part_len - i) % 3 == 0) {
            res += ',';
        }
        res += s[i];
    }
    if (dot != std::string::npos) {
        res += s.substr(dot);
    }
    return res;
}

/// Parse money string like "1,000.00" -> 1000.0
inline double parseMoney(std::string_view cell) {
    std::string clean;
    for (size_t i = 0; i < cell.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(cell[i]);
        if (c == 0xe2 && i + 2 < cell.size() &&
            static_cast<unsigned char>(cell[i + 1]) == 0x82 &&
            static_cast<unsigned char>(cell[i + 2]) == 0xac) {
            i += 2;
            continue; // UTF-8 '€'
        }
        if (c != ',' && !std::isspace(c)) {
            clean += static_cast<char>(c);
        }
    }
    if (clean.empty()) return 0.0;
    try {
        return std::stod(clean);
    } catch (...) {
        return 0.0;
    }
}

// ============================================================================
// Follow-Up Helpers: Expenses & Meds
// ============================================================================

inline constexpr std::string_view TABLE_HEADER =
    "| Date | Who | Provider | Item | Amount (€) | Reimbursed (€) | Qualifies | Claimed | Source |";

/// Triage expense -> table row, or std::nullopt.
inline std::optional<ExpenseRow> expenseRow(const TriageItem& t, const MineConfig& config = loadMineConfig()) {
    if (!t.expense) return std::nullopt;
    const auto& e = *t.expense;

    auto clean_str = [](std::string_view v) -> std::string {
        std::string s;
        for (size_t i = 0; i < v.size(); ++i) {
            unsigned char c = static_cast<unsigned char>(v[i]);
            // Filter control chars & unicode line/paragraph separators
            if (c < 32 || c == 127) {
                s += ' ';
            } else if (c == '|') {
                s += '/';
            } else {
                s += static_cast<char>(c);
            }
        }
        // Normalize whitespace
        std::string res;
        bool in_space = false;
        for (char c : s) {
            if (std::isspace(static_cast<unsigned char>(c))) {
                if (!in_space) {
                    res += ' ';
                    in_space = true;
                }
            } else {
                res += c;
                in_space = false;
            }
        }
        size_t start = 0;
        while (start < res.size() && std::isspace(static_cast<unsigned char>(res[start]))) ++start;
        size_t end = res.size();
        while (end > start && std::isspace(static_cast<unsigned char>(res[end - 1]))) --end;
        return res.substr(start, end - start);
    };

    std::string date = !e.date.empty() ? clean_str(e.date) : clean_str(t.date);
    for (char& c : date) if (c == '-') c = '.';

    double amount = e.amount;
    std::string currency = clean_str(e.currency);
    for (char& c : currency) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    std::string whoRaw = clean_str(e.who);
    for (char& c : whoRaw) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    std::string who;
    for (const auto& [tag, name] : config.whoNames) {
        if (whoRaw.starts_with(tag)) {
            who = name;
            break;
        }
    }
    if (who.empty()) who = clean_str(e.who);

    static const std::regex date_re(R"(^\d{4}\.\d{2}\.\d{2}$)");
    if (!std::regex_match(date, date_re) || who.empty() || !std::isfinite(amount) || amount <= 0.0) {
        return std::nullopt;
    }
    if (!currency.empty() && currency != "EUR" && currency != "€") {
        return std::nullopt;
    }

    std::string q = clean_str(e.qualifies);
    for (char& c : q) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::string qualifies = (q == "yes" || q == "no") ? q : "ask";

    std::string provider = clean_str(e.provider);
    if (provider.empty()) provider = "(provider?)";

    std::string item = clean_str(e.item);
    if (item.empty()) item = clean_str(t.title);

    std::string source;
    if (t.ids.gm && !t.ids.gm->empty()) {
        source = "[✉️](https://mail.google.com/mail/u/?authuser=" + std::string(config.gmailAccount) + "#all/" + *t.ids.gm + ")";
    } else {
        source = "WhatsApp " + date;
    }

    return ExpenseRow{
        .date = date,
        .who = who,
        .provider = provider,
        .item = item,
        .amount = std::round(amount * 100.0) / 100.0,
        .qualifies = qualifies,
        .source = source,
    };
}

/// Cells of a markdown table row.
inline std::vector<std::string> tableCells(std::string_view line) {
    std::vector<std::string> cells;
    size_t start = 0;
    while (start < line.size() && line[start] != '|') ++start;
    if (start >= line.size()) return cells;
    ++start; // past first '|'

    while (start < line.size()) {
        size_t next = line.find('|', start);
        if (next == std::string_view::npos) break;
        std::string_view cell = line.substr(start, next - start);
        size_t cstart = 0;
        while (cstart < cell.size() && std::isspace(static_cast<unsigned char>(cell[cstart]))) ++cstart;
        size_t cend = cell.size();
        while (cend > cstart && std::isspace(static_cast<unsigned char>(cell[cend - 1]))) --cend;
        cells.emplace_back(cell.substr(cstart, cend - cstart));
        start = next + 1;
    }
    return cells;
}

struct TableAt {
    int header = -1;
    std::vector<int> rows;
    int sectionEnd = -1;
};

inline std::optional<TableAt> findTable(const std::vector<std::string>& lines, std::string_view year) {
    int start = -1;
    std::string target = "## " + std::string(year);
    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
        std::string_view l = lines[i];
        while (!l.empty() && std::isspace(static_cast<unsigned char>(l.back()))) l.remove_suffix(1);
        while (!l.empty() && std::isspace(static_cast<unsigned char>(l.front()))) l.remove_prefix(1);
        if (l == target) {
            start = i;
            break;
        }
    }
    if (start < 0) return std::nullopt;

    int sectionEnd = static_cast<int>(lines.size());
    static const std::regex heading_re(R"(^#{1,2}\s+)");
    for (int i = start + 1; i < static_cast<int>(lines.size()); ++i) {
        if (std::regex_search(lines[i], heading_re)) {
            sectionEnd = i;
            break;
        }
    }

    int header = -1;
    static const std::regex header_re(R"(^\|\s*Date\s*\|)");
    for (int i = start + 1; i < sectionEnd; ++i) {
        if (std::regex_search(lines[i], header_re)) {
            header = i;
            break;
        }
    }
    if (header < 0) return std::nullopt;

    std::vector<int> rows;
    for (int i = header + 2; i < sectionEnd && lines[i].starts_with("|"); ++i) {
        rows.push_back(i);
    }
    return TableAt{.header = header, .rows = std::move(rows), .sectionEnd = sectionEnd};
}

inline std::string formatExpenseRow(const ExpenseRow& r) {
    return "| " + r.date + " | " + r.who + " | " + r.provider + " | " + r.item + " | " +
           money(r.amount) + " |  | " + r.qualifies + " | no | " + r.source + " |";
}

inline void insertYear(std::vector<std::string>& lines, std::string_view year) {
    int at = -1;
    static const std::regex year_heading(R"(^## (\d{4})\s*$)");
    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
        std::smatch m;
        if (std::regex_match(lines[i], m, year_heading)) {
            if (m[1].str() < year) {
                at = i;
                break;
            }
        }
    }
    if (at < 0) {
        static const std::regex questions_re(R"(^## Questions)");
        for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
            if (std::regex_search(lines[i], questions_re)) {
                at = i;
                break;
            }
        }
    }
    if (at < 0) at = static_cast<int>(lines.size());

    std::vector<std::string> block = {
        "## " + std::string(year),
        std::string(TABLE_HEADER),
        "|---|---|---|---|---|---|---|---|---|",
        "",
        "**Totals:** qualifying €0.00; ask €0.00; not qualifying €0.00; estimated relief 20% of qualifying = €0.00 (€0.00 if the ask items are accepted).",
        "",
    };
    lines.insert(lines.begin() + at, block.begin(), block.end());
}

inline bool sameExpense(const std::vector<std::string>& c, const ExpenseRow& row) {
    if (c.size() < 9) return false;
    double cellAmount = parseMoney(c[4]);
    if (std::abs(cellAmount - row.amount) > 0.005) return false;

    const std::string& source = c[8];
    if (source.starts_with("[") && source == row.source) {
        return true;
    }

    auto base = [](std::string_view p) -> std::string {
        std::string lower(p);
        for (char& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        lower = std::regex_replace(lower, std::regex(R"(\(.*?\))"), "");
        lower = std::regex_replace(lower, std::regex(R"([^a-z0-9 ])"), "");
        lower = std::regex_replace(lower, std::regex(R"(\s+)"), " ");
        size_t s = 0;
        while (s < lower.size() && std::isspace(static_cast<unsigned char>(lower[s]))) ++s;
        size_t e = lower.size();
        while (e > s && std::isspace(static_cast<unsigned char>(lower[e - 1]))) --e;
        return lower.substr(s, e - s);
    };

    std::string a = base(c[2]);
    std::string b = base(row.provider);

    std::string cWho(c[1]);
    for (char& ch : cWho) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    std::string rowWho(row.who);
    for (char& ch : rowWho) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

    return (c[0] == row.date) && (cWho == rowWho) &&
           (a.find(b) != std::string::npos || b.find(a) != std::string::npos);
}

inline void updateTotals(std::vector<std::string>& lines, std::string_view year) {
    auto table = findTable(lines, year);
    if (!table) return;

    double sumYes = 0.0, sumAsk = 0.0, sumNo = 0.0;
    for (int idx : table->rows) {
        auto c = tableCells(lines[idx]);
        if (c.size() < 7) continue;
        double amount = parseMoney(c[4]);
        double reimbursed = (c.size() > 5) ? parseMoney(c[5]) : 0.0;
        double net = amount - reimbursed;
        std::string q = c[6];
        for (char& ch : q) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

        if (q == "yes") sumYes += net;
        else if (q == "no") sumNo += amount;
        else sumAsk += net;
    }

    int at = -1;
    for (int i = table->header + 1; i < table->sectionEnd; ++i) {
        if (lines[i].starts_with("**Totals:**")) {
            at = i;
            break;
        }
    }
    if (at < 0) return;

    std::string l = lines[at];
    l = std::regex_replace(l, std::regex(R"(not qualifying €[\d,.]+)"), "not qualifying €" + money(sumNo));
    l = std::regex_replace(l, std::regex(R"((\*\*Totals:\*\*\s*)qualifying €[\d,.]+)"), "$1qualifying €" + money(sumYes));
    l = std::regex_replace(l, std::regex(R"(\bask €[\d,.]+)"), "ask €" + money(sumAsk));
    l = std::regex_replace(l, std::regex(R"(20% of qualifying = €[\d,.]+)"), "20% of qualifying = €" + money(sumYes * 0.2));
    l = std::regex_replace(l, std::regex(R"(\(€[\d,.]+ if )"), "(€" + money((sumYes + sumAsk) * 0.2) + " if ");
    lines[at] = l;
}

struct AddExpenseResult {
    std::string content;
    std::vector<ExpenseRow> added;
    std::vector<ExpenseRow> skipped;
};

/// Insert expense rows into the Medical Expenses note.
inline AddExpenseResult addExpenseRows(std::string_view content, const std::vector<ExpenseRow>& rows) {
    std::vector<std::string> lines;
    std::string contentStr(content);
    std::istringstream ss{contentStr};
    std::string line;
    while (std::getline(ss, line)) {
        lines.push_back(line);
    }
    if (content.ends_with('\n')) {
        lines.push_back("");
    }

    auto sorted = rows;
    std::sort(sorted.begin(), sorted.end(), [](const ExpenseRow& a, const ExpenseRow& b) {
        return a.date < b.date;
    });

    std::vector<ExpenseRow> added;
    std::vector<ExpenseRow> skipped;
    std::set<std::string> touched;

    for (const auto& row : sorted) {
        std::string year = row.date.substr(0, 4);
        auto table = findTable(lines, year);
        if (!table) {
            insertYear(lines, year);
            table = findTable(lines, year);
        }
        if (!table) continue;

        bool is_duplicate = false;
        for (int i : table->rows) {
            auto c = tableCells(lines[i]);
            if (sameExpense(c, row)) {
                is_duplicate = true;
                break;
            }
        }
        if (is_duplicate) {
            skipped.push_back(row);
            continue;
        }

        int at = table->header + 2;
        for (int i : table->rows) {
            auto c = tableCells(lines[i]);
            if (!c.empty() && c[0] <= row.date) {
                at = i + 1;
            }
        }
        lines.insert(lines.begin() + at, formatExpenseRow(row));
        added.push_back(row);
        touched.insert(year);
    }

    for (const auto& year : touched) {
        updateTotals(lines, year);
    }

    std::string out;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i > 0) out += "\n";
        out += lines[i];
    }
    return AddExpenseResult{.content = std::move(out), .added = std::move(added), .skipped = std::move(skipped)};
}

/// Replace a note's ## Meds section, folding following former meds headings.
inline std::string replaceMedsSection(std::string_view content, std::string_view section) {
    std::vector<std::string> lines;
    std::string contentStr(content);
    std::istringstream ss{contentStr};
    std::string line;
    while (std::getline(ss, line)) {
        lines.push_back(line);
    }
    if (content.ends_with('\n')) {
        lines.push_back("");
    }

    int start = -1;
    static const std::regex meds_re(R"(^## Meds\s*$)");
    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
        if (std::regex_match(lines[i], meds_re)) {
            start = i;
            break;
        }
    }
    if (start < 0) {
        throw std::runtime_error("no ## Meds section");
    }

    auto nextHeading = [&](int from) -> int {
        static const std::regex heading_re(R"(^#{1,2}\s+)");
        for (int j = from + 1; j < static_cast<int>(lines.size()); ++j) {
            if (std::regex_search(lines[j], heading_re)) {
                return j;
            }
        }
        return static_cast<int>(lines.size());
    };

    int end = nextHeading(start);
    if (end < static_cast<int>(lines.size())) {
        static const std::regex former_re(R"(^#{1,2} Meds (Former|Prior|History))", std::regex_constants::icase);
        if (std::regex_search(lines[end], former_re)) {
            end = nextHeading(end);
        }
    }

    std::vector<std::string> body;
    std::string secStr(section);
    while (!secStr.empty() && (secStr.back() == '\n' || secStr.back() == '\r')) {
        secStr.pop_back();
    }
    std::istringstream body_ss{secStr};
    while (std::getline(body_ss, line)) {
        body.push_back(line);
    }

    std::vector<std::string> res;
    for (int i = 0; i < start; ++i) res.push_back(lines[i]);
    for (auto& b : body) res.push_back(std::move(b));
    if (end < static_cast<int>(lines.size())) {
        res.push_back("");
        for (int i = end; i < static_cast<int>(lines.size()); ++i) {
            res.push_back(lines[i]);
        }
    } else {
        res.push_back("");
    }

    std::string out;
    for (size_t i = 0; i < res.size(); ++i) {
        if (i > 0) out += "\n";
        out += res[i];
    }
    return out;
}

// ============================================================================
// Backup & Prune
// ============================================================================

/// Keep 30 newest and 1 per month for up to 12 months.
inline std::vector<std::string> prune(const std::vector<std::string>& files) {
    static const std::regex log_re(R"(^log-\d{4}-\d{2}-\d{2}\.db\.gz$)");
    std::vector<std::string> sorted;
    for (const auto& f : files) {
        if (std::regex_match(f, log_re)) {
            sorted.push_back(f);
        }
    }
    std::sort(sorted.begin(), sorted.end(), std::greater<std::string>());

    std::set<std::string> keep;
    for (size_t i = 0; i < sorted.size() && i < 30; ++i) {
        keep.insert(sorted[i]);
    }

    std::set<std::string> months;
    for (const auto& f : sorted) {
        std::string month = f.substr(4, 7); // "YYYY-MM"
        if (!months.contains(month) && months.size() < 12) {
            months.insert(month);
            keep.insert(f);
        }
    }

    std::vector<std::string> to_delete;
    for (const auto& f : sorted) {
        if (!keep.contains(f)) {
            to_delete.push_back(f);
        }
    }
    return to_delete;
}

inline BackupResult backupLog(AppDb& db, const std::string& today = "") {
    std::string day = !today.empty() ? today : format_iso_now().substr(0, 10);
    std::string name = "log-" + day + ".db.gz";
    std::string tmp = "/tmp/personal-log-backup-" + std::to_string(getpid());
    std::string tmpDb = tmp + ".db";
    std::string tmpGz = tmp + ".gz";

    db.backup(tmpDb);

    std::string gzipCmd = "gzip -c \"" + tmpDb + "\" > \"" + tmpGz + "\"";
    (void)::system(gzipCmd.c_str());

    auto t = current_tools();
    if (!t) {
        std::error_code ec;
        std::filesystem::remove(tmpDb, ec);
        std::filesystem::remove(tmpGz, ec);
        return BackupResult{.file = name, .created = false, .pruned = {}};
    }

    std::string folder = "Backups/personal-log";
    const char* envFolder = std::getenv("PERSONAL_LOG_BACKUP_FOLDER");
    if (envFolder && *envFolder) folder = envFolder;

    // List backups in Drive folder
    slimr::Json listReq = slimr::Json::object_t{
        {"service", "drive"},
        {"resource", slimr::Json::array_t{"files"}},
        {"method", "list"},
        {"params", slimr::Json::object_t{{"pageSize", 1000}}},
    };
    auto listRes = t->google(listReq);

    std::vector<std::pair<std::string, std::string>> existing; // (id, name)
    if (listRes.contains("files") && listRes.at("files").is_array()) {
        for (const auto& f : listRes.at("files").get<slimr::Json::array_t>()) {
            std::string fid = f.contains("id") ? f.at("id").get<std::string>() : "";
            std::string fname = f.contains("name") ? f.at("name").get<std::string>() : "";
            existing.emplace_back(fid, fname);
        }
    }

    bool created = false;
    bool found = false;
    for (const auto& [fid, fname] : existing) {
        if (fname == name) {
            found = true;
            break;
        }
    }
    if (!found) {
        t->driveUpload(tmpGz, name, folder, "application/gzip");
        created = true;
        existing.emplace_back("", name);
    }

    std::vector<std::string> allNames;
    for (const auto& [fid, fname] : existing) {
        allNames.push_back(fname);
    }
    auto pruned = prune(allNames);

    for (const auto& [fid, fname] : existing) {
        if (!fid.empty() && std::find(pruned.begin(), pruned.end(), fname) != pruned.end()) {
            slimr::Json delReq = slimr::Json::object_t{
                {"service", "drive"},
                {"resource", slimr::Json::array_t{"files"}},
                {"method", "delete"},
                {"params", slimr::Json::object_t{{"fileId", fid}}},
            };
            t->google(delReq);
        }
    }

    std::error_code ec;
    std::filesystem::remove(tmpDb, ec);
    std::filesystem::remove(tmpGz, ec);

    return BackupResult{.file = name, .created = created, .pruned = std::move(pruned)};
}

// ============================================================================
// Mining Pipeline & Follow-Ups
// ============================================================================

struct StepUsage {
    std::string model;
    int64_t calls = 0;
    int64_t input = 0;
    int64_t output = 0;
    int64_t reasoning = 0;
};

using UsageMap = std::map<std::string, StepUsage>;

inline LlmReply callModel(
    const std::string& step,
    const std::string& system,
    const std::string& user,
    UsageMap& usage,
    std::vector<std::string>& warnings
) {
    auto choice = modelFor(step);
    std::string key = step;
    std::string used = choice.model;
    LlmReply reply;
    auto t = current_tools();
    if (!t) throw std::runtime_error("No ToolRunner configured");

    try {
        reply = t->llm(choice.model, choice.effort, system, user);
    } catch (const std::exception& e) {
        if (!choice.fallback || *choice.fallback == choice.model) {
            throw;
        }
        key = step + " (fallback)";
        used = *choice.fallback;
        if (!usage.contains(key)) {
            warnings.push_back(step + ": " + choice.model + " failed, used " + *choice.fallback + ": " + std::string(e.what()).substr(0, 200));
        }
        reply = t->llm(*choice.fallback, choice.effort, system, user);
    }

    auto& u = usage[key];
    u.model = used;
    u.calls++;
    u.input += reply.usage.input;
    u.output += reply.usage.output;
    u.reasoning += reply.usage.reasoning;
    return reply;
}

inline std::string formatEmail(const MinedEmail& e) {
    std::string s = "## Email gm=" + e.threadId;
    if (e.loggedEntryId) {
        s += " (thread already logged as entry #" + std::to_string(*e.loggedEntryId) + ")";
    }
    s += "\nFrom: " + e.from;
    s += "\nTo: " + e.to;
    s += "\nDate: " + e.date;
    s += "\nSubject: " + e.subject;
    if (!e.attachments.empty()) {
        s += "\nAttachments: ";
        for (size_t i = 0; i < e.attachments.size(); ++i) {
            if (i > 0) s += ", ";
            s += e.attachments[i];
        }
    }
    s += "\n\n" + e.body;
    return s;
}

inline std::string formatEvent(const MinedEvent& e) {
    std::string s = e.start;
    if (!e.end.empty()) s += " -> " + e.end;
    s += " | ";
    for (size_t i = 0; i < e.calendars.size(); ++i) {
        if (i > 0) s += "+";
        s += e.calendars[i];
    }
    s += " | " + e.title + " | " + e.location + " | " + e.description + " | cal=" + e.id;
    if (e.seriesCount && *e.seriesCount > 1) {
        s += " | series=" + std::to_string(*e.seriesCount) + " to " + e.seriesLast.value_or("");
    }
    return s;
}

inline std::string defaultEnd(const std::string& start) {
    size_t tPos = start.find('T');
    std::string date = (tPos != std::string::npos) ? start.substr(0, tPos) : start;
    std::string time = (tPos != std::string::npos) ? start.substr(tPos + 1) : "09:00";
    int h = 9, m = 0;
    sscanf(time.c_str(), "%d:%d", &h, &m);
    int hour = std::min(h + 1, 23);
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d", hour, m);
    return date + "T" + buf;
}

inline bool similar(std::string_view a, std::string_view b) {
    auto words = [](std::string_view s) -> std::set<std::string> {
        std::set<std::string> res;
        size_t i = 0;
        while (i < s.size()) {
            while (i < s.size() && !std::isalnum(static_cast<unsigned char>(s[i]))) ++i;
            if (i >= s.size()) break;
            size_t j = i;
            while (j < s.size() && std::isalnum(static_cast<unsigned char>(s[j]))) ++j;
            std::string w(s.substr(i, j - i));
            for (char& c : w) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (w.size() > 2) res.insert(std::move(w));
            i = j;
        }
        return res;
    };
    auto x = words(a);
    auto y = words(b);
    const auto& small = (x.size() <= y.size()) ? x : y;
    const auto& big = (&small == &x) ? y : x;
    if (small.empty()) return false;
    size_t hits = 0;
    for (const auto& w : small) {
        if (big.contains(w)) ++hits;
    }
    return static_cast<double>(hits) / static_cast<double>(small.size()) >= 0.5;
}

/// Strip title prefix: Dr, Mr, Mrs, Ms, Miss, Prof
inline std::string stripTitle(std::string_view name) {
    static const std::regex title_re(R"(^(dr|mr|mrs|ms|miss|prof)\.?\s+)", std::regex_constants::icase);
    std::string s(name);
    return std::regex_replace(s, title_re, "");
}

inline std::string normName(std::string_view name) {
    std::string s;
    bool in_space = false;
    for (char c : name) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!in_space) {
                s += ' ';
                in_space = true;
            }
        } else {
            s += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            in_space = false;
        }
    }
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) ++start;
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
    return s.substr(start, end - start);
}

// ============================================================================
// Follow-ups Implementation
// ============================================================================

struct FollowUpContext {
    AppDb& db;
    Collected& collected;
    std::vector<TriageItem>& triage;
    DraftOutput& draft;
    MineResult& result;
    bool dryRun = false;
    std::function<LlmReply(const std::string& step, const std::string& system, const std::string& user)> ask;
};

inline void followUpsContacts(FollowUpContext& ctx, const MineConfig& config = loadMineConfig()) {
    const std::set<std::string> family(config.familyAliases.begin(), config.familyAliases.end());
    std::map<std::string, std::vector<std::string>> mentions;

    auto note = [&](const std::string& name, const std::string& line) {
        std::string clean = normName(name);
        size_t sp = clean.find(' ');
        std::string first = (sp != std::string::npos) ? clean.substr(0, sp) : clean;
        if (family.contains(clean) || (family.contains(first) && clean.ends_with(config.familySurname))) {
            return;
        }
        mentions[name].push_back(line);
    };

    for (const auto& a : ctx.draft.add) {
        for (const auto& p : a.people) {
            std::string tagsStr;
            for (const auto& t : a.tags) tagsStr += t + " ";
            std::string detailsStr;
            for (size_t i = 0; i < a.details.size(); ++i) {
                if (i > 0) detailsStr += "; ";
                detailsStr += a.details[i];
            }
            note(p.name, a.date + " | " + a.title + " | " + tagsStr + "| " + detailsStr);
        }
    }
    for (const auto& u : ctx.draft.update) {
        auto e = ctx.db.get(u.id);
        for (const auto& p : u.addPeople) {
            std::string tagsStr;
            if (e) for (const auto& t : e->tags) tagsStr += t + " ";
            std::string detailsStr;
            for (size_t i = 0; i < u.addDetails.size(); ++i) {
                if (i > 0) detailsStr += "; ";
                detailsStr += u.addDetails[i];
            }
            note(p.name, (e ? e->date : "") + " | " + (e ? e->title : ("#" + std::to_string(u.id))) + " | " + tagsStr + "| " + detailsStr);
        }
    }

    if (mentions.empty()) return;

    auto t = current_tools();
    if (!t) return;

    // List contacts from Google
    slimr::Json listReq = slimr::Json::object_t{
        {"service", "people"},
        {"resource", slimr::Json::array_t{"people", "connections"}},
        {"method", "list"},
        {"params", slimr::Json::object_t{
            {"resourceName", "people/me"},
            {"personFields", "names"},
            {"pageSize", 200},
        }},
    };

    std::map<std::string, std::string> byName;
    std::vector<std::string> allNames;

    std::string pageToken;
    do {
        if (!pageToken.empty()) {
            listReq["params"]["pageToken"] = pageToken;
        }
        auto res = t->google(listReq);
        if (res.contains("connections") && res.at("connections").is_array()) {
            for (const auto& c : res.at("connections").get<slimr::Json::array_t>()) {
                std::string resName = c.contains("resourceName") ? c.at("resourceName").get<std::string>() : "";
                if (c.contains("names") && c.at("names").is_array()) {
                    for (const auto& n : c.at("names").get<slimr::Json::array_t>()) {
                        std::vector<std::string> fulls;
                        if (n.contains("displayName")) fulls.push_back(n.at("displayName").get<std::string>());
                        std::string given = n.contains("givenName") ? n.at("givenName").get<std::string>() : "";
                        std::string fam = n.contains("familyName") ? n.at("familyName").get<std::string>() : "";
                        if (!given.empty() && !fam.empty()) fulls.push_back(given + " " + fam);

                        for (const auto& full : fulls) {
                            if (full.empty()) continue;
                            std::string nfull = normName(full);
                            allNames.push_back(nfull);
                            if (full.find(' ') != std::string::npos) {
                                byName[nfull] = resName;
                            }
                        }
                    }
                }
            }
        }
        pageToken = (res.contains("nextPageToken") && res.at("nextPageToken").is_string())
                    ? res.at("nextPageToken").get<std::string>() : "";
    } while (!pageToken.empty());

    std::map<std::string, std::string> resolved;
    std::vector<std::string> unknown;

    for (const auto& [name, _lines] : mentions) {
        std::string bare = normName(stripTitle(name));
        if (bare.find(' ') == std::string::npos) continue; // never on first name alone

        auto hit = byName.find(bare);
        if (hit != byName.end()) {
            resolved[name] = hit->second;
            ctx.result.followUps.push_back("contact: link " + name + " -> " + hit->second);
            continue;
        }

        // Similar check
        std::vector<std::string> words;
        std::stringstream bss(bare);
        std::string w;
        while (bss >> w) words.push_back(w);

        std::string similar;
        for (const auto& k : allNames) {
            std::vector<std::string> kw;
            std::stringstream kss(k);
            while (kss >> w) kw.push_back(w);
            if (!kw.empty() && !words.empty() && kw[0] == words[0]) {
                if (std::find(kw.begin(), kw.end(), words.back()) != kw.end() ||
                    (k.find('(') != std::string::npos && k.find(')') != std::string::npos)) {
                    similar = k;
                    break;
                }
            }
        }

        if (!similar.empty()) {
            ctx.result.followUps.push_back("contact: " + name + " has no exact match (similar: \"" + similar + "\"); left alone");
        } else {
            unknown.push_back(name);
        }
    }

    if (!unknown.empty()) {
        std::string promptUser;
        for (const auto& n : unknown) {
            if (!promptUser.empty()) promptUser += "\n\n";
            promptUser += "## " + n + "\n";
            for (const auto& l : mentions[n]) promptUser += l + "\n";
        }

        auto reply = ctx.ask("contacts", "# Contacts", promptUser);
        std::stringstream ss(reply.text);
        std::string line;
        std::vector<slimr::Json> decisions;
        while (std::getline(ss, line)) {
            while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front()))) line.erase(line.begin());
            if (line.starts_with('{')) {
                auto parsed = slimr::parse_json<slimr::Json>(line);
                if (parsed) decisions.push_back(*parsed);
            }
        }

        std::map<std::string, std::string> groupCache;
        for (const auto& d : decisions) {
            bool create = d.contains("create") && d.at("create").is_boolean() && d.at("create").get<bool>();
            std::string dname = d.contains("name") ? d.at("name").get<std::string>() : "";
            std::string group = d.contains("group") ? d.at("group").get<std::string>() : "";
            std::string howWeKnow = d.contains("howWeKnow") ? d.at("howWeKnow").get<std::string>() : "";

            if (!create || dname.empty() || std::find(unknown.begin(), unknown.end(), dname) == unknown.end()) {
                continue;
            }
            if (std::find(config.contactGroups.begin(), config.contactGroups.end(), group) == config.contactGroups.end()) {
                continue;
            }

            std::string bio = "How we know them: " + scrub(howWeKnow);
            if (ctx.dryRun) {
                ctx.result.followUps.push_back("contact: create " + dname + " [" + group + "] \"" + bio + "\"");
                continue;
            }

            try {
                // Ensure group exists
                if (groupCache.empty()) {
                    auto gRes = t->google(slimr::Json::object_t{
                        {"service", "people"},
                        {"resource", slimr::Json::array_t{"contactGroups"}},
                        {"method", "list"},
                        {"params", slimr::Json::object_t{{"pageSize", 1000}}},
                    });
                    if (gRes.contains("contactGroups") && gRes.at("contactGroups").is_array()) {
                        for (const auto& grp : gRes.at("contactGroups").get<slimr::Json::array_t>()) {
                            std::string rName = grp.contains("resourceName") ? grp.at("resourceName").get<std::string>() : "";
                            if (grp.contains("name")) groupCache[grp.at("name").get<std::string>()] = rName;
                        }
                    }
                }
                std::string groupResName;
                auto git = groupCache.find(group);
                if (git != groupCache.end()) {
                    groupResName = git->second;
                } else {
                    auto cRes = t->google(slimr::Json::object_t{
                        {"service", "people"},
                        {"resource", slimr::Json::array_t{"contactGroups"}},
                        {"method", "create"},
                        {"body", slimr::Json::object_t{{"contactGroup", slimr::Json::object_t{{"name", group}}}}},
                    });
                    groupResName = cRes.contains("resourceName") ? cRes.at("resourceName").get<std::string>() : "";
                    groupCache[group] = groupResName;
                }

                std::string bareDname = stripTitle(dname);
                std::vector<std::string> parts;
                std::stringstream dnss(bareDname);
                std::string ppart;
                while (dnss >> ppart) parts.push_back(ppart);
                std::string given = !parts.empty() ? parts[0] : "";
                std::string fam;
                for (size_t pi = 1; pi < parts.size(); ++pi) {
                    if (!fam.empty()) fam += " ";
                    fam += parts[pi];
                }

                slimr::Json nameObj = slimr::Json::object_t{
                    {"givenName", given},
                    {"familyName", fam},
                };
                auto cCreated = t->google(slimr::Json::object_t{
                    {"service", "people"},
                    {"resource", slimr::Json::array_t{"people"}},
                    {"method", "createContact"},
                    {"params", slimr::Json::object_t{{"personFields", "names"}}},
                    {"body", slimr::Json::object_t{
                        {"names", slimr::Json::array_t{nameObj}},
                        {"biographies", slimr::Json::array_t{slimr::Json::object_t{{"value", bio}, {"contentType", "TEXT_PLAIN"}}}},
                        {"memberships", slimr::Json::array_t{slimr::Json::object_t{{"contactGroupMembership", slimr::Json::object_t{{"contactGroupResourceName", groupResName}}}}}},
                    }},
                });
                std::string createdResName = cCreated.contains("resourceName") ? cCreated.at("resourceName").get<std::string>() : "";
                if (createdResName.empty()) throw std::runtime_error("no resourceName returned");
                resolved[dname] = createdResName;
                ctx.result.followUps.push_back("contact: created " + dname + " [" + group + "] -> " + createdResName);
            } catch (const std::exception& e) {
                ctx.result.warnings.push_back("contacts: create " + dname + ": " + std::string(e.what()).substr(0, 200));
            }
        }
    }

    if (ctx.dryRun || resolved.empty()) return;

    std::set<int64_t> touchedEntries;
    for (int64_t id : ctx.result.added) touchedEntries.insert(id);
    for (int64_t id : ctx.result.updated) touchedEntries.insert(id);

    for (int64_t id : touchedEntries) {
        auto entry = ctx.db.get(id);
        if (!entry) continue;
        std::vector<Person> peopleToUpdate;
        for (const auto& p : entry->people) {
            if (!p.contact.has_value() || p.contact->empty()) {
                auto rit = resolved.find(p.name);
                if (rit != resolved.end()) {
                    peopleToUpdate.push_back(Person{.name = p.name, .contact = rit->second});
                }
            }
        }
        if (!peopleToUpdate.empty()) {
            ctx.db.update(id, EntryPatch{.addPeople = std::move(peopleToUpdate)});
        }
    }
}

inline void followUpsKidsDocuments(FollowUpContext& ctx, const MineConfig& config = loadMineConfig()) {
    auto t = current_tools();
    if (!t) return;

    for (const auto& item : ctx.triage) {
        if (!item.critical || item.attachments.empty() || !item.ids.gm) continue;
        const std::string& thread = *item.ids.gm;

        std::vector<std::string> kids;
        for (const auto& tag : item.tags) {
            if (config.kidsDrive.contains(tag)) {
                kids.push_back(tag);
            }
        }
        if (kids.empty()) continue;

        bool school = (item.docFolder && *item.docFolder == "school") ||
                      (!item.docFolder.has_value() && std::find(item.tags.begin(), item.tags.end(), "school") != item.tags.end() &&
                       std::none_of(item.tags.begin(), item.tags.end(), [](const std::string& tg) { return tg.starts_with("health"); }));

        for (const auto& filename : item.attachments) {
            // Find email in collected emails
            auto emailIt = std::find_if(ctx.collected.emails.begin(), ctx.collected.emails.end(), [&](const MinedEmail& em) {
                return em.threadId == thread && std::find(em.attachments.begin(), em.attachments.end(), filename) != em.attachments.end();
            });
            if (emailIt == ctx.collected.emails.end()) {
                ctx.result.followUps.push_back("drive: skip " + filename + " (not in this window's emails for gm:" + thread + ")");
                continue;
            }

            std::string ext;
            size_t dot = filename.find_last_of('.');
            if (dot != std::string::npos) ext = filename.substr(dot + 1);

            std::string stem = (item.attachments.size() > 1 && dot != std::string::npos) ? (" - " + filename.substr(0, dot)) : "";
            std::string rawTitle = item.date + " - " + item.title + stem;
            std::string title;
            for (char c : rawTitle) {
                if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' || c == '#' || c == '^' || c == '[' || c == ']') {
                    title += '-';
                } else {
                    title += c;
                }
            }
            std::string name = !ext.empty() ? (title + "." + ext) : title;

            for (const auto& kid : kids) {
                std::string folder = config.kidsDrive.at(kid) + "/" + (school ? std::string(config.kidsSchoolFolder) : "health");
                try {
                    // Check if file exists in Drive
                    slimr::Json listReq = slimr::Json::object_t{
                        {"service", "drive"},
                        {"resource", slimr::Json::array_t{"files"}},
                        {"method", "list"},
                        {"params", slimr::Json::object_t{
                            {"q", "name = '" + name + "'"},
                            {"pageSize", 10},
                        }},
                    };
                    auto listRes = t->google(listReq);
                    if (listRes.contains("files") && listRes.at("files").is_array() && !listRes.at("files").get<slimr::Json::array_t>().empty()) {
                        ctx.result.followUps.push_back("drive: skip " + folder + "/" + name + " (already there)");
                        continue;
                    }

                    if (ctx.dryRun) {
                        ctx.result.followUps.push_back("drive: upload " + filename + " -> " + folder + "/" + name + " (link on entry gm:" + thread + ")");
                        continue;
                    }

                    // Download attachment & upload to Drive
                    auto msgMeta = t->gmailMessage(emailIt->id);
                    std::string attachmentId;
                    for (const auto& att : msgMeta.attachments) {
                        if (att.filename == filename) {
                            attachmentId = att.attachmentId;
                            break;
                        }
                    }
                    if (attachmentId.empty()) {
                        throw std::runtime_error("attachment " + filename + " not found in message " + emailIt->id);
                    }
                    std::string savedPath = t->gmailAttachment(emailIt->id, attachmentId, filename);
                    auto upRes = t->driveUpload(savedPath, name, folder, "application/pdf");
                    std::string webViewLink = upRes.contains("webViewLink") ? upRes.at("webViewLink").get<std::string>() : "";
                    if (webViewLink.empty() && upRes.contains("id")) {
                        webViewLink = "https://drive.google.com/file/d/" + upRes.at("id").get<std::string>() + "/view";
                    }

                    auto entry = ctx.db.getBySource(Source{.kind = "gm", .id = thread});
                    if (entry) {
                        ctx.db.update(entry->id, EntryPatch{
                            .addLinks = {Link{.kind = "file", .label = title, .url = webViewLink}}
                        });
                        ctx.result.followUps.push_back("drive: uploaded " + folder + "/" + name + " (entry #" + std::to_string(entry->id) + ")");
                    } else {
                        ctx.result.warnings.push_back("kids' documents: no entry for gm:" + thread + " to link " + name);
                    }
                } catch (const std::exception& e) {
                    ctx.result.warnings.push_back("kids' documents: " + folder + "/" + name + ": " + std::string(e.what()).substr(0, 200));
                }
            }
        }
    }
}

inline void followUpsMedicalExpenses(FollowUpContext& ctx, const MineConfig& config = loadMineConfig()) {
    auto t = current_tools();
    if (!t) return;

    std::vector<ExpenseRow> rows;
    static const std::regex hex_re(R"(^[0-9a-fA-F]+$)");

    for (const auto& item : ctx.triage) {
        if (!item.expense) continue;
        if (item.ids.gm && !std::regex_match(*item.ids.gm, hex_re)) {
            ctx.result.warnings.push_back("medical expenses: row skipped, Gmail id is not hex");
            continue;
        }
        auto r = expenseRow(item, config);
        if (!r) {
            ctx.result.followUps.push_back("expense: ignore \"" + item.title + "\" (no family member, date or euro amount)");
            continue;
        }
        if (r->date >= config.medicalExpensesSince) {
            rows.push_back(*r);
        }
    }
    if (rows.empty()) return;

    try {
        auto noteRes = t->obsidian("read", slimr::Json::object_t{{"note", std::string(config.medicalExpensesNote)}});
        std::string content = noteRes.contains("content") ? noteRes.at("content").get<std::string>() : "";
        std::string path = noteRes.contains("path") ? noteRes.at("path").get<std::string>() : std::string(config.medicalExpensesNote);

        auto addRes = addExpenseRows(content, rows);
        for (const auto& r : addRes.skipped) {
            ctx.result.followUps.push_back("expense: skip " + r.date + " " + r.who + " " + r.provider + " €" + money(r.amount) + " (" + r.qualifies + ") (already listed)");
        }
        if (addRes.added.empty()) return;

        if (!ctx.dryRun) {
            t->obsidian("write", slimr::Json::object_t{{"path", path}, {"content", addRes.content}, {"overwrite", true}});
        }
        for (const auto& r : addRes.added) {
            ctx.result.followUps.push_back("expense: " + std::string(ctx.dryRun ? "add" : "added") + " " + r.date + " " + r.who + " " + r.provider + " €" + money(r.amount) + " (" + r.qualifies + ")");
        }
    } catch (const std::exception& e) {
        ctx.result.warnings.push_back("medical expenses: " + std::string(e.what()).substr(0, 200));
    }
}

inline void followUpsMeds(FollowUpContext& ctx, const MineConfig& config = loadMineConfig()) {
    auto t = current_tools();
    if (!t) return;

    std::set<std::string> people;
    auto addFrom = [&](const std::vector<std::string>& tags) {
        if (std::find(tags.begin(), tags.end(), "health/meds") != tags.end()) {
            for (const auto& tg : tags) {
                if (config.medsNotes.contains(tg)) people.insert(tg);
            }
        }
    };

    for (const auto& a : ctx.draft.add) addFrom(a.tags);
    for (const auto& u : ctx.draft.update) {
        auto existing = ctx.db.get(u.id);
        std::vector<std::string> combined = u.addTags;
        if (existing) for (const auto& tg : existing->tags) combined.push_back(tg);
        addFrom(combined);
    }

    for (const auto& who : people) {
        std::string path = config.medsNotes.at(who);
        auto entries = ctx.db.search(SearchFilters{.tags = {"health/meds", who}, .limit = 500});
        std::reverse(entries.begin(), entries.end()); // oldest first

        if (ctx.dryRun) {
            ctx.result.followUps.push_back("meds: rebuild ## Meds in " + path + " from " + std::to_string(entries.size()) + " logged health/meds entries plus this run's");
            continue;
        }

        try {
            auto noteRes = t->obsidian("read", slimr::Json::object_t{{"note", path}});
            std::string content = noteRes.contains("content") ? noteRes.at("content").get<std::string>() : "";
            std::string notePath = noteRes.contains("path") ? noteRes.at("path").get<std::string>() : path;

            std::string userPrompt = "Person: " + (config.whoNames.contains(who) ? config.whoNames.at(who) : who) + "\n\n# Current section\n" + content + "\n\n# health/meds entries\n";
            for (const auto& e : entries) {
                std::string detailsStr;
                for (size_t i = 0; i < e.details.size(); ++i) {
                    if (i > 0) detailsStr += "; ";
                    detailsStr += e.details[i];
                }
                userPrompt += e.date + " | " + e.title + " | " + detailsStr + "\n";
            }

            auto reply = ctx.ask("meds", "# Meds section", userPrompt);
            std::string section = reply.text;
            while (!section.empty() && std::isspace(static_cast<unsigned char>(section.front()))) section.erase(section.begin());
            while (!section.empty() && std::isspace(static_cast<unsigned char>(section.back()))) section.pop_back();

            if (!section.starts_with("## Meds") || section.find("### Current") == std::string::npos) {
                throw std::runtime_error("model reply is not a ## Meds section");
            }

            std::string newContent = replaceMedsSection(content, section);
            t->obsidian("write", slimr::Json::object_t{{"path", notePath}, {"content", newContent}, {"overwrite", true}});
            ctx.result.followUps.push_back("meds: rebuilt ## Meds in " + notePath);
        } catch (const std::exception& e) {
            ctx.result.warnings.push_back("meds: " + path + ": " + std::string(e.what()).substr(0, 200));
        }
    }
}

inline void runFollowUps(FollowUpContext& ctx, const MineConfig& config = loadMineConfig()) {
    followUpsContacts(ctx, config);
    followUpsKidsDocuments(ctx, config);
    followUpsMedicalExpenses(ctx, config);
    followUpsMeds(ctx, config);
}

// ============================================================================
// Collection, Triage, Draft & Apply
// ============================================================================

inline Collected collect(AppDb& db, const std::string& since, const std::string& until, const MineConfig& config = loadMineConfig()) {
    Collected c{.since = since, .until = until};
    auto t = current_tools();
    if (!t) return c;

    // Collect emails via google tool
    try {
        slimr::Json emailListReq = slimr::Json::object_t{
            {"service", "gmail"},
            {"resource", slimr::Json::array_t{"users", "messages"}},
            {"method", "list"},
            {"params", slimr::Json::object_t{{"userId", "me"}}},
        };
        auto emailListRes = t->google(emailListReq);
        std::vector<std::string> msgIds;
        if (emailListRes.contains("pages") && emailListRes.at("pages").is_array()) {
            for (const auto& p : emailListRes.at("pages").get<slimr::Json::array_t>()) {
                if (p.contains("messages") && p.at("messages").is_array()) {
                    for (const auto& m : p.at("messages").get<slimr::Json::array_t>()) {
                        if (m.contains("id")) msgIds.push_back(m.at("id").get<std::string>());
                    }
                }
            }
        }

        std::vector<GmailMessage> fetched;
        for (const auto& mid : msgIds) {
            try {
                fetched.push_back(t->gmailMessage(mid));
            } catch (const std::exception& e) {
                c.warnings.push_back("gmail " + mid + ": " + std::string(e.what()).substr(0, 200));
            }
        }

        // Noise filtering with Jev
        std::vector<JevQuestion> qs;
        for (const auto& m : fetched) {
            qs.push_back(JevQuestion{.state = "From: " + m.from + "\nSubject: " + m.subject + "\n\n" + m.body.substr(0, 600), .statement = "keep"});
        }
        auto scores = t->jev(qs);
        if (!scores) {
            c.warnings.push_back("JEV_API_KEY not set or Jev failed: no noise filter applied");
        }

        for (size_t i = 0; i < fetched.size(); ++i) {
            const auto& m = fetched[i];
            bool keep = false;
            for (const auto& d : config.keepDomains) {
                if (m.from.find(d) != std::string::npos) {
                    keep = true;
                    break;
                }
            }
            if (!keep && scores && i < scores->size() && (*scores)[i] < JEV_DROP_BELOW) {
                continue; // drop noise
            }

            auto logged = db.getBySource(Source{.kind = "gm", .id = m.threadId});
            std::vector<std::string> attNames;
            for (const auto& att : m.attachments) attNames.push_back(att.filename);

            c.emails.push_back(MinedEmail{
                .id = m.id,
                .threadId = m.threadId,
                .from = m.from,
                .to = m.to,
                .date = m.date,
                .subject = m.subject,
                .body = m.body.substr(0, 6000),
                .attachments = std::move(attNames),
                .loggedEntryId = logged ? std::optional<int64_t>(logged->id) : std::nullopt,
            });
        }
    } catch (const std::exception& e) {
        c.warnings.push_back("collect emails: " + std::string(e.what()).substr(0, 200));
    }

    // Collect calendar events
    try {
        std::string suggestId = suggestCalendarId(config);
        auto mapWithSuggest = config.calendars;
        mapWithSuggest[suggestId] = SUGGEST_CALENDAR;

        for (const auto& [calId, calName] : mapWithSuggest) {
            slimr::Json calReq = slimr::Json::object_t{
                {"service", "calendar"},
                {"resource", slimr::Json::array_t{"events"}},
                {"method", "list"},
                {"params", slimr::Json::object_t{{"calendarId", calId}}},
            };
            auto calRes = t->google(calReq);
            if (calRes.contains("pages") && calRes.at("pages").is_array()) {
                for (const auto& p : calRes.at("pages").get<slimr::Json::array_t>()) {
                    if (p.contains("items") && p.at("items").is_array()) {
                        for (const auto& item : p.at("items").get<slimr::Json::array_t>()) {
                            std::string eid = item.contains("id") ? item.at("id").get<std::string>() : "";
                            std::string summary = item.contains("summary") ? item.at("summary").get<std::string>() : "";
                            std::string startDt;
                            if (item.contains("start") && item.at("start").contains("dateTime")) {
                                startDt = item.at("start").at("dateTime").get<std::string>();
                            }
                            std::string endDt;
                            if (item.contains("end") && item.at("end").contains("dateTime")) {
                                endDt = item.at("end").at("dateTime").get<std::string>();
                            }
                            c.events.push_back(MinedEvent{
                                .id = eid,
                                .calendars = {calName},
                                .start = startDt,
                                .end = endDt,
                                .title = summary,
                                .location = "",
                                .description = "",
                            });
                        }
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        c.warnings.push_back("collect events: " + std::string(e.what()).substr(0, 200));
    }

    // Collect Deadlines
    try {
        slimr::Json dlReq = slimr::Json::object_t{
            {"service", "tasks"},
            {"resource", slimr::Json::array_t{"tasks"}},
            {"method", "list"},
            {"params", slimr::Json::object_t{{"tasklist", std::string(config.deadlinesTasklist)}}},
        };
        auto dlRes = t->google(dlReq);
        if (dlRes.contains("items") && dlRes.at("items").is_array()) {
            for (const auto& item : dlRes.at("items").get<slimr::Json::array_t>()) {
                std::string dtitle = item.contains("title") ? item.at("title").get<std::string>() : "";
                std::optional<std::string> ddue = std::nullopt;
                if (item.contains("due") && item.at("due").is_string()) {
                    ddue = item.at("due").get<std::string>().substr(0, 10);
                }
                c.openDeadlines.push_back(OpenDeadline{.title = dtitle, .due = ddue});
            }
        }
    } catch (const std::exception& e) {
        c.warnings.push_back("deadlines: " + std::string(e.what()).substr(0, 200));
    }

    // WhatsApp messages
    try {
        auto wchats = t->whatsappChats();
        struct WantedChat {
            WhatsAppChatConfig cfg;
            std::string jid;
        };
        std::vector<WantedChat> wanted;
        for (const auto& cfg : config.whatsappChats) {
            std::vector<WhatsAppChat> matches;
            for (const auto& c : wchats) {
                if (c.name == cfg.name && c.is_group == !cfg.direct) {
                    matches.push_back(c);
                }
            }
            std::sort(matches.begin(), matches.end(), [](const WhatsAppChat& a, const WhatsAppChat& b) {
                return a.message_count > b.message_count;
            });
            if (matches.empty()) {
                c.warnings.push_back("whatsapp chat not found: " + cfg.name + " (renamed for the new school year?)");
            } else {
                wanted.push_back(WantedChat{.cfg = cfg, .jid = matches[0].jid});
            }
        }
        if (!wanted.empty()) {
            std::vector<std::string> jids;
            for (const auto& w : wanted) jids.push_back(w.jid);
            auto wmsgs = t->whatsappExport(jids, since, until);
            for (const auto& w : wanted) {
                std::vector<std::string> lns;
                for (const auto& wm : wmsgs) {
                    if (wm.chat_jid == w.jid && !wm.content.empty()) {
                        std::string cleanContent;
                        bool in_sp = false;
                        for (char ch : wm.content) {
                            if (std::isspace(static_cast<unsigned char>(ch))) {
                                if (!in_sp) { cleanContent += ' '; in_sp = true; }
                            } else {
                                cleanContent += ch;
                                in_sp = false;
                            }
                        }
                        if (cleanContent.size() > 1500) cleanContent = cleanContent.substr(0, 1500);
                        lns.push_back(wm.ts_utc + " | " + (wm.is_from_me ? config.selfName : wm.sender_name) + " | " + cleanContent + " [wa:" + wm.id + "]");
                    }
                }
                if (!lns.empty()) {
                    c.whatsapp.push_back(WhatsAppChatLines{.chat = w.cfg.name, .kind = w.cfg.kind, .lines = std::move(lns)});
                }
            }
        }
    } catch (const std::exception& e) {
        c.warnings.push_back("whatsapp: " + std::string(e.what()).substr(0, 200));
    }

    // Nearby logged entries
    auto nearbyEntries = db.search(SearchFilters{
        .from = dotted(addDays(since, -10)),
        .to = dotted(addDays(until, 30)),
        .limit = 500
    });
    for (const auto& ne : nearbyEntries) {
        std::vector<std::string> scs;
        for (const auto& s : ne.sources) scs.push_back(s.kind + ":" + s.id);
        c.nearby.push_back(NearbyEntry{
            .id = ne.id,
            .date = ne.date,
            .title = ne.title,
            .tags = ne.tags,
            .sources = std::move(scs),
        });
    }

    c.stats["emails"] = static_cast<int64_t>(c.emails.size());
    int64_t waLines = 0;
    for (const auto& w : c.whatsapp) waLines += w.lines.size();
    c.stats["whatsappLines"] = waLines;
    c.stats["events"] = static_cast<int64_t>(c.events.size());
    c.stats["eventsAlreadyLogged"] = 0;

    return c;
}

inline std::vector<TriageItem> runTriage(const Collected& c, UsageMap& usage, std::vector<std::string>& warnings) {
    std::string systemPrompt = "# Triage\nRead emails and chats and produce JSON lines.";
    std::string userPrompt = "Today is " + format_iso_now().substr(0, 10) + ".\n\n";
    for (const auto& e : c.emails) {
        userPrompt += formatEmail(e) + "\n\n";
    }

    auto reply = callModel("triage", systemPrompt, userPrompt, usage, warnings);
    std::vector<TriageItem> items;
    std::stringstream ss(reply.text);
    std::string line;
    while (std::getline(ss, line)) {
        while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front()))) line.erase(line.begin());
        if (!line.starts_with('{')) continue;
        auto parsed = slimr::parse_json<slimr::Json>(line);
        if (parsed) {
            const auto& j = *parsed;
            TriageItem item;
            if (j.contains("date")) item.date = j.at("date").get<std::string>();
            if (j.contains("title")) item.title = j.at("title").get<std::string>();
            if (j.contains("tags") && j.at("tags").is_array()) {
                for (const auto& tg : j.at("tags").get<slimr::Json::array_t>()) {
                    if (tg.is_string()) item.tags.push_back(tg.get<std::string>());
                }
            }
            if (j.contains("details") && j.at("details").is_array()) {
                for (const auto& d : j.at("details").get<slimr::Json::array_t>()) {
                    if (d.is_string()) item.details.push_back(d.get<std::string>());
                }
            }
            if (j.contains("people") && j.at("people").is_array()) {
                for (const auto& p : j.at("people").get<slimr::Json::array_t>()) {
                    if (p.is_string()) item.people.push_back(p.get<std::string>());
                }
            }
            if (j.contains("ids") && j.at("ids").is_object()) {
                if (j.at("ids").contains("gm") && j.at("ids").at("gm").is_string()) {
                    item.ids.gm = j.at("ids").at("gm").get<std::string>();
                }
            }
            if (j.contains("subject")) item.subject = j.at("subject").get<std::string>();
            if (j.contains("critical") && j.at("critical").is_boolean()) {
                item.critical = j.at("critical").get<bool>();
            }
            if (j.contains("docFolder") && j.at("docFolder").is_string()) {
                item.docFolder = j.at("docFolder").get<std::string>();
            }
            if (j.contains("attachments") && j.at("attachments").is_array()) {
                for (const auto& att : j.at("attachments").get<slimr::Json::array_t>()) {
                    if (att.is_string()) item.attachments.push_back(att.get<std::string>());
                }
            }
            if (j.contains("expense") && j.at("expense").is_object()) {
                const auto& ex = j.at("expense");
                TriageExpense te;
                if (ex.contains("date")) te.date = ex.at("date").get<std::string>();
                if (ex.contains("who")) te.who = ex.at("who").get<std::string>();
                if (ex.contains("provider")) te.provider = ex.at("provider").get<std::string>();
                if (ex.contains("item")) te.item = ex.at("item").get<std::string>();
                if (ex.contains("amount")) {
                    const auto& amtVal = ex.at("amount");
                    if (amtVal.is_double()) {
                        te.amount = amtVal.get<double>();
                    } else if (amtVal.is_int64()) {
                        te.amount = static_cast<double>(amtVal.get<int64_t>());
                    } else if (amtVal.is_string()) {
                        te.amount = parseMoney(amtVal.get<std::string>());
                    }
                }
                if (ex.contains("currency")) te.currency = ex.at("currency").get<std::string>();
                if (ex.contains("qualifies")) te.qualifies = ex.at("qualifies").get<std::string>();
                item.expense = te;
            }
            items.push_back(std::move(item));
        } else {
            warnings.push_back("triage line not JSON: " + line.substr(0, 120));
        }
    }
    return items;
}

inline DraftOutput runDraft(const Collected& c, const std::vector<TriageItem>& triage, UsageMap& usage, std::vector<std::string>& warnings) {
    std::string systemPrompt = "# Draft\nDraft log additions and updates.";
    std::string userPrompt = "Window: " + c.since + " to " + c.until + "\n";
    for (const auto& t : triage) {
        userPrompt += t.date + " | " + t.title + "\n";
    }

    auto reply = callModel("draft", systemPrompt, userPrompt, usage, warnings);
    std::string text = reply.text;
    size_t first = text.find('{');
    size_t last = text.rfind('}');
    if (first == std::string::npos || last == std::string::npos || last < first) {
        throw std::runtime_error("Drafter returned invalid JSON");
    }
    std::string jsonStr = text.substr(first, last - first + 1);
    auto parsed = slimr::parse_json<slimr::Json>(jsonStr);
    if (!parsed) {
        throw std::runtime_error("Failed to parse draft JSON");
    }

    const auto& j = *parsed;
    DraftOutput draft;
    if (j.contains("add") && j.at("add").is_array()) {
        for (const auto& item : j.at("add").get<slimr::Json::array_t>()) {
            DraftAdd da;
            if (item.contains("date")) da.date = item.at("date").get<std::string>();
            if (item.contains("title")) da.title = item.at("title").get<std::string>();
            if (item.contains("tags") && item.at("tags").is_array()) {
                for (const auto& tg : item.at("tags").get<slimr::Json::array_t>()) {
                    if (tg.is_string()) da.tags.push_back(tg.get<std::string>());
                }
            }
            if (item.contains("details") && item.at("details").is_array()) {
                for (const auto& d : item.at("details").get<slimr::Json::array_t>()) {
                    if (d.is_string()) da.details.push_back(d.get<std::string>());
                }
            }
            if (item.contains("people") && item.at("people").is_array()) {
                for (const auto& p : item.at("people").get<slimr::Json::array_t>()) {
                    Person person;
                    if (p.contains("name")) person.name = p.at("name").get<std::string>();
                    if (p.contains("contact") && p.at("contact").is_string()) {
                        person.contact = p.at("contact").get<std::string>();
                    }
                    da.people.push_back(std::move(person));
                }
            }
            if (item.contains("sources") && item.at("sources").is_array()) {
                for (const auto& s : item.at("sources").get<slimr::Json::array_t>()) {
                    Source src;
                    if (s.contains("kind")) src.kind = s.at("kind").get<std::string>();
                    if (s.contains("id")) src.id = s.at("id").get<std::string>();
                    da.sources.push_back(std::move(src));
                }
            }
            if (item.contains("links") && item.at("links").is_array()) {
                for (const auto& l : item.at("links").get<slimr::Json::array_t>()) {
                    Link link;
                    if (l.contains("kind")) link.kind = l.at("kind").get<std::string>();
                    if (l.contains("label")) link.label = l.at("label").get<std::string>();
                    if (l.contains("url")) link.url = l.at("url").get<std::string>();
                    da.links.push_back(std::move(link));
                }
            }
            draft.add.push_back(std::move(da));
        }
    }
    if (j.contains("update") && j.at("update").is_array()) {
        for (const auto& item : j.at("update").get<slimr::Json::array_t>()) {
            DraftUpdate du;
            if (item.contains("id")) du.id = item.at("id").get<int64_t>();
            if (item.contains("addDetails") && item.at("addDetails").is_array()) {
                for (const auto& d : item.at("addDetails").get<slimr::Json::array_t>()) {
                    if (d.is_string()) du.addDetails.push_back(d.get<std::string>());
                }
            }
            if (item.contains("addTags") && item.at("addTags").is_array()) {
                for (const auto& tg : item.at("addTags").get<slimr::Json::array_t>()) {
                    if (tg.is_string()) du.addTags.push_back(tg.get<std::string>());
                }
            }
            if (item.contains("addPeople") && item.at("addPeople").is_array()) {
                for (const auto& p : item.at("addPeople").get<slimr::Json::array_t>()) {
                    Person person;
                    if (p.contains("name")) person.name = p.at("name").get<std::string>();
                    if (p.contains("contact") && p.at("contact").is_string()) {
                        person.contact = p.at("contact").get<std::string>();
                    }
                    du.addPeople.push_back(std::move(person));
                }
            }
            if (item.contains("addSources") && item.at("addSources").is_array()) {
                for (const auto& s : item.at("addSources").get<slimr::Json::array_t>()) {
                    Source src;
                    if (s.contains("kind")) src.kind = s.at("kind").get<std::string>();
                    if (s.contains("id")) src.id = s.at("id").get<std::string>();
                    du.addSources.push_back(std::move(src));
                }
            }
            draft.update.push_back(std::move(du));
        }
    }
    if (j.contains("suggestEvents") && j.at("suggestEvents").is_array()) {
        for (const auto& item : j.at("suggestEvents").get<slimr::Json::array_t>()) {
            DraftSuggestEvent dse;
            if (item.contains("title")) dse.title = item.at("title").get<std::string>();
            if (item.contains("start")) dse.start = item.at("start").get<std::string>();
            if (item.contains("end") && item.at("end").is_string()) dse.end = item.at("end").get<std::string>();
            if (item.contains("allDay") && item.at("allDay").is_boolean()) dse.allDay = item.at("allDay").get<bool>();
            if (item.contains("location")) dse.location = item.at("location").get<std::string>();
            if (item.contains("description")) dse.description = item.at("description").get<std::string>();
            draft.suggestEvents.push_back(std::move(dse));
        }
    }
    if (j.contains("deadlines") && j.at("deadlines").is_array()) {
        for (const auto& item : j.at("deadlines").get<slimr::Json::array_t>()) {
            DraftDeadline ddl;
            if (item.contains("title")) ddl.title = item.at("title").get<std::string>();
            if (item.contains("due")) ddl.due = item.at("due").get<std::string>();
            if (item.contains("notes")) ddl.notes = item.at("notes").get<std::string>();
            draft.deadlines.push_back(std::move(ddl));
        }
    }
    if (j.contains("skipped") && j.at("skipped").is_array()) {
        for (const auto& item : j.at("skipped").get<slimr::Json::array_t>()) {
            if (item.is_string()) draft.skipped.push_back(item.get<std::string>());
        }
    }
    return draft;
}

inline void applyDraft(
    AppDb& db,
    const Collected& c,
    const std::vector<TriageItem>& triage,
    const DraftOutput& draft,
    MineResult& result,
    const MineConfig& config = loadMineConfig()
) {
    auto t = current_tools();
    std::map<std::string, std::string> subjects;
    for (const auto& e : c.emails) subjects[e.threadId] = e.subject;
    for (const auto& tr : triage) {
        if (tr.ids.gm && tr.subject) subjects[*tr.ids.gm] = *tr.subject;
    }

    auto emailLinks = [&](const std::vector<Source>& sources) -> std::vector<Link> {
        std::vector<Link> lks;
        for (const auto& s : sources) {
            if (s.kind == "gm") {
                auto it = subjects.find(s.id);
                std::string label = (it != subjects.end()) ? it->second : "email";
                lks.push_back(Link{
                    .kind = "email",
                    .label = label,
                    .url = "https://mail.google.com/mail/u/?authuser=" + std::string(config.gmailAccount) + "#all/" + s.id
                });
            }
        }
        return lks;
    };

    std::vector<NewEntry> entries;
    for (const auto& a : draft.add) {
        std::vector<Person> sanitizedPeople;
        for (const auto& p : a.people) {
            sanitizedPeople.push_back(Person{
                .name = p.name,
                .contact = sanitizeContact(p.contact, result.warnings)
            });
        }
        auto sanitizedLks = sanitizeLinks(a.links, result.warnings);
        auto emlLinks = emailLinks(a.sources);
        sanitizedLks.insert(sanitizedLks.end(), emlLinks.begin(), emlLinks.end());

        entries.push_back(NewEntry{
            .date = a.date,
            .title = a.title,
            .tags = a.tags,
            .details = a.details,
            .people = std::move(sanitizedPeople),
            .sources = a.sources,
            .links = std::move(sanitizedLks),
        });
    }

    auto addRes = db.add(entries);
    result.added = addRes.added;
    result.skippedDuplicates = static_cast<int64_t>(addRes.skipped.size());

    for (const auto& sk : addRes.skipped) {
        if (sk.index >= 0 && sk.index < static_cast<int64_t>(entries.size())) {
            const auto& entry = entries[sk.index];
            auto existing = db.get(sk.existingId);
            std::vector<std::string> newDetails;
            for (const auto& d : entry.details) {
                if (!existing || std::find(existing->details.begin(), existing->details.end(), d) == existing->details.end()) {
                    newDetails.push_back(d);
                }
            }
            std::vector<Source> freeSources;
            for (const auto& src : entry.sources) {
                if (!db.getBySource(src)) {
                    freeSources.push_back(src);
                }
            }
            if (!newDetails.empty() || !freeSources.empty()) {
                std::vector<std::string> allDetails = existing ? existing->details : std::vector<std::string>{};
                allDetails.insert(allDetails.end(), newDetails.begin(), newDetails.end());
                db.update(sk.existingId, EntryPatch{
                    .details = allDetails,
                    .addTags = entry.tags,
                    .addSources = freeSources,
                    .addLinks = emailLinks(freeSources),
                });
            }
        }
    }

    for (const auto& u : draft.update) {
        auto existing = db.get(u.id);
        if (!existing) {
            result.warnings.push_back("draft updated missing entry #" + std::to_string(u.id));
            continue;
        }
        std::vector<Source> freeSources;
        for (const auto& src : u.addSources) {
            auto owner = db.getBySource(src);
            if (!owner || owner->id == u.id) {
                freeSources.push_back(src);
            }
        }
        std::vector<Person> sanitizedPeople;
        for (const auto& p : u.addPeople) {
            sanitizedPeople.push_back(Person{
                .name = p.name,
                .contact = sanitizeContact(p.contact, result.warnings)
            });
        }
        std::vector<std::string> allDetails = existing->details;
        for (const auto& d : u.addDetails) {
            if (std::find(allDetails.begin(), allDetails.end(), d) == allDetails.end()) {
                allDetails.push_back(d);
            }
        }
        db.update(u.id, EntryPatch{
            .details = allDetails,
            .addTags = u.addTags,
            .addPeople = std::move(sanitizedPeople),
            .addSources = freeSources,
            .addLinks = emailLinks(freeSources),
        });
        result.updated.push_back(u.id);
    }

    // Google Tasks deadlines
    if (t) {
        std::set<std::string> openDeadlines;
        for (const auto& odl : c.openDeadlines) {
            std::string lower(odl.title);
            for (char& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            openDeadlines.insert(lower);
        }

        static const std::regex due_re(R"(^\d{4}-\d{2}-\d{2}$)");
        for (const auto& d : draft.deadlines) {
            std::string lower(d.title);
            for (char& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if (openDeadlines.contains(lower) || !std::regex_match(d.due, due_re)) {
                continue;
            }
            try {
                slimr::Json body = slimr::Json::object_t{
                    {"title", d.title},
                    {"due", d.due + "T00:00:00.000Z"},
                    {"notes", (d.notes.empty() ? "" : (d.notes + "\n")) + config.deadlineNote},
                };
                t->google(slimr::Json::object_t{
                    {"service", "tasks"},
                    {"resource", slimr::Json::array_t{"tasks"}},
                    {"method", "insert"},
                    {"params", slimr::Json::object_t{{"tasklist", std::string(config.deadlinesTasklist)}}},
                    {"body", body},
                });
                result.deadlinesCreated.push_back(d.due + " " + d.title);
            } catch (const std::exception& e) {
                result.warnings.push_back("deadline \"" + d.title + "\": " + std::string(e.what()).substr(0, 200));
            }
        }
    }

    // Suggested events in AI-Suggests calendar
    if (t && !draft.suggestEvents.empty()) {
        std::string calId = suggestCalendarId(config);
        std::string today = format_iso_now().substr(0, 10);
        for (const auto& s : draft.suggestEvents) {
            std::string day = s.start.substr(0, 10);
            if (day < today) continue;
            bool existsUpcoming = false;
            for (const auto& u : c.upcoming) {
                if (u.start.substr(0, 10) == day && similar(u.title, s.title)) {
                    existsUpcoming = true;
                    break;
                }
            }
            if (existsUpcoming) continue;

            bool allDay = s.allDay || (s.start.find('T') == std::string::npos);
            slimr::Json startObj = allDay ? slimr::Json::object_t{{"date", day}}
                                          : slimr::Json::object_t{{"dateTime", s.start.substr(0, 16) + ":00"}, {"timeZone", config.timeZone}};
            std::string endVal = s.end ? *s.end : (allDay ? addDays(day, 1) : defaultEnd(s.start));
            slimr::Json endObj = allDay ? slimr::Json::object_t{{"date", endVal.substr(0, 10)}}
                                        : slimr::Json::object_t{{"dateTime", endVal.substr(0, 16) + ":00"}, {"timeZone", config.timeZone}};

            try {
                slimr::Json eventBody = slimr::Json::object_t{
                    {"summary", s.title},
                    {"location", s.location},
                    {"description", (s.description.empty() ? "" : (s.description + "\n\n")) + config.eventSuggestionNote},
                    {"start", startObj},
                    {"end", endObj},
                };
                t->google(slimr::Json::object_t{
                    {"service", "calendar"},
                    {"resource", slimr::Json::array_t{"events"}},
                    {"method", "insert"},
                    {"params", slimr::Json::object_t{{"calendarId", calId}}},
                    {"body", eventBody},
                });
                result.eventsSuggested.push_back(s.start + " " + s.title);
            } catch (const std::exception& e) {
                result.warnings.push_back("suggest \"" + s.title + "\": " + std::string(e.what()).substr(0, 200));
            }
        }
    }
}

/// Run the mining job: collect, triage, draft, apply, follow-ups.
inline MineResult runMine(AppDb& db, const MineOptions& options = {}, const MineConfig& config = loadMineConfig()) {
    (void)suggestCalendarId(config);

    std::string today = format_iso_now().substr(0, 10);
    std::string until = options.until.value_or(today);
    auto lastUntilOpt = db.lastSuccessfulUntil();
    std::string since;
    if (options.since) {
        since = *options.since;
    } else if (lastUntilOpt && !lastUntilOpt->empty()) {
        std::string lu = *lastUntilOpt;
        for (char& c : lu) if (c == '.') c = '-';
        since = addDays(lu, -1);
    } else {
        since = addDays(until, -3);
    }

    bool dryRun = options.dryRun;
    std::optional<int64_t> runId = std::nullopt;
    if (!dryRun) {
        runId = db.runStart(dotted(since), dotted(until));
    }

    UsageMap usage;
    MineResult result{
        .since = since,
        .until = until,
        .dryRun = dryRun,
    };

    try {
        Collected collected = collect(db, since, until, config);
        for (auto& w : collected.warnings) result.warnings.push_back(std::move(w));

        for (const auto& [k, v] : collected.stats) {
            result.stats[k] = v;
        }

        auto triage = runTriage(collected, usage, result.warnings);
        result.stats["triageItems"] = static_cast<int64_t>(triage.size());

        if (triage.empty() && collected.events.empty()) {
            slimr::Json usageObj = slimr::Json::object_t{};
            for (const auto& [k, v] : usage) {
                usageObj[k] = slimr::Json::object_t{
                    {"model", v.model},
                    {"calls", v.calls},
                    {"input", v.input},
                    {"output", v.output},
                    {"reasoning", v.reasoning},
                };
            }
            result.stats["usage"] = usageObj;
            if (runId) {
                db.runFinish(*runId, "ok", result.stats);
            }
            return result;
        }

        auto draft = runDraft(collected, triage, usage, result.warnings);
        result.draft = draft;

        slimr::Json usageObj = slimr::Json::object_t{};
        for (const auto& [k, v] : usage) {
            usageObj[k] = slimr::Json::object_t{
                {"model", v.model},
                {"calls", v.calls},
                {"input", v.input},
                {"output", v.output},
                {"reasoning", v.reasoning},
            };
        }
        result.stats["usage"] = usageObj;

        if (!dryRun) {
            applyDraft(db, collected, triage, draft, result, config);
        }

        FollowUpContext fuCtx{
            .db = db,
            .collected = collected,
            .triage = triage,
            .draft = draft,
            .result = result,
            .dryRun = dryRun,
            .ask = [&](const std::string& step, const std::string& system, const std::string& user) {
                return callModel(step, system, user, usage, result.warnings);
            }
        };
        runFollowUps(fuCtx, config);

        slimr::Json usageObjFinal = slimr::Json::object_t{};
        for (const auto& [k, v] : usage) {
            usageObjFinal[k] = slimr::Json::object_t{
                {"model", v.model},
                {"calls", v.calls},
                {"input", v.input},
                {"output", v.output},
                {"reasoning", v.reasoning},
            };
        }
        result.stats["usage"] = usageObjFinal;

        if (runId) {
            result.stats["added"] = static_cast<int64_t>(result.added.size());
            result.stats["updated"] = static_cast<int64_t>(result.updated.size());
            result.stats["skippedDuplicates"] = result.skippedDuplicates;
            db.runFinish(*runId, "ok", result.stats);
        }

        return result;
    } catch (const std::exception& e) {
        if (runId) {
            db.runFinish(*runId, "error", result.stats, e.what());
        }
        throw;
    }
}

/// Convenience alias
inline MineResult mine(AppDb& db, const MineOptions& options = {}) {
    return runMine(db, options);
}

// Overloads for app.hpp Tool callers
template <typename T>
requires requires(const T& t) { t.dryRun.get(); }
inline MineResult runMine(AppDb& db, const T& in) {
    MineOptions opt;
    if (in.since.get().has_value() && in.since.get()->has_value()) {
        opt.since = *in.since.get();
    }
    if (in.until.get().has_value() && in.until.get()->has_value()) {
        opt.until = *in.until.get();
    }
    if (in.dryRun.get().has_value() && in.dryRun.get()->has_value()) {
        opt.dryRun = *in.dryRun.get();
    }
    return runMine(db, opt);
}

template <typename T>
requires requires(const T& t) { t.dryRun.get(); }
inline MineResult mine(AppDb& db, const T& in) {
    return runMine(db, in);
}

template <typename T>
requires requires(const T& t) { t.destPath.get(); }
inline BackupResult backupLog(AppDb& db, const T&) {
    return backupLog(db);
}

} // namespace personal_log
