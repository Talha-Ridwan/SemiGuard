#pragma once

#include <cstdint>
#include <array>
enum class SType : std::uint8_t
{
    Data = 0,
    SelectReq = 1,
    SelectRsp = 2,
    LinktestReq = 5,
    LinktestRsp = 6
};

struct HsmsHeader
{   /*
    session id 1 for data messages
    wBit 1 means reply is wanted
    function odd = request, even = reply
    ptype 0 meaning body is SECS-II
    Stype to 0 means Data
    sysbytes are just transaction id, reply must match request id so host can match them
    */
    std::uint16_t sessionId = 1; 
    std::uint8_t stream = 0;
    std::uint8_t function = 0;
    bool wBit = false;
    std::uint8_t pType = 0;
    SType stype = SType::Data;
    std::uint32_t sysBytes = 0;

    static constexpr std::size_t WireSize = 10;

    [[nodiscard]] std::array<std::uint8_t, WireSize> encode() const noexcept
    {
        std::array<std::uint8_t, WireSize> buf{};

        buf[0] = static_cast<std::uint8_t>(sessionId >> 8);
        buf[1] = static_cast<std::uint8_t>(sessionId & 0xFF);

        buf[2] = (wBit ? 0x80 : 0x00) | (stream & 0x7F);

        buf[3] = function;

        buf[4] = pType;

        buf[5] = static_cast<std::uint8_t>(stype);

        buf[6] = static_cast<std::uint8_t>(sysBytes >> 24);
        buf[7] = static_cast<std::uint8_t>((sysBytes >> 16) & 0xFF);
        buf[8] = static_cast<std::uint8_t>((sysBytes >> 8) & 0xFF);
        buf[9] = static_cast<std::uint8_t>(sysBytes & 0xFF);

        return buf;
    }

    [[nodiscard]] static HsmsHeader decode(const std::array<std::uint8_t, WireSize> &buf) noexcept
    {
        HsmsHeader hdr{};

        hdr.sessionId = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(buf[0]) << 8) | buf[1]);

        hdr.wBit = (buf[2] & 0x80) != 0;
        hdr.stream = buf[2] & 0x7F;

        hdr.function = buf[3];
        hdr.pType = buf[4];
        hdr.stype = static_cast<SType>(buf[5]);

        hdr.sysBytes = (static_cast<std::uint32_t>(buf[6]) << 24) |
                       (static_cast<std::uint32_t>(buf[7]) << 16) |
                       (static_cast<std::uint32_t>(buf[8]) << 8) |
                       static_cast<std::uint32_t>(buf[9]);

        return hdr;
    }
};