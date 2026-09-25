#pragma once

#include "HsmsHeader.hpp"
#include "SecsMessage.hpp"
#include "Core/GEMStateMachine.hpp"
#include <Common/Types.hpp>
#include <cerrno>
#include <cstddef>
#include <iostream>
#include <optional>
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

struct HsmsFrame
{
    HsmsHeader header;
    std::vector<std::uint8_t> body;
};

class TcpServer
{
    std::uint16_t port_;
    int listenFd_ = -1;
    int clientFd_ = -1;
    std::uint32_t nextSys_ = 0;

    static constexpr std::uint32_t MaxFrameLength = 1024 * 1024;

public:
    explicit TcpServer(std::uint16_t port) : port_(port) {}

    TcpServer(const TcpServer &) = delete;
    TcpServer &operator=(const TcpServer &) = delete;

    ~TcpServer()
    {
        if (clientFd_ != -1)
            close(clientFd_);
        if (listenFd_ != -1)
            close(listenFd_);
    }

    bool start()
    {
        listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (listenFd_ == -1)
            return false;

        int one = 1;
        setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port_);
        addr.sin_addr.s_addr = htonl(INADDR_ANY);

        if (bind(listenFd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == -1 ||
            listen(listenFd_, 1) == -1)
        {
            close(listenFd_);
            listenFd_ = -1;
            return false;
        }
        return true;
    }

    bool acceptClient()
    {
        clientFd_ = accept(listenFd_, nullptr, nullptr);
        return clientFd_ != -1;
    }

    [[nodiscard]] std::optional<HsmsFrame> readFrame() const {
        std::array<std::uint8_t, 4> lenBuf{};
        if (!readExact(lenBuf.data(), lenBuf.size()))
            return std::nullopt;

        // reassemble 4 bytes into one 32-bit number: shift each back up to its
        // slot and OR them together (mirror image of the split in sendFrame)
        std::uint32_t length = (static_cast<std::uint32_t>(lenBuf[0]) << 24) |
                               (static_cast<std::uint32_t>(lenBuf[1]) << 16) |
                               (static_cast<std::uint32_t>(lenBuf[2]) << 8) |
                               static_cast<std::uint32_t>(lenBuf[3]);

        if (length < HsmsHeader::WireSize || length > MaxFrameLength)
            return std::nullopt;

        std::array<std::uint8_t, HsmsHeader::WireSize> hdrBuf{};
        if (!readExact(hdrBuf.data(), hdrBuf.size()))
            return std::nullopt;

        HsmsFrame frame;
        frame.header = HsmsHeader::decode(hdrBuf);
        frame.body.resize(length - HsmsHeader::WireSize);
        if (!frame.body.empty() && !readExact(frame.body.data(), frame.body.size()))
            return std::nullopt;

        return frame;
    }

    bool sendFrame(const HsmsHeader& header, const std::vector<std::uint8_t>& body = {})
    {
        const auto length = static_cast<std::uint32_t>(HsmsHeader::WireSize + body.size());

        std::vector<std::uint8_t> out;
        out.reserve(4 + length);
        // split one 32-bit number into 4 bytes, biggest first (big-endian):
        // slide the wanted byte down with >>, stencil the rest off with & 0xFF
        out.push_back(static_cast<std::uint8_t>(length >> 24));
        out.push_back(static_cast<std::uint8_t>((length >> 16) & 0xFF));
        out.push_back(static_cast<std::uint8_t>((length >> 8) & 0xFF));
        out.push_back(static_cast<std::uint8_t>(length & 0xFF));

        const auto hdr = header.encode();
        out.insert(out.end(), hdr.begin(), hdr.end());
        out.insert(out.end(), body.begin(), body.end());

        return writeAll(out.data(), out.size());
    }

