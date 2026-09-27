#include "livegraph.h"

#include <QPainter>
#include <QPainterPath>
#include <cmath>

LiveGraph::LiveGraph(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(180);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void LiveGraph::setSeries(const QVector<Series> &series)
{
    m_series = series;
    m_points.clear();
    update();
}

void LiveGraph::setSeriesVisible(int index, bool visible)
{
    if (index >= 0 && index < m_series.size()) {
        m_series[index].visible = visible;
        update();
    }
}

void LiveGraph::setWindowSeconds(int seconds)
{
    m_windowSec = qMax(10, seconds);
    update();
}

void LiveGraph::addPoint(qint64 msSinceEpoch, const QVector<double> &values)
{
    m_points.append({msSinceEpoch, values});
    // Keep a bit more than the largest window we offer (1 h) to allow zooming out.
    const qint64 keepFrom = msSinceEpoch - 3600 * 1000LL;
    int drop = 0;
    while (drop < m_points.size() && m_points[drop].ms < keepFrom)
        ++drop;
    if (drop > 0)
        m_points.remove(0, drop);
    update();
}

void LiveGraph::clear()
{
    m_points.clear();
    update();
}

static double niceCeil(double v)
{
    if (v <= 0)
        return 10.0;
    const double mag = std::pow(10.0, std::floor(std::log10(v)));
    for (double m : {1.0, 2.0, 2.5, 5.0, 10.0}) {
        if (v <= m * mag)
            return m * mag;
    }
    return 10.0 * mag;
}

void LiveGraph::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QPalette pal = palette();
    p.fillRect(rect(), pal.base());

    const int left = 48, right = 12, top = 28, bottom = 24;
    const QRectF plot(left, top, width() - left - right, height() - top - bottom);
    if (plot.width() < 20 || plot.height() < 20)
        return;

    const qint64 nowMs = m_points.isEmpty() ? 0 : m_points.last().ms;
    const qint64 fromMs = nowMs - m_windowSec * 1000LL;

    double maxV = 0.0;
    for (const Point &pt : m_points) {
        if (pt.ms < fromMs)
            continue;
        for (int i = 0; i < pt.v.size() && i < m_series.size(); ++i)
            if (m_series[i].visible)
                maxV = qMax(maxV, pt.v[i]);
    }
    maxV = niceCeil(maxV * 1.1);

    // Grid + y labels
    QColor gridColor = pal.text().color();
    gridColor.setAlpha(40);
    QColor labelColor = pal.text().color();
    labelColor.setAlpha(160);
    QFont f = font();
    f.setPointSizeF(f.pointSizeF() * 0.85);
    p.setFont(f);
    const int ySteps = 4;
    for (int i = 0; i <= ySteps; ++i) {
        const double y = plot.bottom() - plot.height() * i / ySteps;
        p.setPen(QPen(gridColor, 1));
        p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        p.setPen(labelColor);
        p.drawText(QRectF(0, y - 8, left - 6, 16), Qt::AlignRight | Qt::AlignVCenter,
                   QStringLiteral("%1 W").arg(maxV * i / ySteps, 0, 'f', 0));
    }
    // x labels
    p.setPen(labelColor);
    auto fmtSec = [](int s) {
        return s >= 60 ? QStringLiteral("-%1 min").arg(s / 60.0, 0, 'g', 3)
                       : QStringLiteral("-%1 s").arg(s);
    };
    p.drawText(QRectF(plot.left(), plot.bottom() + 4, 80, 16), Qt::AlignLeft, fmtSec(m_windowSec));
    p.drawText(QRectF(plot.center().x() - 40, plot.bottom() + 4, 80, 16), Qt::AlignHCenter,
               fmtSec(m_windowSec / 2));
    p.drawText(QRectF(plot.right() - 80, plot.bottom() + 4, 80, 16), Qt::AlignRight,
               QStringLiteral("now"));

    // Legend
    qreal lx = plot.left();
    for (const Series &s : m_series) {
        QColor c = s.color;
        if (!s.visible)
            c.setAlpha(60);
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawRoundedRect(QRectF(lx, 8, 12, 12), 3, 3);
        p.setPen(s.visible ? pal.text().color() : labelColor);
        const int w = p.fontMetrics().horizontalAdvance(s.name);
        p.drawText(QPointF(lx + 16, 18), s.name);
        lx += 16 + w + 16;
    }

    if (m_points.size() < 2)
        return;

    p.setClipRect(plot.adjusted(-1, -1, 1, 1));
    for (int si = 0; si < m_series.size(); ++si) {
        if (!m_series[si].visible)
            continue;
        QPainterPath path;
        bool started = false;
        for (const Point &pt : m_points) {
            if (pt.ms < fromMs - 5000 || si >= pt.v.size())
                continue;
            const double x = plot.left() + plot.width() * double(pt.ms - fromMs) / (m_windowSec * 1000.0);
            const double y = plot.bottom() - plot.height() * (pt.v[si] / maxV);
            if (!started) {
                path.moveTo(x, y);
                started = true;
            } else {
                path.lineTo(x, y);
            }
        }
        p.setPen(QPen(m_series[si].color, si == m_series.size() - 1 ? 2.5 : 1.8));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    }
}
