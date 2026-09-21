#include <catch2/catch_test_macros.hpp>
#include "Core/GEMStateMachine.hpp"

TEST_CASE("valid state transition succeeds", "[statemachine]"){
    GEMStateMachine sm;
    bool result = sm.switchState(State::IDLE);
    REQUIRE(result == true);
    REQUIRE(sm.getState() == State::IDLE);
}

TEST_CASE("invalid state transition rejected", "[statemachine]") {
    GEMStateMachine sm;
    bool result = sm.switchState(State::EXECUTING);  
    REQUIRE(result == false);
    REQUIRE(sm.getState() == State::OFF_LINE);   
}