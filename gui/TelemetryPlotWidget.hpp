#pragma once

#include <QWidget>

#include <vector>

class TelemetryPlotWidget : public QWidget
{
    Q_OBJECT

public:
    explicit TelemetryPlotWidget(QWidget* parent = nullptr);

    void addSample(float thickness);
    void clearSamples();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    std::vector<float> samples_;

    static constexpr std::size_t MaxSamples = 240;
};
