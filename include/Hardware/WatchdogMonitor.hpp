#pragma once

#include "Core/GEMStateMachine.hpp"
#include "StageSimulator.hpp"

#include <chrono>
#include <cmath>
#include <stop_token>
#include <thread>

class WatchdogMonitor
{
    const StageSimulator& ss_;
    GEMStateMachine& gm_;
    std::jthread worker_;

public: 
    WatchdogMonitor(StageSimulator& ss, GEMStateMachine& gm)
    : ss_(ss),
      gm_(gm),
      worker_([this](std::stop_token st) { run(st); })
      {}

private:
    void run(std::stop_token st){
        using namespace std::chrono_literals;
        while(!st.stop_requested()){
            WaferPoint snapshot = ss_.getPosition();
            if(std::sqrt((snapshot.x * snapshot.x) + (snapshot.y * snapshot.y) )> 150.0){
                gm_.switchState(State::ALARM);
            }

            std::this_thread::sleep_for(50ms);
        }
    }
};


