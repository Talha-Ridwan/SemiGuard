#pragma once
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>
#include <string_view>
#include <bit>
enum class SecsFormat : std::uint8_t{
    L = 0x00, //List
    B = 0x08, //Binary
    A = 0x10, //ASCII
    U1 = 0x29,
    U4 = 0x2C,
    F4 = 0x24
};

struct SecsItem {
    SecsFormat format = SecsFormat::L;
    std::vector<SecsItem> items;
    std::vector<std::uint8_t> data;

    static SecsItem list(std::vector<SecsItem> children){
        SecsItem item;
        item.format = SecsFormat::L;
        item.items = std::move(children);
        return item;
    }
    static SecsItem ascii(std::string_view text){
        SecsItem item;
        item.format = SecsFormat::A;
        item.data.assign(text.begin(), text.end());
        return item;
    };
    static SecsItem binary(std::uint8_t value){
        SecsItem item;
        item.format = SecsFormat::B;
        item.data.push_back(value);
        return item;
    };
    static SecsItem u4(std::uint32_t value){
        SecsItem item;
        item.format = SecsFormat::U4;
        item.data.push_back(value >> 24);
        item.data.push_back(value >> 16);
        item.data.push_back(value >> 8);
        item.data.push_back(value >> 0);
        return item;
    };

    static SecsItem f4(const float value) {
        SecsItem item;
        item.format = SecsFormat::F4;
        const auto bits = std::bit_cast<std::uint32_t>(value);
        item.data.push_back(bits >> 24);
        item.data.push_back(bits >> 16);
        item.data.push_back(bits >> 8);
        item.data.push_back(bits >> 0);

        return item;
    }

    [[nodiscard]] std::vector<std::uint8_t> encode() const{
        std::vector<std::uint8_t> out;
        out.push_back((static_cast<std::uint8_t>(format) << 2) | 1);  // format byte: type in top 6 bits, |1 = length field is 1 byte wide
        if (format == SecsFormat::L){ //is a valid list
            out.push_back(items.size());
            for(const SecsItem& child : items){ //count bytes of the value
                auto childBytes = child.encode();
                out.insert(out.end(), childBytes.begin(), childBytes.end());
            }
        }else {
            out.push_back(data.size());
            out.insert(out.end(), data.begin(), data.end());
        }
        return out;
    }

    static std::optional<SecsItem> decode(const std::vector<std::uint8_t>& bytes){
        std::size_t pos = 0;
        auto item = decodeAt(bytes, pos);
        if (!item || pos != bytes.size())
            return std::nullopt;
        return item;
    }

private:
    static std::optional<SecsItem> decodeAt(const std::vector<std::uint8_t>& bytes, std::size_t& pos){
        if (pos >= bytes.size())
            return std::nullopt;

        const std::uint8_t formatByte = bytes[pos++];
        const std::size_t lengthBytes = formatByte & 0x03;
        if (lengthBytes == 0 || bytes.size() - pos < lengthBytes)
            return std::nullopt;

        std::size_t length = 0;
        for (std::size_t i = 0; i < lengthBytes; ++i)
            length = (length << 8) | bytes[pos++];

        SecsItem item;
        item.format = static_cast<SecsFormat>(formatByte >> 2);

        if (item.format == SecsFormat::L){
            for (std::size_t i = 0; i < length; ++i){
                auto child = decodeAt(bytes, pos);
                if (!child)
                    return std::nullopt;
                item.items.push_back(std::move(*child));
            }
        }else {
            if (bytes.size() - pos < length)
                return std::nullopt;
            item.data.assign(bytes.begin() + pos, bytes.begin() + pos + length);
            pos += length;
        }
        return item;
    }
};