    void serve(GEMStateMachine& gem)
    {
        while (const auto frame = readFrame())
        {
            switch (const HsmsHeader& req = frame->header; req.stype)
            {
            case SType::SelectReq:
            {
                HsmsHeader rsp;
                rsp.sessionId = req.sessionId;
                rsp.stype = SType::SelectRsp;
                rsp.sysBytes = req.sysBytes;
                sendFrame(rsp);
                std::cout << "[hsms] Select.req -> Select.rsp\n";
                break;
            }
            case SType::LinktestReq:
            {
                HsmsHeader rsp;
                rsp.sessionId = 0xFFFF;
                rsp.stype = SType::LinktestRsp;
                rsp.sysBytes = req.sysBytes;
                sendFrame(rsp);
                std::cout << "[hsms] Linktest.req -> Linktest.rsp\n";
                break;
            }
            case SType::Data: {
                std::cout << "[hsms] data S" << int(req.stream) << "F" << int(req.function)
                          << (req.wBit ? "W" : "") << ", " << frame->body.size() << " body bytes\n";

                auto root = SecsItem::decode(frame->body);

                if (req.stream == 1 && req.function == 13) {
                    SecsItem reply = SecsItem::list({
                        SecsItem::binary(0),
                        SecsItem::list({})
                    });

                    HsmsHeader rsp;
                    rsp.sessionId = req.sessionId;
                    rsp.stream = 1;
                    rsp.function = 14;
                    rsp.wBit = false;
                    rsp.stype = SType::Data;
                    rsp.sysBytes = req.sysBytes;

                    sendFrame(rsp, reply.encode());
                    std::cout << "[hsms] S1F13 -> S1F14 (COMMACK=0)\n";
                }

                if (req.stream == 2 && req.function == 41) {
                    if (root && !root->items.empty() && root->items[0].format == SecsFormat::A) {
                        const auto& d = root->items[0].data;
                        std::string rcmd(d.begin(), d.end());

                        std::uint8_t hcack;
                        if (rcmd == "START") {
                            hcack = (gem.switchState(State::SETUP) && //not atomic, watchdog might signal alarm after releasing lock
                                     gem.switchState(State::EXECUTING)) ? 0 : 2;
                        }
                        // STOP / PAUSE / RESUME / else  ← your turn
                        else if (rcmd == "STOP") {
                            hcack = gem.switchState(State::IDLE) ? 0 : 2;
                        }
                        else if (rcmd == "PAUSE") {
                            hcack = gem.switchState(State::PAUSED) ? 0 : 2;
                        }
                        else if (rcmd == "RESUME") {
                            hcack = gem.switchState(State::EXECUTING) ? 0 : 2;
                        }
                        else {
                            hcack = 1; //unknown command given
                        }

                        SecsItem reply = SecsItem::list({
                            SecsItem::binary(hcack),
                            SecsItem::list({})
                        });

                        HsmsHeader rsp;
                        rsp.sessionId = req.sessionId;
                        rsp.stream = 2;
                        rsp.function = 42;
                        rsp.wBit = false;
                        rsp.stype = SType::Data;
                        rsp.sysBytes = req.sysBytes;

                        sendFrame(rsp, reply.encode());
                        std::cout << "[hsms] S2F41 " << rcmd << " -> S2F42 (HCACK=" << int(hcack) << ")\n";
                    }
                }

                break;
            }
            default:
                std::cout << "[hsms] ignoring unsupported stype " << int(req.stype) << "\n";
                break;
            }
        }

        close(clientFd_);
        clientFd_ = -1;
        std::cout << "[hsms] host disconnected\n";
    }

private:
    bool readExact(std::uint8_t* dst, const std::size_t n) const
    {
        std::size_t got = 0;
        while (got < n)
        {
            ssize_t r = recv(clientFd_, dst + got, n - got, 0);
            if (r > 0)
                got += static_cast<std::size_t>(r);
            else if (r == -1 && errno == EINTR)
                continue;
            else
                return false;
        }
        return true;
    }

    bool writeAll(const std::uint8_t* src, const std::size_t n) const
    {
        std::size_t sent = 0;
        while (sent < n)
        {
            ssize_t w = send(clientFd_, src + sent, n - sent, MSG_NOSIGNAL);
            if (w > 0)
                sent += static_cast<std::size_t>(w);
            else if (w == -1 && errno == EINTR)
                continue;
            else
                return false;
        }
        return true;
    }

public:
    bool sendEventReport(const Measurement& m) {
        SecsItem values = SecsItem::list({
            SecsItem::f4(static_cast<float>(m.x)),
            SecsItem::f4(static_cast<float>(m.y)),
            SecsItem::f4(static_cast<float>(m.thickness_nm))
        });

        SecsItem report = SecsItem::list({ SecsItem::u4(1), values });

        SecsItem body = SecsItem::list({
            SecsItem::u4(0),               // DATAID
            SecsItem::u4(1),               // CEID
            SecsItem::list({ report })     // reports list (L,1)
        });

        HsmsHeader hdr;
        hdr.stream   = 6;
        hdr.function = 11;
        hdr.wBit     = false;
        hdr.stype    = SType::Data;
        hdr.sysBytes = ++nextSys_;
        return sendFrame(hdr, body.encode());
    }
};