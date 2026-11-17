#pragma once

#include "db.hpp"
#include "mine.hpp"
#include "mine_config.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <ctime>
#include <exception>
#include <filesystem>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <utility>

namespace personal_log {

struct SchedulerClock {
    using Clock = std::chrono::system_clock;

    std::function<Clock::time_point()> now = [] { return Clock::now(); };
    std::function<std::tm(std::time_t)> toLocal = [](std::time_t t) {
        std::tm tm{};
        localtime_r(&t, &tm);
        return tm;
    };
    std::function<Clock::time_point(std::tm)> fromLocal = [](std::tm tm) {
        tm.tm_isdst = -1;
        return Clock::from_time_t(std::mktime(&tm));
    };
    std::function<bool(std::stop_token, Clock::time_point)> waitUntil =
        [](std::stop_token st, Clock::time_point target) {
            std::mutex mu;
            std::condition_variable_any cv;
            std::unique_lock<std::mutex> lock(mu);
            cv.wait_until(lock, st, target, [] { return false; });
            return !st.stop_requested();
        };
};

namespace scheduler_detail {

inline std::string formatDate(const std::tm &tm) {
    char buf[16]{};
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    return std::string(buf);
}

inline std::string normalizeDashedDate(std::string date) {
    for (char &ch : date) {
        if (ch == '.')
            ch = '-';
    }
    return date;
}

inline SchedulerClock::Clock::time_point computeNextSlot(
    SchedulerClock::Clock::time_point from, int hour, int minute, const SchedulerClock &clock) {
    std::tm tm = clock.toLocal(SchedulerClock::Clock::to_time_t(from));
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = 0;
    auto candidate = clock.fromLocal(tm);
    if (candidate <= from) {
        tm.tm_mday += 1;
        candidate = clock.fromLocal(tm);
    }
    return candidate;
}

inline bool needsStartupCatchUp(
    const std::optional<std::string> &lastSuccessfulUntil,
    SchedulerClock::Clock::time_point now,
    int hour,
    int minute,
    const SchedulerClock &clock) {
    std::tm localNow = clock.toLocal(SchedulerClock::Clock::to_time_t(now));
    std::string today = formatDate(localNow);
    std::tm todaySlotTm = localNow;
    todaySlotTm.tm_hour = hour;
    todaySlotTm.tm_min = minute;
    todaySlotTm.tm_sec = 0;
    auto todaySlot = clock.fromLocal(todaySlotTm);

    std::string latestPassedDate = (now >= todaySlot) ? today : addDays(today, -1);
    if (!lastSuccessfulUntil.has_value() || lastSuccessfulUntil->empty()) {
        return true;
    }
    return normalizeDashedDate(*lastSuccessfulUntil) < latestPassedDate;
}

} // namespace scheduler_detail

using ScheduledRunFn = std::function<void(AppDb &, const MineConfig &)>;

inline void defaultScheduledRun(AppDb &db, const MineConfig &config) {
    (void)runMine(db, MineOptions{}, config);
    if (config.schedule.has_value() && config.schedule->backup) {
        (void)backupLog(db);
    }
}

inline void runMiningSchedulerLoop(
    std::stop_token st,
    AppDb &db,
    const MineConfig &config,
    const SchedulerClock &clock = {},
    const ScheduledRunFn &runOnce = defaultScheduledRun) {
    if (!config.schedule.has_value() || !config.schedule->enabled) {
        return;
    }
    const auto [hour, minute] = parseScheduleTime(config.schedule->time);

    auto invokeSafely = [&]() {
        try {
            runOnce(db, config);
        } catch (const std::exception &e) {
            std::cerr << "personal-log-mcp scheduler: " << warningCategory(e.what()) << "\n";
        } catch (...) {
            std::cerr << "personal-log-mcp scheduler: unknown error\n";
        }
    };

    if (!st.stop_requested() &&
        scheduler_detail::needsStartupCatchUp(db.lastSuccessfulUntil(), clock.now(), hour, minute, clock)) {
        invokeSafely();
    }

    auto nextWake = scheduler_detail::computeNextSlot(clock.now(), hour, minute, clock);
    while (!st.stop_requested()) {
        if (!clock.waitUntil(st, nextWake) || st.stop_requested()) {
            break;
        }
        invokeSafely();
        nextWake = scheduler_detail::computeNextSlot(clock.now(), hour, minute, clock);
    }
}

inline void runMiningScheduler(
    std::stop_token st,
    const std::function<AppDb &()> &getDb,
    const SchedulerClock &clock = {},
    const ScheduledRunFn &runOnce = defaultScheduledRun) {
    std::error_code ec;
    if (!std::filesystem::exists(mineConfigPath(), ec)) {
        return;
    }
    std::optional<MineConfig> config;
    try {
        config = loadMineConfig();
    } catch (const std::exception &e) {
        std::cerr << "personal-log-mcp scheduler: " << warningCategory(e.what()) << "\n";
        return;
    }
    if (!config->schedule.has_value() || !config->schedule->enabled) {
        return;
    }
    runMiningSchedulerLoop(std::move(st), getDb(), *config, clock, runOnce);
}

} // namespace personal_log
