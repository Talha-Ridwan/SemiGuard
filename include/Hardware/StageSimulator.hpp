#pragma once

#include "Common/Types.hpp"
#include "Common/SafeQueue.hpp"
#include "stop_token"

#include <thread>
#include <chrono>

class StageSimulator{
    SafeQueue<WaferPoint>& targets_;
    SafeQueue<WaferPoint>& arrivals_;
    WaferPoint current_{0.0, 0.0};
    std::jthread worker_;

public:
    StageSimulator(SafeQueue<WaferPoint>& targets, SafeQueue<WaferPoint>& arrivals)
        : targets_(targets),
          arrivals_(arrivals),
          worker_([this](std::stop_token st){ run(st); })
          {}
        ~StageSimulator(){
        targets_.stop();
        }
private:
    static constexpr int kSteps = 20;
    void run(std::stop_token st){
        using namespace std::chrono_literals;

        WaferPoint target;

        while(targets_.pop(target)){
            WaferPoint start = current_;
            for(int i = 1; i <=kSteps; ++i){
                if(st.stop_requested()) return;
                double t = static_cast<double>(i) / kSteps;
                current_.x = start.x + (target.x - start.x) * t;
                current_.y = start.y + (target.y - start.y) * t;
                std::this_thread::sleep_for(5ms);
            }

            arrivals_.push(current_);
        }
    }
};