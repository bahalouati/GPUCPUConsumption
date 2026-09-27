#pragma once

#include "powermonitor.h"
#include "sessionstore.h"
#include "settings.h"

#include <QMainWindow>
#include <QSystemTrayIcon>
#include <QTimer>

class QLabel;
class QComboBox;
class QSlider;
class QSpinBox;
class QTabWidget;
class QTableWidget;
class QTreeWidget;
class LiveGraph;
class EnergyCalendar;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(SessionStore *store, QWidget *parent = nullptr);
    ~MainWindow() override;

    bool startHidden() const;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    struct Card {
        QLabel *value = nullptr;
        QLabel *detail = nullptr;
    };

    QWidget *buildLiveTab();
    QWidget *buildCalendarTab();
    QWidget *buildLogTab();
    QWidget *buildSettingsTab();
    Card makeCard(const QString &title, const QColor &accent, QWidget *parent, QLayout *into);
    void setupTray();

    void tick();
    void flush();
    void startNewSession();
    void finishSession();
    void quitApp();
    void settingsChanged();
    void updateMonitorLabel();
    void updateSessionLabels();
    void updateTray(double wallW, const PowerMonitor::Sample &s, double monitorW);

    void refreshCalendar();
    void showDay(const QDate &day);
    void refreshLog();
    void exportCsv();
    void deleteSelectedSession();

    QString fmtEnergy(double wh) const;
    QString fmtCost(double wh) const;
    static QString fmtDuration(qint64 sec);

    AppSettings m_s;
    PowerMonitor m_pm;
    SessionStore *m_store;

    QTimer m_sampleTimer;
    QTimer m_flushTimer;

    SessionRecord m_session;
    double m_activeSec = 0.0;
    qint64 m_lastTickMs = 0;
    // accumulated since last flush (watt-seconds)
    double m_accCpu = 0.0, m_accGpu = 0.0, m_accMon = 0.0, m_accTotal = 0.0, m_accSec = 0.0;
    double m_todayFlushedWh = 0.0;
    QDate m_todayDate;
    bool m_finished = false;
    bool m_quitting = false;
    bool m_trayMessageShown = false;

    // Widgets
    QTabWidget *m_tabs = nullptr;
    Card m_cpuCard, m_gpuCard, m_monCard, m_totalCard;
    LiveGraph *m_graph = nullptr;
    QComboBox *m_monitorCombo = nullptr;
    QSlider *m_brightness = nullptr;
    QLabel *m_brightnessLabel = nullptr;
    QSpinBox *m_monitorCount = nullptr;
    QLabel *m_monitorWatts = nullptr;
    QLabel *m_sessStart = nullptr, *m_sessDuration = nullptr, *m_sessEnergy = nullptr,
           *m_sessAvg = nullptr, *m_sessCost = nullptr,
           *m_sessBreakdown = nullptr, *m_todayLabel = nullptr;

    EnergyCalendar *m_calendar = nullptr;
    QLabel *m_dayTitle = nullptr, *m_daySummary = nullptr, *m_monthSummary = nullptr;
    QTreeWidget *m_daySessions = nullptr;

    QTableWidget *m_logTable = nullptr;
    QLabel *m_logSummary = nullptr;

    QSystemTrayIcon *m_tray = nullptr;
};
