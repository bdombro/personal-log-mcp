#pragma once

#include "db.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace personal_log {

/// Lines that were neither entries, details, headings nor the intro line.
struct UnparsedLine {
    int64_t line = 0;
    std::string text;

    bool operator==(const UnparsedLine&) const = default;
};

/// Result of parsing one year note.
struct ParsedYear {
    std::vector<NewEntry> entries;
    std::vector<UnparsedLine> unparsed;

    bool operator==(const ParsedYear&) const = default;
};

namespace detail {

inline std::string trim(std::string_view s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) ++start;
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
    return std::string(s.substr(start, end - start));
}

inline void addDetail(NewEntry& entry, const std::string& text) {
    if (text.starts_with("👥")) {
        static const std::regex wikiRegex(R"(\[\[([^\]|#]+)(?:[#|][^\]]*)?\]\])");
        auto it = std::sregex_iterator(text.begin(), text.end(), wikiRegex);
        auto end = std::sregex_iterator();
        for (; it != end; ++it) {
            std::string name = trim((*it)[1].str());
            bool exists = false;
            for (const auto& p : entry.people) {
                if (p.name == name) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                entry.people.push_back(Person{.name = name, .contact = std::nullopt});
            }
        }
        return;
    }

    std::string kind;
    if (text.starts_with("✉️")) kind = "email";
    else if (text.starts_with("📎")) kind = "file";

    if (!kind.empty()) {
        static const std::regex linkRegex(R"(\[([^\]]*)\]\(([^)\s]+)\))");
        auto it = std::sregex_iterator(text.begin(), text.end(), linkRegex);
        auto end = std::sregex_iterator();
        bool found = false;
        for (; it != end; ++it) {
            found = true;
            std::string label = (*it)[1].str();
            std::string url = (*it)[2].str();
            bool exists = false;
            for (const auto& l : entry.links) {
                if (l.url == url) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                entry.links.push_back(Link{.kind = kind, .label = label, .url = url});
            }
        }
        if (found) return;
    }

    entry.details.push_back(text);
}

inline NewEntry parseEntryLine(const std::string& date, const std::string& rest) {
    std::vector<Source> sources;
    static const std::regex commentRegex(R"(%%(.*?)%%)");
    static const std::regex sourceRegex(R"(^(cal|gm|wa):(.+)$)");

    auto it = std::sregex_iterator(rest.begin(), rest.end(), commentRegex);
    auto end = std::sregex_iterator();
    for (; it != end; ++it) {
        std::string ids = (*it)[1].str();
        std::istringstream iss(ids);
        std::string token;
        while (iss >> token) {
            std::smatch sm;
            if (std::regex_match(token, sm, sourceRegex)) {
                sources.push_back(Source{.kind = sm[1].str(), .id = sm[2].str()});
            }
        }
    }

    std::string withoutIds = std::regex_replace(rest, commentRegex, "");

    std::istringstream iss(withoutIds);
    std::vector<std::string> words;
    std::string w;
    while (iss >> w) {
        words.push_back(w);
    }

    std::vector<std::string> tags;
    while (words.size() > 1 && words.back().starts_with('#')) {
        std::string tag = words.back().substr(1);
        for (char& c : tag) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        tags.push_back(tag);
        words.pop_back();
    }
    std::reverse(tags.begin(), tags.end());

    std::string title;
    for (size_t i = 0; i < words.size(); ++i) {
        if (i > 0) title += " ";
        title += words[i];
    }

    return NewEntry{
        .date = date,
        .title = title,
        .tags = std::move(tags),
        .details = {},
        .people = {},
        .sources = std::move(sources),
        .links = {}
    };
}

} // namespace detail

/// Parse one year note's text.
inline ParsedYear parseYearNote(std::string_view text) {
    ParsedYear result;
    NewEntry* current = nullptr;

    static const std::regex introRegex(R"(^Family log for \d{4}\.)");
    static const std::regex entryRegex(R"(^- (\d{4}\.\d{2}(?:\.\d{2})?) (.*)$)");
    static const std::regex detailRegex(R"(^\t+- (.*)$)");

    std::istringstream iss{std::string(text)};
    std::string raw;
    int64_t lineNumber = 0;

    while (std::getline(iss, raw)) {
        ++lineNumber;
        if (!raw.empty() && raw.back() == '\r') {
            raw.pop_back();
        }

        std::string trimmed = detail::trim(raw);
        if (trimmed.empty() || trimmed.starts_with('#') || std::regex_search(raw, introRegex)) {
            continue;
        }

        std::smatch entryMatch;
        if (std::regex_match(raw, entryMatch, entryRegex)) {
            result.entries.push_back(detail::parseEntryLine(entryMatch[1].str(), entryMatch[2].str()));
            current = &result.entries.back();
            continue;
        }

        std::smatch detailMatch;
        if (std::regex_match(raw, detailMatch, detailRegex) && current) {
            detail::addDetail(*current, detail::trim(detailMatch[1].str()));
            continue;
        }

        result.unparsed.push_back(UnparsedLine{.line = lineNumber, .text = raw});
    }

    return result;
}

} // namespace personal_log
