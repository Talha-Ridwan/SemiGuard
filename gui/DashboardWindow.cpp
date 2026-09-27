#include "DashboardWindow.hpp"

#include "Protocol/SecsMessage.hpp"
#include "TelemetryPlotWidget.hpp"
#include "WaferMapWidget.hpp"

#include <QAbstractItemView>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTime>
#include <QVBoxLayout>
#include <QWidget>

#include <bit>
#include <optional>
#include <vector>

namespace {

std::optional<int> leadingAck(const HsmsFrameView& v)
{
    const std::vector<std::uint8_t> bytes(v.body.begin(), v.body.end());
    const auto root = SecsItem::decode(bytes);
    if (root && !root->items.empty() &&
        root->items[0].format == SecsFormat::B && !root->items[0].data.empty())
        return int(root->items[0].data[0]);
    return std::nullopt;
}

struct Measurement3 { float x; float y; float thickness; };

float readF4(const SecsItem& it)
{
    const std::uint32_t bits = (std::uint32_t(it.data[0]) << 24) |
                               (std::uint32_t(it.data[1]) << 16) |
                               (std::uint32_t(it.data[2]) << 8)  |
                                std::uint32_t(it.data[3]);
    return std::bit_cast<float>(bits);
}

// S6F11 body = L[ U4 DATAID, U4 CEID, L[ L[ U4 RPTID, L[ F4 x, F4 y, F4 thk ] ] ] ]
std::optional<Measurement3> parseS6F11(const HsmsFrameView& v)
{
    if (!(v.stype == 0 && v.stream == 6 && v.function == 11))
        return std::nullopt;

    const std::vector<std::uint8_t> bytes(v.body.begin(), v.body.end());
    const auto root = SecsItem::decode(bytes);
    if (!root || root->items.size() < 3)
        return std::nullopt;

    const SecsItem& reports = root->items[2];
    if (reports.format != SecsFormat::L || reports.items.empty())
        return std::nullopt;

    const SecsItem& report = reports.items[0];
    if (report.items.size() < 2)
        return std::nullopt;

    const SecsItem& values = report.items[1];
    if (values.format != SecsFormat::L || values.items.size() < 3)
        return std::nullopt;

    for (int i = 0; i < 3; ++i)
        if (values.items[i].format != SecsFormat::F4 || values.items[i].data.size() < 4)
            return std::nullopt;

    return Measurement3{ readF4(values.items[0]), readF4(values.items[1]), readF4(values.items[2]) };
}

QString frameLabel(const HsmsFrameView& v)
{
    if (v.stype == 0)
        return QStringLiteral("S%1F%2%3").arg(v.stream).arg(v.function).arg(
            v.wbit ? QStringLiteral("W") : QString());

    switch (v.stype)
    {
    case 1:  return QStringLiteral("Select.req");
    case 2:  return QStringLiteral("Select.rsp");
    case 5:  return QStringLiteral("Linktest.req");
    case 6:  return QStringLiteral("Linktest.rsp");
    default: return QStringLiteral("stype%1").arg(v.stype);
    }
}

// The state the tool reaches if this command is accepted (HCACK=0).
QString stateForCommand(const QString& rcmd)
{
    if (rcmd == "START" || rcmd == "RESUME") return QStringLiteral("EXECUTING");
    if (rcmd == "STOP")  return QStringLiteral("IDLE");
    if (rcmd == "PAUSE") return QStringLiteral("PAUSED");
    return QString();
}

} // namespace

DashboardWindow::DashboardWindow(QWidget* parent) : QMainWindow(parent)
{
    setWindowTitle("SemiGuard Dashboard");
    resize(1100, 720);

    auto* central = new QWidget;
    auto* root = new QVBoxLayout(central);

    auto* topBar = new QHBoxLayout;
    stateChip_ = new QLabel("STATE: —");
    stateChip_->setStyleSheet(
        "font-weight:bold; padding:6px 14px; border-radius:6px; background:#3a3f4b; color:white;");
    connLed_ = new QLabel("● offline");
    connLed_->setStyleSheet("color:#c0392b; font-weight:bold;");
    topBar->addWidget(stateChip_);
    topBar->addStretch();
    topBar->addWidget(connLed_);
    root->addLayout(topBar);

    auto* middle = new QHBoxLayout;

    waferMapBox_ = new QGroupBox("Wafer Map (300mm)");
    auto* wmInner = new QVBoxLayout(waferMapBox_);
    waferMap_ = new WaferMapWidget;
    wmInner->addWidget(waferMap_);

    telemetryBox_ = new QGroupBox("Telemetry (thickness)");
    auto* tInner = new QVBoxLayout(telemetryBox_);
    telemetryPlot_ = new TelemetryPlotWidget;
    tInner->addWidget(telemetryPlot_);

    middle->addWidget(waferMapBox_, 1);
    middle->addWidget(telemetryBox_, 1);
    root->addLayout(middle, 1);

    auto* controls = new QHBoxLayout;
    connectBtn_ = new QPushButton("Connect / Select");
    startBtn_   = new QPushButton("Start");
    pauseBtn_   = new QPushButton("Pause");
    stopBtn_    = new QPushButton("Stop");
    abortBtn_   = new QPushButton("EMERGENCY ABORT");
    abortBtn_->setStyleSheet("background:#c0392b; color:white; font-weight:bold;");
    for (auto* b : {connectBtn_, startBtn_, pauseBtn_, stopBtn_, abortBtn_})
        controls->addWidget(b);
    root->addLayout(controls);

    logTable_ = new QTableWidget(0, 4);
    logTable_->setHorizontalHeaderLabels({"Time", "Dir", "Message", "Detail"});
    logTable_->horizontalHeader()->setStretchLastSection(true);
    logTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    root->addWidget(logTable_);

    setCentralWidget(central);

    client_ = new SecsClient(this);
    connect(connectBtn_, &QPushButton::clicked, this, &DashboardWindow::onConnectClicked);
    connect(startBtn_,   &QPushButton::clicked, this, &DashboardWindow::onStart);
    connect(pauseBtn_,   &QPushButton::clicked, this, &DashboardWindow::onPause);
    connect(stopBtn_,    &QPushButton::clicked, this, &DashboardWindow::onStop);
    connect(abortBtn_,   &QPushButton::clicked, this, &DashboardWindow::onAbort);

    connect(client_, &SecsClient::connected,     this, &DashboardWindow::onOnline);
    connect(client_, &SecsClient::disconnected,  this, &DashboardWindow::onOffline);
    connect(client_, &SecsClient::errorText,     this, &DashboardWindow::onError);
    connect(client_, &SecsClient::frameReceived, this, &DashboardWindow::onFrame);
}

