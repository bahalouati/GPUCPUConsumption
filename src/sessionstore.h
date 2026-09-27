#pragma once

#include <QDate>
#include <QMap>
#include <QString>
#include <QVector>

struct SessionRecord {
    qint64 id = -1;
    qint64 startTs = 0;      // unix seconds
    qint64 endTs = 0;
    qint64 activeSec = 0;    // seconds actually measured (excludes suspend)
    double cpuWh = 0.0;
    double gpuWh = 0.0;
    double monitorWh = 0.0;
    double totalWh = 0.0;    // at the wall, incl. PSU losses and "other"
    double peakW = 0.0;

    double avgW() const { return activeSec > 0 ? totalWh * 3600.0 / activeSec : 0.0; }
};

// SQLite-backed log of sessions and one-minute samples.
class SessionStore
{
public:
    bool open(QString *error = nullptr);
    QString path() const { return m_path; }

    qint64 beginSession(qint64 startTs);
    void updateSession(const SessionRecord &s);
    void addSample(qint64 sessionId, qint64 ts, double cpuW, double gpuW, double monitorW,
                   double totalW, double energyWh);
    void deleteSession(qint64 id);

    QVector<SessionRecord> sessions(qint64 fromTs = 0, qint64 toTs = 0) const;
    // Wall energy (Wh) per local calendar day in [from, to].
    QMap<QDate, double> dailyEnergy(const QDate &from, const QDate &to) const;

private:
    QString m_path;
};
