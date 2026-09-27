#include "energycalendar.h"

#include <QPainter>

EnergyCalendar::EnergyCalendar(QWidget *parent)
    : QCalendarWidget(parent)
{
    setGridVisible(true);
    setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
    setMinimumSize(420, 320);
}

void EnergyCalendar::setEnergy(const QMap<QDate, double> &whPerDay)
{
    m_wh = whPerDay;
    m_maxWh = 0.0;
    for (double v : whPerDay)
        m_maxWh = qMax(m_maxWh, v);
    updateCells();
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void EnergyCalendar::paintCell(QPainter *painter, const QRect &rect, QDate date) const
#else
void EnergyCalendar::paintCell(QPainter *painter, const QRect &rect, const QDate &date) const
#endif
{
    QCalendarWidget::paintCell(painter, rect, date);

    const double wh = m_wh.value(date, 0.0);
    if (wh <= 0.0 || m_maxWh <= 0.0)
        return;

    painter->save();
    const double t = qBound(0.0, wh / m_maxWh, 1.0);
    QColor heat = QColor::fromHsvF((1.0 - t) * 0.33, 0.85, 0.9);  // green -> red
    heat.setAlphaF(0.18 + 0.32 * t);
    painter->fillRect(rect.adjusted(1, 1, -1, -1), heat);

    QFont f = painter->font();
    f.setPointSizeF(qMax(6.0, f.pointSizeF() * 0.7));
    painter->setFont(f);
    painter->setPen(palette().text().color());
    const QString label = wh >= 1000.0 ? QStringLiteral("%1 kWh").arg(wh / 1000.0, 0, 'f', 2)
                                       : QStringLiteral("%1 Wh").arg(wh, 0, 'f', wh < 10.0 ? 1 : 0);
    painter->drawText(rect.adjusted(2, 0, -3, -2), Qt::AlignRight | Qt::AlignBottom, label);
    painter->restore();
}
