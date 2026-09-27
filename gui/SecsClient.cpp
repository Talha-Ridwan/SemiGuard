#include "SecsClient.hpp"

#include "Protocol/HsmsHeader.hpp"
#include "Protocol/SecsMessage.hpp"

#include <QTcpSocket>
#include <array>
#include <string>

SecsClient::SecsClient(QObject* parent)
    : QObject(parent), socket_(new QTcpSocket(this))
{
    connect(socket_, &QTcpSocket::connected,    this, &SecsClient::onConnected);
    connect(socket_, &QTcpSocket::readyRead,    this, &SecsClient::onReadyRead);
    connect(socket_, &QTcpSocket::disconnected, this, &SecsClient::disconnected);
    connect(socket_, &QTcpSocket::errorOccurred, this,
            [this] { emit errorText(socket_->errorString()); });
}

void SecsClient::connectToTool(const QString& host, quint16 port)
{
    rxBuf_.clear();
    socket_->connectToHost(host, port);
}

void SecsClient::disconnectFromTool()
{
    socket_->disconnectFromHost();
}

void SecsClient::onConnected()
{
    emit connected();
    sendSelect();
}

void SecsClient::writeFrame(const HsmsHeader& header, const QByteArray& body)
{
    const quint32 length = quint32(HsmsHeader::WireSize) + quint32(body.size());
    const auto hdr = header.encode();

    QByteArray out;
    out.reserve(qsizetype(4) + qsizetype(length));
    out.append(char((length >> 24) & 0xFF));
    out.append(char((length >> 16) & 0xFF));
    out.append(char((length >> 8) & 0xFF));
    out.append(char(length & 0xFF));
    out.append(reinterpret_cast<const char*>(hdr.data()), qsizetype(hdr.size()));
    out.append(body);

    socket_->write(out);
}

void SecsClient::sendSelect()
{
    HsmsHeader h;
    h.sessionId = 0xFFFF;
    h.stype     = SType::SelectReq;
    h.sysBytes  = ++nextSys_;
    writeFrame(h);
}

void SecsClient::sendHostCommand(const QString& rcmd)
{
    if (!socket_ || socket_->state() != QAbstractSocket::ConnectedState)
    {
        emit errorText(QStringLiteral("not connected; %1 not sent").arg(rcmd));
        return;
    }

    SecsItem body = SecsItem::list({
        SecsItem::ascii(rcmd.toStdString()),
        SecsItem::list({})
    });
    const auto bytes = body.encode();
    const QByteArray qbody(reinterpret_cast<const char*>(bytes.data()), qsizetype(bytes.size()));

    HsmsHeader h;
    h.sessionId = 0x0001;
    h.stream    = 2;
    h.function  = 41;
    h.wBit      = true;
    h.stype     = SType::Data;
    h.sysBytes  = ++nextSys_;
    writeFrame(h, qbody);
}

void SecsClient::onReadyRead()
{
    rxBuf_.append(socket_->readAll());

    while (rxBuf_.size() >= 4)
    {
        const auto* p = reinterpret_cast<const quint8*>(rxBuf_.constData());
        const quint32 length = (quint32(p[0]) << 24) | (quint32(p[1]) << 16) |
                               (quint32(p[2]) << 8)  |  quint32(p[3]);

        if (length < HsmsHeader::WireSize || length > (1u << 20))
        {
            emit errorText(QStringLiteral("bad frame length %1; aborting").arg(length));
            socket_->abort();
            return;
        }

        if (rxBuf_.size() < qsizetype(4) + qsizetype(length))
            break;

        std::array<std::uint8_t, HsmsHeader::WireSize> hb{};
        for (std::size_t i = 0; i < hb.size(); ++i)
            hb[i] = quint8(rxBuf_[qsizetype(4) + qsizetype(i)]);
        const HsmsHeader h = HsmsHeader::decode(hb);

        HsmsFrameView v;
        v.sessionId = h.sessionId;
        v.stream    = h.stream;
        v.function  = h.function;
        v.wbit      = h.wBit;
        v.stype     = quint8(h.stype);
        v.sysBytes  = h.sysBytes;
        v.body      = rxBuf_.mid(qsizetype(4 + HsmsHeader::WireSize),
                                 qsizetype(length - HsmsHeader::WireSize));

        rxBuf_.remove(0, qsizetype(4) + qsizetype(length));
        emit frameReceived(v);
    }
}
