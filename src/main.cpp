#include "Core/GEMStateMachine.hpp"
#include "Hardware/SensorSimulator.hpp"
#include "Hardware/StageSimulator.hpp"
#include "Hardware/WatchdogMonitor.hpp"
#include "Protocol/TcpServer.hpp"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <string_view>
#include <thread>
#include <vector>

namespace {

std::string_view toString(State s){
    switch(s){
        case State::OFF_LINE:  return "OFF_LINE";
        case State::IDLE:      return "IDLE";
        case State::SETUP:     return "SETUP";
        case State::EXECUTING: return "EXECUTING";
        case State::PAUSED:    return "PAUSED";
        case State::ALARM:     return "ALARM";
    }
    return "UNKNOWN";
}

} // namespace

int main(){
    using namespace std::chrono_literals;

    SafeQueue<WaferPoint>  targets;
    SafeQueue<WaferPoint>  arrivals;
    SafeQueue<Measurement> telemetry;

    GEMStateMachine gm;

    StageSimulator  stage{targets, arrivals};
    SensorSimulator sensor{arrivals, telemetry};
    WatchdogMonitor watchdog{stage, gm};

    gm.switchState(State::IDLE);
    gm.switchState(State::SETUP);
    gm.switchState(State::EXECUTING);

    // Last point sits off the 300mm wafer (150mm radius) to trip the watchdog.
    const std::vector<WaferPoint> points{ {0,0}, {10,0}, {20,0}, {20,10}, {200,0} };

    for(const auto& p : points){
        targets.push(p);
    }

    std::cout << std::fixed << std::setprecision(2);

    for(std::size_t i = 0; i < points.size(); ++i){
        Measurement m;
        if(!telemetry.pop(m)) break;
        std::cout << "x=" << m.x << "  y=" << m.y
                  << "  thickness=" << m.thickness_nm << " nm\n";
    }

    // The watchdog polls every 50ms; give it a beat to observe the final position.
    std::this_thread::sleep_for(100ms);
    std::cout << "state: " << toString(gm.getState()) << "\n";

    TcpServer server{5000};
    if(!server.start()){
        std::cerr << "failed to listen on port 5000\n";
        return 1;
    }
    std::cout << "listening on port 5000, waiting for host...\n";

    if(!server.acceptClient()){
        std::cerr << "accept failed\n";
        return 1;
    }
    std::cout << "host connected\n";

    server.serve();

    return 0;
}
