#include "sessionstore.h"

#include <QDateTime>
#include <QDir>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QVariant>

namespace {
qint64 startOfDay(const QDate &d)
{
    return QDateTime(d, QTime(0, 0)).toSecsSinceEpoch();
}
} // namespace

bool SessionStore::open(QString *error)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    m_path = dir + QStringLiteral("/powerlog.sqlite");

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"));
    db.setDatabaseName(m_path);
    if (!db.open()) {
        if (error)
            *error = db.lastError().text();
        return false;
    }
    QSqlQuery q;
    q.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS sessions ("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " start_ts INTEGER NOT NULL, end_ts INTEGER NOT NULL,"
        " active_sec INTEGER NOT NULL DEFAULT 0,"
        " cpu_wh REAL DEFAULT 0, gpu_wh REAL DEFAULT 0, monitor_wh REAL DEFAULT 0,"
        " total_wh REAL DEFAULT 0, peak_w REAL DEFAULT 0)"));
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS samples ("
        " session_id INTEGER NOT NULL, ts INTEGER NOT NULL,"
        " cpu_w REAL, gpu_w REAL, monitor_w REAL, total_w REAL, energy_wh REAL)"));
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_samples_ts ON samples(ts)"));
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_samples_session ON samples(session_id)"));
    return true;
}

qint64 SessionStore::beginSession(qint64 startTs)
{
    QSqlQuery q;
    q.prepare(QStringLiteral("INSERT INTO sessions(start_ts, end_ts) VALUES(?, ?)"));
    q.addBindValue(startTs);
    q.addBindValue(startTs);
    if (!q.exec())
        return -1;
    return q.lastInsertId().toLongLong();
}

void SessionStore::updateSession(const SessionRecord &s)
{
    QSqlQuery q;
    q.prepare(QStringLiteral(
        "UPDATE sessions SET end_ts=?, active_sec=?, cpu_wh=?, gpu_wh=?, monitor_wh=?,"
        " total_wh=?, peak_w=? WHERE id=?"));
    q.addBindValue(s.endTs);
    q.addBindValue(s.activeSec);
    q.addBindValue(s.cpuWh);
    q.addBindValue(s.gpuWh);
    q.addBindValue(s.monitorWh);
    q.addBindValue(s.totalWh);
    q.addBindValue(s.peakW);
    q.addBindValue(s.id);
    q.exec();
}

void SessionStore::addSample(qint64 sessionId, qint64 ts, double cpuW, double gpuW,
                             double monitorW, double totalW, double energyWh)
{
    QSqlQuery q;
    q.prepare(QStringLiteral(
        "INSERT INTO samples(session_id, ts, cpu_w, gpu_w, monitor_w, total_w, energy_wh)"
        " VALUES(?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(sessionId);
    q.addBindValue(ts);
    q.addBindValue(cpuW);
    q.addBindValue(gpuW);
    q.addBindValue(monitorW);
    q.addBindValue(totalW);
    q.addBindValue(energyWh);
    q.exec();
}

void SessionStore::deleteSession(qint64 id)
{
    QSqlQuery q;
    q.prepare(QStringLiteral("DELETE FROM samples WHERE session_id=?"));
    q.addBindValue(id);
    q.exec();
    q.prepare(QStringLiteral("DELETE FROM sessions WHERE id=?"));
    q.addBindValue(id);
    q.exec();
}

QVector<SessionRecord> SessionStore::sessions(qint64 fromTs, qint64 toTs) const
{
    QSqlQuery q;
    QString sql = QStringLiteral(
        "SELECT id, start_ts, end_ts, active_sec, cpu_wh, gpu_wh, monitor_wh, total_wh, peak_w"
        " FROM sessions");
    if (toTs > 0)
        sql += QStringLiteral(" WHERE start_ts < ? AND end_ts >= ?");
    sql += QStringLiteral(" ORDER BY start_ts DESC");
    q.prepare(sql);
    if (toTs > 0) {
        q.addBindValue(toTs);
        q.addBindValue(fromTs);
    }
    QVector<SessionRecord> out;
    if (!q.exec())
        return out;
    while (q.next()) {
        SessionRecord r;
        r.id = q.value(0).toLongLong();
        r.startTs = q.value(1).toLongLong();
        r.endTs = q.value(2).toLongLong();
        r.activeSec = q.value(3).toLongLong();
        r.cpuWh = q.value(4).toDouble();
        r.gpuWh = q.value(5).toDouble();
        r.monitorWh = q.value(6).toDouble();
        r.totalWh = q.value(7).toDouble();
        r.peakW = q.value(8).toDouble();
        out.append(r);
    }
    return out;
}

QMap<QDate, double> SessionStore::dailyEnergy(const QDate &from, const QDate &to) const
{
    QMap<QDate, double> out;
    QSqlQuery q;
    q.prepare(QStringLiteral(
        "SELECT date(ts, 'unixepoch', 'localtime') AS d, SUM(energy_wh) FROM samples"
        " WHERE ts >= ? AND ts < ? GROUP BY d"));
    q.addBindValue(startOfDay(from));
    q.addBindValue(startOfDay(to.addDays(1)));
    if (!q.exec())
        return out;
    while (q.next()) {
        const QDate d = QDate::fromString(q.value(0).toString(), Qt::ISODate);
        if (d.isValid())
            out.insert(d, q.value(1).toDouble());
    }
    return out;
}
