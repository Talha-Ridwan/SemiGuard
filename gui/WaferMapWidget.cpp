#include "WaferMapWidget.hpp"

#include <QPainter>

#include <algorithm>

namespace {

QColor thicknessColor(float t)
{
    constexpr float lo = 460.0f;
    constexpr float hi = 540.0f;
    const float n = std::clamp((t - lo) / (hi - lo), 0.0f, 1.0f);
    const float hue = (1.0f - n) * 240.0f / 360.0f;  // 240deg blue (thin) -> 0deg red (thick)
    return QColor::fromHsvF(hue, 0.85, 0.95);
}

} // namespace

WaferMapWidget::WaferMapWidget(QWidget* parent) : QWidget(parent)
{
    setMinimumSize(240, 240);
}

void WaferMapWidget::addPoint(float x, float y, float thickness)
{
    dies_.push_back({x, y, thickness});
    update();
}

void WaferMapWidget::clearPoints()
{
    dies_.clear();
    update();
}

void WaferMapWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const double w = width();
    const double h = height();
    const QPointF c(w / 2.0, h / 2.0);
    const double margin = 16.0;
    const double scale = (std::min(w, h) / 2.0 - margin) / WaferRadiusMm;
    const double rpx = WaferRadiusMm * scale;

    p.setPen(QPen(QColor("#8a8f98"), 1.0));
    p.setBrush(QColor("#e9eaee"));
    p.drawEllipse(c, rpx, rpx);
    p.drawLine(QPointF(c.x() - 9, c.y() + rpx), QPointF(c.x() + 9, c.y() + rpx));  // notch

    for (const Die& d : dies_)
    {
        const QPointF px(c.x() + d.x * scale, c.y() - d.y * scale);  // y up on screen
        p.setPen(Qt::NoPen);
        p.setBrush(thicknessColor(d.thickness));
        p.drawEllipse(px, 7.0, 7.0);
    }
}
