#pragma once

#include <QByteArray>
#include <QObject>
#include <QtGlobal>

class QTcpSocket;
struct HsmsHeader;

struct HsmsFrameView
{
    quint16    sessionId = 0;
    quint8     stream    = 0;
    quint8     function  = 0;
    bool       wbit      = false;
    quint8     stype     = 0;
    quint32    sysBytes  = 0;
    QByteArray body;
};

class SecsClient : public QObject
{
    Q_OBJECT

public:
    explicit SecsClient(QObject* parent = nullptr);

    void connectToTool(const QString& host, quint16 port);
    void disconnectFromTool();
    void sendHostCommand(const QString& rcmd);

signals:
    void connected();
    void disconnected();
    void errorText(const QString& message);
    void frameReceived(const HsmsFrameView& frame);

private slots:
    void onConnected();
    void onReadyRead();

private:
    void sendSelect();
    void writeFrame(const HsmsHeader& header, const QByteArray& body = {});

    QTcpSocket* socket_  = nullptr;
    QByteArray  rxBuf_;
    quint32     nextSys_ = 0;
};
