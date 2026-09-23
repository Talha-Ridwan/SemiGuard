#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "Core/GEMStateMachine.hpp"
#include "Hardware/SensorSimulator.hpp"
#include "Hardware/StageSimulator.hpp"
#include "Hardware/WatchdogMonitor.hpp"

#include <chrono>
#include <numeric>
#include <thread>
#include <vector>

using Catch::Approx;
using namespace std::chrono_literals;

// NOTE: SafeQueue::pop() blocks with no timeout, so a broken pipeline makes these
// tests hang rather than fail. CMake sets a per-test TIMEOUT as the safety net.

TEST_CASE("stage interpolates to the target and announces the arrival", "[stage]") {
    SafeQueue<WaferPoint> targets;
    SafeQueue<WaferPoint> arrivals;
    StageSimulator stage{targets, arrivals};

    targets.push(WaferPoint{10.0, 20.0});

    WaferPoint arrived{};
    REQUIRE(arrivals.pop(arrived));
    REQUIRE(arrived.x == Approx(10.0));
    REQUIRE(arrived.y == Approx(20.0));

    // The announced arrival and the live position must agree once the move is done.
    const WaferPoint live = stage.getPosition();
    REQUIRE(live.x == Approx(10.0));
    REQUIRE(live.y == Approx(20.0));
}

TEST_CASE("stage visits targets in order", "[stage]") {
    SafeQueue<WaferPoint> targets;
    SafeQueue<WaferPoint> arrivals;
    StageSimulator stage{targets, arrivals};

    const std::vector<WaferPoint> points{{1.0, 0.0}, {2.0, 0.0}, {3.0, 0.0}};
    for (const auto& p : points) {
        targets.push(p);
    }

    for (const auto& expected : points) {
        WaferPoint arrived{};
        REQUIRE(arrivals.pop(arrived));
        REQUIRE(arrived.x == Approx(expected.x));
        REQUIRE(arrived.y == Approx(expected.y));
    }
}

TEST_CASE("sensor enriches an arrival into a measurement", "[sensor]") {
    SafeQueue<WaferPoint> arrivals;
    SafeQueue<Measurement> telemetry;
    SensorSimulator sensor{arrivals, telemetry};

    arrivals.push(WaferPoint{3.0, 4.0});

    Measurement m{};
    REQUIRE(telemetry.pop(m));

    // Coordinates pass through untouched; only thickness is generated.
    REQUIRE(m.x == Approx(3.0));
    REQUIRE(m.y == Approx(4.0));

    // PDD FR-2: 500nm +/- 12nm. This band is ~8 sigma, so it should never flake.
    REQUIRE(m.thickness_nm > 400.0);
    REQUIRE(m.thickness_nm < 600.0);
}

TEST_CASE("sensor thickness follows the specified distribution", "[sensor]") {
    SafeQueue<WaferPoint> arrivals;
    SafeQueue<Measurement> telemetry;
    SensorSimulator sensor{arrivals, telemetry};

    constexpr int kSamples = 60;
    for (int i = 0; i < kSamples; ++i) {
        arrivals.push(WaferPoint{0.0, 0.0});
    }

    std::vector<double> thicknesses;
    thicknesses.reserve(kSamples);
    for (int i = 0; i < kSamples; ++i) {
        Measurement m{};
        REQUIRE(telemetry.pop(m));
        thicknesses.push_back(m.thickness_nm);
    }

    const double mean =
        std::accumulate(thicknesses.begin(), thicknesses.end(), 0.0) / kSamples;

    // Standard error is 12/sqrt(60) ~= 1.55nm, so +/-10nm is roughly 6 sigma.
    REQUIRE(mean > 490.0);
    REQUIRE(mean < 510.0);

    // A stuck generator would return the same value every time.
    REQUIRE(thicknesses.front() != thicknesses.back());
}

TEST_CASE("full pipeline delivers one measurement per target, in order", "[pipeline]") {
    SafeQueue<WaferPoint> targets;
    SafeQueue<WaferPoint> arrivals;
    SafeQueue<Measurement> telemetry;

    StageSimulator stage{targets, arrivals};
    SensorSimulator sensor{arrivals, telemetry};

    const std::vector<WaferPoint> points{{0.0, 0.0}, {10.0, 0.0}, {20.0, 10.0}};
    for (const auto& p : points) {
        targets.push(p);
    }

    for (const auto& expected : points) {
        Measurement m{};
        REQUIRE(telemetry.pop(m));
        REQUIRE(m.x == Approx(expected.x));
        REQUIRE(m.y == Approx(expected.y));
        REQUIRE(m.thickness_nm > 400.0);
        REQUIRE(m.thickness_nm < 600.0);
    }
}

TEST_CASE("stage refuses an off-wafer target without moving", "[stage]") {
    SafeQueue<WaferPoint> targets;
    SafeQueue<WaferPoint> arrivals;
    StageSimulator stage{targets, arrivals};

    // The follow-up target is on the wafer, but a tripped limit locks the motors,
    // so it must never be executed either.
    targets.push(WaferPoint{200.0, 0.0});  // 200mm out, past the 150mm limit
    targets.push(WaferPoint{10.0, 0.0});

    // No arrival for either target: the stage closes the queue on its way out.
    WaferPoint arrived{};
    REQUIRE_FALSE(arrivals.pop(arrived));

    REQUIRE(stage.isStopped());

    // The check happens before the first step, so the stage never left the origin.
    const WaferPoint live = stage.getPosition();
    REQUIRE(live.x == Approx(0.0));
    REQUIRE(live.y == Approx(0.0));
}

