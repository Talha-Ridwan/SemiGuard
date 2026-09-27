#pragma once

#include "SecsClient.hpp"

#include <QMainWindow>

class QLabel;
class QPushButton;
class QTableWidget;
class QGroupBox;
class WaferMapWidget;
class TelemetryPlotWidget;

class DashboardWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit DashboardWindow(QWidget* parent = nullptr);

private slots:
    void onConnectClicked();
    void onOnline();
    void onOffline();
    void onError(const QString& message);
    void onFrame(const HsmsFrameView& frame);

    void onStart();
    void onStop();
    void onPause();
    void onAbort();

private:
    void sendCommand(const QString& rcmd);
    void setState(const QString& name);
    void appendLog(const QString& dir, const QString& msg, const QString& detail);

    SecsClient* client_ = nullptr;
    QString     pendingState_;

    WaferMapWidget*      waferMap_      = nullptr;
    TelemetryPlotWidget* telemetryPlot_ = nullptr;

    QLabel*       stateChip_    = nullptr;
    QLabel*       connLed_      = nullptr;
    QGroupBox*    waferMapBox_  = nullptr;
    QGroupBox*    telemetryBox_ = nullptr;
    QTableWidget* logTable_     = nullptr;

    QPushButton*  connectBtn_   = nullptr;
    QPushButton*  startBtn_     = nullptr;
    QPushButton*  pauseBtn_     = nullptr;
    QPushButton*  stopBtn_      = nullptr;
    QPushButton*  abortBtn_     = nullptr;
};
