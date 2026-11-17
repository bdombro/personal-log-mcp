#pragma once

#include "app.hpp"
#include "mine_config.hpp"
#include "scheduler.hpp"
#include "test.hpp"
#include "test_db.hpp"
#include "test_mine.hpp"
#include "test_mine_config.hpp"

#include <atomic>
#include <chrono>
#include <ctime>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace personal_log::testing {

inline SchedulerClock::Clock::time_point utcTime(int year, int month, int day, int hour, int minute, int second = 0) {
    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = second;
    return SchedulerClock::Clock::from_time_t(timegm(&tm));
}

inline SchedulerClock makeUtcClock(
    std::shared_ptr<SchedulerClock::Clock::time_point> current,
    std::vector<SchedulerClock::Clock::time_point> *waits = nullptr,
    int maxWaits = 0,
    std::stop_source *stopSource = nullptr) {
    SchedulerClock clock;
    clock.now = [current]() { return *current; };
    clock.toLocal = [](std::time_t t) {
        std::tm tm{};
        gmtime_r(&t, &tm);
        return tm;
    };
    clock.fromLocal = [](std::tm tm) {
        return SchedulerClock::Clock::from_time_t(timegm(&tm));
    };
    clock.waitUntil = [current, waits, maxWaits, stopSource](
                          std::stop_token st, SchedulerClock::Clock::time_point target) mutable {
        if (st.stop_requested()) {
            return false;
        }
        if (waits) {
            waits->push_back(target);
        }
        if (waits && static_cast<int>(waits->size()) > maxWaits) {
            if (stopSource) {
                stopSource->request_stop();
            }
            return false;
        }
        *current = target;
        return true;
    };
    return clock;
}