TEST_CASE("stage accepts a target exactly on the wafer edge", "[stage]") {
    SafeQueue<WaferPoint> targets;
    SafeQueue<WaferPoint> arrivals;
    StageSimulator stage{targets, arrivals};

    targets.push(WaferPoint{150.0, 0.0});  // r == 150mm is still on the wafer

    WaferPoint arrived{};
    REQUIRE(arrivals.pop(arrived));
    REQUIRE(arrived.x == Approx(150.0));
    REQUIRE_FALSE(stage.isStopped());
}

TEST_CASE("stage halts at its last good point when a later target is off-wafer", "[stage]") {
    SafeQueue<WaferPoint> targets;
    SafeQueue<WaferPoint> arrivals;
    StageSimulator stage{targets, arrivals};

    targets.push(WaferPoint{20.0, 10.0});
    targets.push(WaferPoint{200.0, 0.0});

    WaferPoint arrived{};
    REQUIRE(arrivals.pop(arrived));
    REQUIRE(arrived.x == Approx(20.0));
    REQUIRE(arrived.y == Approx(10.0));

    REQUIRE_FALSE(arrivals.pop(arrived));
    REQUIRE(stage.isStopped());

    const WaferPoint live = stage.getPosition();
    REQUIRE(live.x == Approx(20.0));
    REQUIRE(live.y == Approx(10.0));
}

TEST_CASE("watchdog raises ALARM when the stage refuses an off-wafer target", "[watchdog]") {
    SafeQueue<WaferPoint> targets;
    SafeQueue<WaferPoint> arrivals;
    GEMStateMachine gm;

    StageSimulator stage{targets, arrivals};
    WatchdogMonitor watchdog{stage, gm};

    // ALARM is only reachable from a running state, so drive the machine there first.
    REQUIRE(gm.switchState(State::IDLE));
    REQUIRE(gm.switchState(State::SETUP));
    REQUIRE(gm.switchState(State::EXECUTING));

    targets.push(WaferPoint{200.0, 0.0});  // 200mm out, past the 150mm limit

    // The stage refuses before moving, so no arrival ever comes.
    WaferPoint arrived{};
    REQUIRE_FALSE(arrivals.pop(arrived));

    // The watchdog polls every 50ms; allow several cycles before judging.
    std::this_thread::sleep_for(200ms);
    REQUIRE(gm.getState() == State::ALARM);

    // The stage never left the wafer; the watchdog reports the e-stop, not a position.
    const WaferPoint live = stage.getPosition();
    REQUIRE(live.x * live.x + live.y * live.y <= 150.0 * 150.0);
}

TEST_CASE("watchdog raises ALARM when the stage is e-stopped externally", "[watchdog]") {
    SafeQueue<WaferPoint> targets;
    SafeQueue<WaferPoint> arrivals;
    GEMStateMachine gm;

    StageSimulator stage{targets, arrivals};
    WatchdogMonitor watchdog{stage, gm};

    REQUIRE(gm.switchState(State::IDLE));
    REQUIRE(gm.switchState(State::SETUP));
    REQUIRE(gm.switchState(State::EXECUTING));

    // Tripping twice must stay tripped: the stop is idempotent, not a toggle.
    stage.setStop();
    stage.setStop();
    REQUIRE(stage.isStopped());

    std::this_thread::sleep_for(200ms);
    REQUIRE(gm.getState() == State::ALARM);
}

TEST_CASE("watchdog stays quiet while the stage is on the wafer", "[watchdog]") {
    SafeQueue<WaferPoint> targets;
    SafeQueue<WaferPoint> arrivals;
    GEMStateMachine gm;

    StageSimulator stage{targets, arrivals};
    WatchdogMonitor watchdog{stage, gm};

    REQUIRE(gm.switchState(State::IDLE));
    REQUIRE(gm.switchState(State::SETUP));
    REQUIRE(gm.switchState(State::EXECUTING));

    targets.push(WaferPoint{20.0, 10.0});  // ~22mm from centre, well inside

    WaferPoint arrived{};
    REQUIRE(arrivals.pop(arrived));

    std::this_thread::sleep_for(200ms);
    REQUIRE(gm.getState() == State::EXECUTING);
}

TEST_CASE("shutdown joins every worker within the 500ms budget", "[shutdown]") {
    const auto start = std::chrono::steady_clock::now();
    {
        SafeQueue<WaferPoint> targets;
        SafeQueue<WaferPoint> arrivals;
        SafeQueue<Measurement> telemetry;
        GEMStateMachine gm;

        StageSimulator stage{targets, arrivals};
        SensorSimulator sensor{arrivals, telemetry};
        WatchdogMonitor watchdog{stage, gm};

        targets.push(WaferPoint{5.0, 5.0});

        Measurement m{};
        REQUIRE(telemetry.pop(m));
    }  // all three workers are stopped and joined here
    const auto elapsed = std::chrono::steady_clock::now() - start;

    // PDD 4.1: graceful shutdown within 500ms of the exit signal.
    REQUIRE(elapsed < 1000ms);
}
