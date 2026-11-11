#pragma once

#include "json.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace personal_log {

struct WhatsAppChatConfig {
    std::string name;
    std::string kind;
    bool direct = false;
};

struct MineConfig {
    std::map<std::string, std::string> calendars;
    std::string suggestCalendarId;
    std::string deadlinesTasklist;
    std::vector<WhatsAppChatConfig> whatsappChats;
    std::vector<std::string> keepDomains;
    std::string gmailAccount;
    std::string selfName;
    std::string timeZone;
    std::string eventSuggestionNote;
    std::string deadlineNote;
    std::string medicalExpensesNote;
    std::string medicalExpensesSince;
    std::map<std::string, std::string> medsNotes;
    std::map<std::string, std::string> kidsDrive;
    std::string kidsSchoolFolder;
    std::vector<std::string> contactGroups;
    std::map<std::string, std::string> whoNames;
    std::vector<std::string> familyAliases;
    std::string familySurname;
};

inline std::filesystem::path personalLogConfigDir() {
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) {
        return std::filesystem::path(xdg) / "personal-log";
    }
    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::filesystem::path(home) / ".config/personal-log";
    }
    throw std::runtime_error("Set HOME or XDG_CONFIG_HOME for personal-log configuration");
}

inline std::filesystem::path mineConfigPath() {
    if (const char* path = std::getenv("PERSONAL_LOG_MINE_CONFIG"); path && *path) {
        return path;
    }
    return personalLogConfigDir() / "mine.json";
}

inline std::string validateCalendarId(std::string id) {
    const auto first = id.find_first_not_of(" \t\r\n");
    const auto last = id.find_last_not_of(" \t\r\n");
    id = first == std::string::npos ? "" : id.substr(first, last - first + 1);
    if (id.empty() || !std::all_of(id.begin(), id.end(), [](unsigned char ch) {
            return std::isalnum(ch) || ch == '.' || ch == '@' || ch == '-' || ch == '_';
        })) {
        throw std::runtime_error("Invalid mining suggestion calendar id");
    }
    return id;
}

inline std::string suggestCalendarId(const MineConfig& config) {
    const char* overrideId = std::getenv("PERSONAL_LOG_SUGGEST_CALENDAR_ID");
    auto id = validateCalendarId(overrideId && *overrideId ? overrideId : config.suggestCalendarId);
    if (id == "primary" || config.calendars.contains(id)) {
        throw std::runtime_error("Mining suggestion calendar must be separate from source calendars");
    }
    return id;
}

inline MineConfig loadMineConfig(const std::filesystem::path& path = mineConfigPath()) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Mining configuration is required: set PERSONAL_LOG_MINE_CONFIG or create personal-log/mine.json in your XDG config directory");
    }
    std::ostringstream content;
    content << file.rdbuf();
    MineConfig config;
    // Never include parser diagnostics: they can contain private configuration values.
    const auto error = glz::read<glz::opts{
        .null_terminated = false, .error_on_missing_keys = true}>(config, content.str());
    if (file.bad() || error) {
        throw std::runtime_error("Invalid mining configuration JSON: all documented fields are required");
    }
    auto requireText = [](const std::string& value) {
        if (value.empty() || std::all_of(value.begin(), value.end(), [](unsigned char ch) {
                return std::isspace(ch);
            }) || std::any_of(value.begin(), value.end(), [](unsigned char ch) {
                return ch < 32 || ch == 127;
            })) {
            throw std::runtime_error("Invalid mining configuration: empty or control-character field");
        }
    };
    for (const auto* text : {&config.deadlinesTasklist, &config.gmailAccount, &config.selfName,
                            &config.timeZone, &config.eventSuggestionNote, &config.deadlineNote,
                            &config.medicalExpensesNote, &config.medicalExpensesSince,
                            &config.kidsSchoolFolder, &config.familySurname}) {
        requireText(*text);
    }
    for (const auto* values : {&config.keepDomains, &config.contactGroups, &config.familyAliases}) {
        for (const auto& value : *values) requireText(value);
    }
    for (const auto* values : {&config.calendars, &config.medsNotes, &config.kidsDrive, &config.whoNames}) {
        for (const auto& [key, value] : *values) {
            requireText(key);
            requireText(value);
        }
    }
    for (const auto& chat : config.whatsappChats) {
        requireText(chat.name);
        requireText(chat.kind);
    }
    static const std::regex sincePattern(R"(^\d{4}\.\d{2}\.\d{2}$)");
    if (!std::regex_match(config.medicalExpensesSince, sincePattern)) {
        throw std::runtime_error("Invalid mining configuration: medicalExpensesSince must be YYYY.MM.DD");
    }
    for (const auto& [id, name] : config.calendars) {
        (void)name;
        if (validateCalendarId(id) != id) {
            throw std::runtime_error("Invalid mining configuration source calendar id");
        }
    }
    config.suggestCalendarId = validateCalendarId(config.suggestCalendarId);
    (void)suggestCalendarId(config);
    return config;
}

inline std::string suggestCalendarId() {
    return suggestCalendarId(loadMineConfig());
}

} // namespace personal_log
