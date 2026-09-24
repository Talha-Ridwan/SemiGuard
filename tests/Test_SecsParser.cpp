#include <catch2/catch_test_macros.hpp>
#include "Protocol/HsmsHeader.hpp"
#include "Protocol/SecsMessage.hpp"

#include <array>
#include <cstdint>
#include <vector>

using Bytes = std::array<std::uint8_t, HsmsHeader::WireSize>;
using Wire = std::vector<std::uint8_t>;

TEST_CASE("encode S1F13W matches the PDD byte layout", "[hsms]") {
    HsmsHeader h;
    h.stream = 1;
    h.function = 13;
    h.wBit = true;
    h.sysBytes = 7;

    Bytes expected{0x00, 0x01, 0x81, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07};

    REQUIRE(h.encode() == expected);
}

TEST_CASE("multi-byte fields are encoded big-endian", "[hsms]") {
    HsmsHeader h;
    h.sessionId = 0x1234;
    h.sysBytes = 0xAABBCCDD;

    auto buf = h.encode();

    REQUIRE(buf[0] == 0x12);
    REQUIRE(buf[1] == 0x34);
    REQUIRE(buf[6] == 0xAA);
    REQUIRE(buf[7] == 0xBB);
    REQUIRE(buf[8] == 0xCC);
    REQUIRE(buf[9] == 0xDD);
}

TEST_CASE("W-bit shares byte 2 with stream without clobbering it", "[hsms]") {
    HsmsHeader h;
    h.stream = 0x7F;

    h.wBit = false;
    REQUIRE(h.encode()[2] == 0x7F);

    h.wBit = true;
    REQUIRE(h.encode()[2] == 0xFF);
}

TEST_CASE("stream above 7 bits cannot set the W-bit", "[hsms]") {
    HsmsHeader h;
    h.stream = 0x81;
    h.wBit = false;

    REQUIRE(h.encode()[2] == 0x01);
}

TEST_CASE("control message encodes its SType in byte 5", "[hsms]") {
    HsmsHeader h;
    h.sessionId = 0xFFFF;
    h.stype = SType::LinktestReq;

    auto buf = h.encode();

    REQUIRE(buf[5] == 5);
    REQUIRE(buf[2] == 0x00);
    REQUIRE(buf[3] == 0x00);
}

TEST_CASE("decode of a known frame fills every field", "[hsms]") {
    Bytes wire{0x00, 0x01, 0x81, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07};

    HsmsHeader h = HsmsHeader::decode(wire);

    REQUIRE(h.sessionId == 1);
    REQUIRE(h.stream == 1);
    REQUIRE(h.function == 13);
    REQUIRE(h.wBit == true);
    REQUIRE(h.pType == 0);
    REQUIRE(h.stype == SType::Data);
    REQUIRE(h.sysBytes == 7);
}

TEST_CASE("encode then decode round-trips every field", "[hsms]") {
    HsmsHeader original;
    original.sessionId = 0xBEEF;
    original.stream = 127;
    original.function = 255;
    original.wBit = true;
    original.pType = 0;
    original.stype = SType::SelectRsp;
    original.sysBytes = 0xDEADBEEF;

    HsmsHeader copy = HsmsHeader::decode(original.encode());

    REQUIRE(copy.sessionId == original.sessionId);
    REQUIRE(copy.stream == original.stream);
    REQUIRE(copy.function == original.function);
    REQUIRE(copy.wBit == original.wBit);
    REQUIRE(copy.pType == original.pType);
    REQUIRE(copy.stype == original.stype);
    REQUIRE(copy.sysBytes == original.sysBytes);
}

TEST_CASE("ascii item encodes format, length, then characters", "[secs]") {
    Wire expected{0x41, 0x05, 0x53, 0x54, 0x41, 0x52, 0x54};

    REQUIRE(SecsItem::ascii("START").encode() == expected);
}

TEST_CASE("binary item encodes as a single payload byte", "[secs]") {
    Wire expected{0x21, 0x01, 0x00};

    REQUIRE(SecsItem::binary(0).encode() == expected);
}

TEST_CASE("u4 item encodes its value big-endian", "[secs]") {
    Wire expected{0xB1, 0x04, 0x12, 0x34, 0x56, 0x78};

    REQUIRE(SecsItem::u4(0x12345678).encode() == expected);
}

TEST_CASE("empty list encodes a zero child count", "[secs]") {
    Wire expected{0x01, 0x00};

    REQUIRE(SecsItem::list({}).encode() == expected);
}

TEST_CASE("nested list counts children and encodes them in order", "[secs]") {
    Wire expected{0x01, 0x02, 0x21, 0x01, 0x00, 0x01, 0x00};

    REQUIRE(SecsItem::list({SecsItem::binary(0), SecsItem::list({})}).encode() == expected);
}

TEST_CASE("decode reads the START command out of an S2F41 body", "[secs]") {
    Wire body{0x01, 0x02, 0x41, 0x05, 0x53, 0x54, 0x41, 0x52, 0x54, 0x01, 0x00};

    auto root = SecsItem::decode(body);

    REQUIRE(root.has_value());
    REQUIRE(root->format == SecsFormat::L);
    REQUIRE(root->items.size() == 2);
    REQUIRE(root->items[0].format == SecsFormat::A);
    REQUIRE(root->items[0].data == Wire{'S', 'T', 'A', 'R', 'T'});
    REQUIRE(root->items[1].format == SecsFormat::L);
    REQUIRE(root->items[1].items.empty());
}

TEST_CASE("decode then encode round-trips a nested message", "[secs]") {
    auto original = SecsItem::list({SecsItem::u4(0xDEADBEEF),
                                    SecsItem::list({SecsItem::binary(7), SecsItem::ascii("OK")})});
    Wire wire = original.encode();

    auto decoded = SecsItem::decode(wire);

    REQUIRE(decoded.has_value());
    REQUIRE(decoded->encode() == wire);
}

TEST_CASE("decode accepts an item that uses two length bytes", "[secs]") {
    Wire wire{0x42, 0x00, 0x02, 0x4F, 0x4B};

    auto item = SecsItem::decode(wire);

    REQUIRE(item.has_value());
    REQUIRE(item->format == SecsFormat::A);
    REQUIRE(item->data == Wire{'O', 'K'});
}

TEST_CASE("decode rejects malformed bodies", "[secs]") {
    REQUIRE_FALSE(SecsItem::decode(Wire{}).has_value());
    REQUIRE_FALSE(SecsItem::decode(Wire{0x41, 0x05, 0x53}).has_value());
    REQUIRE_FALSE(SecsItem::decode(Wire{0x01, 0x02, 0x21, 0x01, 0x00}).has_value());
    REQUIRE_FALSE(SecsItem::decode(Wire{0x40}).has_value());
    REQUIRE_FALSE(SecsItem::decode(Wire{0x21, 0x01, 0x00, 0xFF}).has_value());
}
