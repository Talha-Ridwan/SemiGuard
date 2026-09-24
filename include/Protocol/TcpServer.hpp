#pragma once

#include "HsmsHeader.hpp"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
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

    std::optional<HsmsFrame> readFrame()
    {
        std::array<std::uint8_t, 4> lenBuf{};
        if (!readExact(lenBuf.data(), lenBuf.size()))
            return std::nullopt;

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
        out.push_back(static_cast<std::uint8_t>(length >> 24));
        out.push_back(static_cast<std::uint8_t>((length >> 16) & 0xFF));
        out.push_back(static_cast<std::uint8_t>((length >> 8) & 0xFF));
        out.push_back(static_cast<std::uint8_t>(length & 0xFF));

        const auto hdr = header.encode();
        out.insert(out.end(), hdr.begin(), hdr.end());
        out.insert(out.end(), body.begin(), body.end());

        return writeAll(out.data(), out.size());
    }

    void serve()
    {
        while (auto frame = readFrame())
        {
            const HsmsHeader& req = frame->header;

            switch (req.stype)
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
            case SType::Data:
                std::cout << "[hsms] data S" << int(req.stream) << "F" << int(req.function)
                          << (req.wBit ? "W" : "") << ", " << frame->body.size() << " body bytes\n";
                break;
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
    bool readExact(std::uint8_t* dst, std::size_t n)
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

    bool writeAll(const std::uint8_t* src, std::size_t n)
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
};