TEST_SUITE("mining scheduler") {
    TEST_CASE("catches up at startup when lastSuccessfulUntil is missing or stale, then waits for next slot") {
        AppDb db(":memory:");
        MineConfig config = loadMineConfig();
        config.schedule = MineScheduleConfig{.enabled = true, .time = "06:00", .backup = true};

        // 09:30 UTC on 2026-11-17 (past today's 06:00 slot), no prior runs -> catch-up + 1 scheduled wait
        auto current = std::make_shared<SchedulerClock::Clock::time_point>(utcTime(2026, 11, 17, 9, 30));
        std::vector<SchedulerClock::Clock::time_point> waits;
        std::stop_source stopSource;
        auto clock = makeUtcClock(current, &waits, 1, &stopSource);

        int runs = 0;
        runMiningSchedulerLoop(
            stopSource.get_token(),
            db,
            config,
            clock,
            [&](AppDb &d, const MineConfig &) {
                ++runs;
                auto tm = clock.toLocal(SchedulerClock::Clock::to_time_t(*current));
                std::string day = scheduler_detail::formatDate(tm);
                auto id = d.runStart(day, day);
                d.runFinish(id, "ok", slimr::Json::object_t{});
            });

        CHECK(runs == 2); // 1 catch-up at 09:30 + 1 scheduled run at 2026-11-18 06:00
        REQUIRE(waits.size() == 2);
        CHECK(waits[0] == utcTime(2026, 11, 18, 6, 0));
        CHECK(waits[1] == utcTime(2026, 11, 19, 6, 0));
    }

    TEST_CASE("skips startup catch-up when today's slot is already covered, and catches up yesterday if missed") {
        AppDb db(":memory:");
        MineConfig config = loadMineConfig();
        config.schedule = MineScheduleConfig{.enabled = true, .time = "06:00", .backup = false};

        // Record a successful run for 2026-11-17
        auto id = db.runStart("2026.11.16", "2026.11.17");
        db.runFinish(id, "ok", slimr::Json::object_t{});

        // Start at 2026-11-17 09:30 -> already covered, no catch-up
        {
            auto current = std::make_shared<SchedulerClock::Clock::time_point>(utcTime(2026, 11, 17, 9, 30));
            std::vector<SchedulerClock::Clock::time_point> waits;
            std::stop_source stopSource;
            auto clock = makeUtcClock(current, &waits, 0, &stopSource);

            int runs = 0;
            runMiningSchedulerLoop(stopSource.get_token(), db, config, clock, [&](AppDb &, const MineConfig &) {
                ++runs;
            });
            CHECK(runs == 0);
            REQUIRE(waits.size() == 1);
            CHECK(waits[0] == utcTime(2026, 11, 18, 6, 0));
        }

        // Start at 2026-11-19 04:00 (before today's 06:00, yesterday 2026-11-18 was missed) -> catch-up runs
        {
            auto current = std::make_shared<SchedulerClock::Clock::time_point>(utcTime(2026, 11, 19, 4, 0));
            std::vector<SchedulerClock::Clock::time_point> waits;
            std::stop_source stopSource;
            auto clock = makeUtcClock(current, &waits, 0, &stopSource);

            int runs = 0;
            runMiningSchedulerLoop(stopSource.get_token(), db, config, clock, [&](AppDb &, const MineConfig &) {
                ++runs;
            });
            CHECK(runs == 1);
            REQUIRE(waits.size() == 1);
            CHECK(waits[0] == utcTime(2026, 11, 19, 6, 0));
        }
    }

    TEST_CASE("defaultScheduledRun executes mine and backup unless backup is disabled, and errors do not kill loop") {
        slimr::testing::TempDirGuard temp("scheduler_run_");
        ScopedMineEnv prefix("HOMEBREW_PREFIX", temp.path.string());

        AppDb db(":memory:");
        MineConfig config = loadMineConfig();
        config.schedule = MineScheduleConfig{.enabled = true, .time = "06:00", .backup = true};

        std::vector<slimr::Json> calls;
        auto runner = std::make_shared<FakeToolsRunner>(calls, slimr::Json::object_t{});
        struct ToolsGuard {
            ~ToolsGuard() { setTools(nullptr); }
        } toolsGuard;
        setTools(runner);

        defaultScheduledRun(db, config);
        CHECK(db.runs(5).size() == 1);
        auto sawDriveList = [&]() {
            for (const auto &call : calls) {
                if (call.contains("service") && call.at("service").is_string() &&
                    call.at("service").get<std::string>() == "drive") {
                    return true;
                }
            }
            return false;
        };
        CHECK(sawDriveList());

        // Now disable backup and verify errors in runOnce are caught without aborting the loop
        calls.clear();
        config.schedule->backup = false;
        defaultScheduledRun(db, config);
        CHECK(!sawDriveList());

        AppDb errDb(":memory:");
        auto current = std::make_shared<SchedulerClock::Clock::time_point>(utcTime(2026, 11, 17, 9, 30));
        std::vector<SchedulerClock::Clock::time_point> waits;
        std::stop_source stopSource;
        auto clock = makeUtcClock(current, &waits, 1, &stopSource);
        int attempts = 0;
        runMiningSchedulerLoop(stopSource.get_token(), errDb, config, clock, [&](AppDb &, const MineConfig &) {
            ++attempts;
            throw std::runtime_error("Transient failure: user@example.com");
        });
        CHECK(attempts == 2);
    }

    TEST_CASE("stop_token wakes condition_variable_any wait immediately without opening missing config DB") {
        slimr::testing::TempDirGuard temp("scheduler_missing_");
        {
            ScopedMineEnv cfg("PERSONAL_LOG_MINE_CONFIG", (temp.path / "missing.json").string());
            ScopedMineEnv dbEnv("PERSONAL_LOG_DB", (temp.path / "data/log.db").string());

            bool dbOpened = false;
            std::stop_source stopSource;
            runMiningScheduler(stopSource.get_token(), [&]() -> AppDb & {
                dbOpened = true;
                static AppDb dummy(":memory:");
                return dummy;
            });
            CHECK(!dbOpened);
            CHECK(!std::filesystem::exists(temp.path / "data/log.db"));
        }

        // Verify real condition_variable_any wait wakes immediately on stop_token
        AppDb db(":memory:");
        MineConfig config = loadMineConfig();
        config.schedule = MineScheduleConfig{.enabled = true, .time = "23:59", .backup = false};
        auto id = db.runStart("2026.11.17", "2099.12.31");
        db.runFinish(id, "ok", slimr::Json::object_t{});

        std::stop_source liveStop;
        std::jthread t([&](std::stop_token st) {
            runMiningSchedulerLoop(std::move(st), db, config, SchedulerClock{}, [&](AppDb &, const MineConfig &) {});
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        t.request_stop();
        t.join();
        CHECK(true);
    }
}

} // namespace personal_log::testing
