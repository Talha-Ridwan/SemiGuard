#include "TelemetryPlotWidget.hpp"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>

TelemetryPlotWidget::TelemetryPlotWidget(QWidget* parent) : QWidget(parent)
{
    setMinimumSize(240, 240);
}

void TelemetryPlotWidget::addSample(float thickness)
{
    samples_.push_back(thickness);
    if (samples_.size() > MaxSamples)
        samples_.erase(samples_.begin());
    update();
}

void TelemetryPlotWidget::clearSamples()
{
    samples_.clear();
    update();
}

void TelemetryPlotWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor("#fafbfc"));

    const double left = 52.0;
    const double pad  = 12.0;
    const QRectF plot(left, pad, width() - left - pad, height() - 2 * pad - 14.0);

    p.setPen(QColor("#d0d3d8"));
    p.drawRect(plot);

    if (samples_.empty())
        return;

    float lo = *std::min_element(samples_.begin(), samples_.end());
    float hi = *std::max_element(samples_.begin(), samples_.end());
    if (hi - lo < 1.0f) { lo -= 1.0f; hi += 1.0f; }
    const float span = (hi - lo);
    lo -= span * 0.15f;
    hi += span * 0.15f;

    const auto xAt = [&](std::size_t i) {
        if (samples_.size() == 1) return plot.center().x();
        return plot.left() + plot.width() * double(i) / double(samples_.size() - 1);
    };
    const auto yAt = [&](float v) {
        return plot.bottom() - plot.height() * double((v - lo) / (hi - lo));
    };

    p.setPen(QColor("#7a828c"));
    p.drawText(QRectF(0, plot.top() - 6, left - 6, 16), Qt::AlignRight | Qt::AlignVCenter,
               QString::number(hi, 'f', 0));
    p.drawText(QRectF(0, plot.bottom() - 8, left - 6, 16), Qt::AlignRight | Qt::AlignVCenter,
               QString::number(lo, 'f', 0));
    p.drawText(QRectF(plot.left(), plot.bottom() + 2, plot.width(), 14),
               Qt::AlignHCenter, "thickness (nm) over scan order");

    QPainterPath path;
    for (std::size_t i = 0; i < samples_.size(); ++i)
    {
        const QPointF pt(xAt(i), yAt(samples_[i]));
        if (i == 0) path.moveTo(pt);
        else        path.lineTo(pt);
    }
    p.setPen(QPen(QColor("#2d7dd2"), 2.0));
    p.drawPath(path);

    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#2d7dd2"));
    for (std::size_t i = 0; i < samples_.size(); ++i)
        p.drawEllipse(QPointF(xAt(i), yAt(samples_[i])), 3.0, 3.0);
}
