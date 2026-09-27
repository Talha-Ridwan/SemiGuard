#pragma once

#include <QWidget>

#include <vector>

class WaferMapWidget : public QWidget
{
    Q_OBJECT

public:
    explicit WaferMapWidget(QWidget* parent = nullptr);

    void addPoint(float x, float y, float thickness);
    void clearPoints();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    struct Die { float x; float y; float thickness; };
    std::vector<Die> dies_;

    static constexpr float WaferRadiusMm = 150.0f;  // 300mm wafer
};
