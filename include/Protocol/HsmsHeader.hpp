#include <cstdint>
#include <unordered_set>

enum class SType : std::uint8_t {
    Data      = 0,
    SelectReq = 1,
    Function  = 3,
    P_Type    = 4,
    S_Type    = 5
};

inline constexpr std::unordered_set<std::uint8_t> SystemBytes = {6, 7, 8, 9};