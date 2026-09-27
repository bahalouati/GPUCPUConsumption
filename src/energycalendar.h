#pragma once

#include <QCalendarWidget>
#include <QMap>

// Calendar that shades each day by how much energy was used and prints the
// daily kWh in the corner of the cell.
class EnergyCalendar : public QCalendarWidget
{
    Q_OBJECT
public:
    explicit EnergyCalendar(QWidget *parent = nullptr);
    void setEnergy(const QMap<QDate, double> &whPerDay);

protected:
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    void paintCell(QPainter *painter, const QRect &rect, QDate date) const override;
#else
    void paintCell(QPainter *painter, const QRect &rect, const QDate &date) const override;
#endif

private:
    QMap<QDate, double> m_wh;
    double m_maxWh = 0.0;
};