void DashboardWindow::onConnectClicked()
{
    appendLog("-->", "Connect", "127.0.0.1:5000");
    client_->connectToTool("127.0.0.1", 5000);
}

void DashboardWindow::onOnline()
{
    connLed_->setText("● online");
    connLed_->setStyleSheet("color:#27ae60; font-weight:bold;");
    appendLog("--", "Select.req", "auto-sent after TCP connect");
}

void DashboardWindow::onOffline()
{
    connLed_->setText("● offline");
    connLed_->setStyleSheet("color:#c0392b; font-weight:bold;");
    pendingState_.clear();
    setState(QStringLiteral("—"));
    appendLog("--", "disconnected", QString());
}

void DashboardWindow::onError(const QString& message)
{
    appendLog("--", "error", message);
}

void DashboardWindow::onFrame(const HsmsFrameView& v)
{
    QString detail;

    if (const auto m = parseS6F11(v))
    {
        waferMap_->addPoint(m->x, m->y, m->thickness);
        telemetryPlot_->addSample(m->thickness);
        detail = QStringLiteral("x=%1  y=%2  thk=%3 nm")
                     .arg(double(m->x), 0, 'f', 1)
                     .arg(double(m->y), 0, 'f', 1)
                     .arg(double(m->thickness), 0, 'f', 2);
    }
    else if (const auto ack = leadingAck(v); ack && v.stype == 0 && v.stream == 2 && v.function == 42)
    {
        detail = QStringLiteral("HCACK=%1").arg(*ack);
        if (*ack == 0 && !pendingState_.isEmpty())
            setState(pendingState_);
        pendingState_.clear();
    }
    else if (const auto ack = leadingAck(v); ack && v.stype == 0 && v.stream == 1 && v.function == 14)
        detail = QStringLiteral("COMMACK=%1").arg(*ack);
    else
        detail = QStringLiteral("sys=%1, %2 body bytes").arg(v.sysBytes).arg(v.body.size());

    appendLog("<--", frameLabel(v), detail);
}

void DashboardWindow::sendCommand(const QString& rcmd)
{
    pendingState_ = stateForCommand(rcmd);
    appendLog("-->", QStringLiteral("S2F41 %1").arg(rcmd), QStringLiteral("wBit=1"));
    client_->sendHostCommand(rcmd);
}

void DashboardWindow::onStart() { sendCommand(QStringLiteral("START")); }
void DashboardWindow::onStop()  { sendCommand(QStringLiteral("STOP")); }
void DashboardWindow::onPause() { sendCommand(QStringLiteral("PAUSE")); }

void DashboardWindow::onAbort()
{
    pendingState_ = QStringLiteral("IDLE");
    appendLog("-->", QStringLiteral("EMERGENCY ABORT"), QStringLiteral("→ S2F41 STOP (halt)"));
    client_->sendHostCommand(QStringLiteral("STOP"));
}

void DashboardWindow::setState(const QString& name)
{
    stateChip_->setText(QStringLiteral("STATE: %1").arg(name));

    QString bg = QStringLiteral("#3a3f4b");
    if      (name == "IDLE")      bg = QStringLiteral("#5d6470");
    else if (name == "SETUP")     bg = QStringLiteral("#2d7dd2");
    else if (name == "EXECUTING") bg = QStringLiteral("#27ae60");
    else if (name == "PAUSED")    bg = QStringLiteral("#e0a800");
    else if (name == "ALARM")     bg = QStringLiteral("#c0392b");

    stateChip_->setStyleSheet(
        QStringLiteral("font-weight:bold; padding:6px 14px; border-radius:6px; background:%1; color:white;")
            .arg(bg));
}

void DashboardWindow::appendLog(const QString& dir, const QString& msg, const QString& detail)
{
    const int r = logTable_->rowCount();
    logTable_->insertRow(r);
    logTable_->setItem(r, 0, new QTableWidgetItem(QTime::currentTime().toString("HH:mm:ss.zzz")));
    logTable_->setItem(r, 1, new QTableWidgetItem(dir));
    logTable_->setItem(r, 2, new QTableWidgetItem(msg));
    logTable_->setItem(r, 3, new QTableWidgetItem(detail));
    logTable_->scrollToBottom();
}
