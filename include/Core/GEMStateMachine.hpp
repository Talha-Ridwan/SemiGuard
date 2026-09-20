#pragma once

#include <mutex>
#include <unordered_map>
#include <set>

enum struct State{
    OFF_LINE,
    IDLE,
    SETUP,
    EXECUTING,
    PAUSED,
    ALARM
};

struct GEMStateMachine{
private:
    State state_;
    mutable std::mutex mtx_;
    const std::unordered_map<State, std::set<State>> possibleSwitchPair = {
        {State::OFF_LINE, {State::IDLE}},
        {State::IDLE, {State::OFF_LINE, State::SETUP, State::ALARM}},
        {State::SETUP, {State::IDLE, State::EXECUTING, State::ALARM}},
        {State::EXECUTING, {State::PAUSED, State::IDLE, State::ALARM}},
        {State::PAUSED, {State::EXECUTING, State::IDLE, State::ALARM}},
        {State::ALARM, {State::IDLE}}
    };
public:
    GEMStateMachine(State CurrentState = State::OFF_LINE) :
        state_(CurrentState){}

    bool switchState(State to){
        std::lock_guard<std::mutex> lock(mtx_);
        const std::set<State>& allowed = possibleSwitchPair.at(state_);
        if(allowed.find(to) != allowed.end()){
            state_ = to;
            return true;
        }
        return false;
    }

    State getState() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return state_;
    }
};
