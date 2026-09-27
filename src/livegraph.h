#pragma once

#include <QColor>
#include <QVector>
#include <QWidget>

// Minimal real-time line chart drawn with QPainter (no QtCharts dependency).
class LiveGraph : public QWidget
{
    Q_OBJECT
public:
    struct Series {
        QString name;
        QColor color;
        bool visible = true;
    };

    explicit LiveGraph(QWidget *parent = nullptr);

    void setSeries(const QVector<Series> &series);
    void setSeriesVisible(int index, bool visible);
    void setWindowSeconds(int seconds);
    // values.size() must match the number of series
    void addPoint(qint64 msSinceEpoch, const QVector<double> &values);
    void clear();

    QSize sizeHint() const override { return {600, 260}; }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    struct Point {
        qint64 ms;
        QVector<double> v;
    };
    QVector<Series> m_series;
    QVector<Point> m_points;
    int m_windowSec = 300;
};
