#pragma once

#include "Common/Types.hpp"
#include "Common/SafeQueue.hpp"

#include <random>
#include <thread>
#include <stop_token>
#include <chrono>

class SensorSimulator{
    SafeQueue<WaferPoint>&  arrivals_;    
    SafeQueue<Measurement>& telemetry_;   

    std::mt19937 gen_{std::random_device{}()};
    std::normal_distribution<double> dist_{500.0, 12.0};

    std::jthread worker_;                 

public:
    SensorSimulator(SafeQueue<WaferPoint>& arrivals, SafeQueue<Measurement>& telemetry)
        : arrivals_(arrivals),
          telemetry_(telemetry),
          worker_([this](std::stop_token st){ run(st); })
    {}

    ~SensorSimulator(){
        arrivals_.stop();
    }

private:
    void run(std::stop_token st){
        using namespace std::chrono_literals;

        WaferPoint p;
        while(arrivals_.pop(p)){
            if(st.stop_requested()) break;
            std::this_thread::sleep_for(2ms);          
            telemetry_.push(Measurement{p.x, p.y, dist_(gen_)});
        }
        telemetry_.stop();
    }
};
