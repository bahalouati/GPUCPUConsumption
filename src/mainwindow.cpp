#include "mainwindow.h"

#include "energycalendar.h"
#include "livegraph.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextStream>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
const QColor kCpuColor(0x3b, 0x82, 0xf6);
const QColor kGpuColor(0x22, 0xc5, 0x5e);
const QColor kMonColor(0xf5, 0x9e, 0x0b);
const QColor kTotalColor(0xef, 0x44, 0x44);

qint64 dayStart(const QDate &d) { return QDateTime(d, QTime(0, 0)).toSecsSinceEpoch(); }

QString fmtTime(qint64 ts, const QString &fmt = QStringLiteral("yyyy-MM-dd HH:mm"))
{
    return QDateTime::fromSecsSinceEpoch(ts).toString(fmt);
}
} // namespace

MainWindow::MainWindow(SessionStore *store, QWidget *parent)
    : QMainWindow(parent)
    , m_store(store)
{
    m_s.load();
    setWindowTitle(tr("Power Meter"));
    resize(900, 680);

    m_tabs = new QTabWidget(this);
    m_tabs->addTab(buildLiveTab(), tr("Live"));
    m_tabs->addTab(buildCalendarTab(), tr("Calendar"));
    m_tabs->addTab(buildLogTab(), tr("Sessions log"));
    m_tabs->addTab(buildSettingsTab(), tr("Settings"));
    setCentralWidget(m_tabs);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int idx) {
        if (idx == 1)
            refreshCalendar();
        else if (idx == 2)
            refreshLog();
    });

    setupTray();
    setWindowFlag(Qt::WindowStaysOnTopHint, m_s.alwaysOnTop);

    startNewSession();

    m_sampleTimer.setInterval(m_s.sampleIntervalMs);
    connect(&m_sampleTimer, &QTimer::timeout, this, &MainWindow::tick);
    m_sampleTimer.start();

    m_flushTimer.setInterval(60 * 1000);
    connect(&m_flushTimer, &QTimer::timeout, this, [this] {
        flush();
        if (m_tabs->currentIndex() == 1)
            refreshCalendar();
    });
    m_flushTimer.start();

    connect(qApp, &QCoreApplication::aboutToQuit, this, &MainWindow::finishSession);
    tick();
}

MainWindow::~MainWindow()
{
    finishSession();
}

bool MainWindow::startHidden() const
{
    return m_s.startMinimized && m_tray;
}

// ---------------------------------------------------------------- UI setup

MainWindow::Card MainWindow::makeCard(const QString &title, const QColor &accent,
                                      QWidget *parent, QLayout *into)
{
    auto *frame = new QFrame(parent);
    frame->setFrameShape(QFrame::StyledPanel);
    frame->setStyleSheet(QStringLiteral("QFrame#card { border-left: 4px solid %1; }").arg(accent.name()));
    frame->setObjectName(QStringLiteral("card"));
    auto *v = new QVBoxLayout(frame);
    auto *t = new QLabel(title, frame);
    t->setStyleSheet(QStringLiteral("color: %1; font-weight: 600;").arg(accent.name()));
    Card c;
    c.value = new QLabel(QStringLiteral("—"), frame);
    QFont f = c.value->font();
    f.setPointSizeF(f.pointSizeF() * 2.2);
    f.setBold(true);
    c.value->setFont(f);
    c.detail = new QLabel(frame);
    c.detail->setWordWrap(true);
    c.detail->setStyleSheet(QStringLiteral("color: palette(mid);"));
    v->addWidget(t);
    v->addWidget(c.value);
    v->addWidget(c.detail);
    v->addStretch();
    into->addWidget(frame);
    return c;
}

QWidget *MainWindow::buildLiveTab()
{
    auto *w = new QWidget;
    auto *root = new QVBoxLayout(w);

    // Hints (e.g. RAPL permission)
    const QStringList hints = m_pm.hints();
    if (!hints.isEmpty()) {
        auto *hint = new QLabel(hints.join(QStringLiteral("\n\n")), w);
        hint->setWordWrap(true);
        hint->setTextInteractionFlags(Qt::TextSelectableByMouse);
        hint->setStyleSheet(QStringLiteral(
            "background:#fff4ce; color:#5c4400; border:1px solid #e6c300; padding:6px; border-radius:4px;"));
        root->addWidget(hint);
    }

    auto *cards = new QHBoxLayout;
    m_cpuCard = makeCard(tr("CPU"), kCpuColor, w, cards);
    m_gpuCard = makeCard(tr("GPU"), kGpuColor, w, cards);
    m_monCard = makeCard(tr("Monitor"), kMonColor, w, cards);
    m_totalCard = makeCard(tr("Total at the wall"), kTotalColor, w, cards);
    root->addLayout(cards);

    // Graph + controls
    auto *graphBox = new QGroupBox(tr("Live power"), w);
    auto *gv = new QVBoxLayout(graphBox);
    auto *gctl = new QHBoxLayout;
    m_graph = new LiveGraph(graphBox);
    m_graph->setSeries({{tr("CPU"), kCpuColor}, {tr("GPU"), kGpuColor},
                        {tr("Monitor"), kMonColor}, {tr("Total"), kTotalColor}});
    m_graph->setWindowSeconds(m_s.graphWindowSec);
    const QStringList names = {tr("CPU"), tr("GPU"), tr("Monitor"), tr("Total")};
    for (int i = 0; i < names.size(); ++i) {
        auto *cb = new QCheckBox(names[i], graphBox);
        cb->setChecked(true);
        connect(cb, &QCheckBox::toggled, this, [this, i](bool on) { m_graph->setSeriesVisible(i, on); });
        gctl->addWidget(cb);
    }
    gctl->addStretch();
    gctl->addWidget(new QLabel(tr("Window:"), graphBox));
    auto *windowCombo = new QComboBox(graphBox);
    const QVector<QPair<QString, int>> windows = {
        {tr("1 min"), 60}, {tr("5 min"), 300}, {tr("15 min"), 900}, {tr("1 hour"), 3600}};
    for (const auto &p : windows)
        windowCombo->addItem(p.first, p.second);
    windowCombo->setCurrentIndex(qMax(0, windowCombo->findData(m_s.graphWindowSec)));
    connect(windowCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, windowCombo](int) {
                m_s.graphWindowSec = windowCombo->currentData().toInt();
                m_graph->setWindowSeconds(m_s.graphWindowSec);
                m_s.save();
            });
    gctl->addWidget(windowCombo);
    gv->addLayout(gctl);
    gv->addWidget(m_graph, 1);
    root->addWidget(graphBox, 1);

    auto *bottom = new QHBoxLayout;

    // Monitor
    auto *monBox = new QGroupBox(tr("Monitor"), w);
    auto *mf = new QFormLayout(monBox);
    m_monitorCombo = new QComboBox(monBox);
    for (const auto &p : monitorPresets()) {
        if (p.maxW > 0)
            m_monitorCombo->addItem(QStringLiteral("%1  (%2–%3 W)").arg(p.name).arg(p.minW).arg(p.maxW));
        else
            m_monitorCombo->addItem(tr("Custom (set in Settings)"));
    }
    m_monitorCombo->setCurrentIndex(qBound(0, m_s.monitorPreset, m_monitorCombo->count() - 1));
    connect(m_monitorCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
        m_s.monitorPreset = i;
        settingsChanged();
    });
    mf->addRow(tr("Model:"), m_monitorCombo);

    auto *brRow = new QHBoxLayout;
    m_brightness = new QSlider(Qt::Horizontal, monBox);
    m_brightness->setRange(0, 100);
    m_brightness->setValue(m_s.monitorBrightness);
    m_brightnessLabel = new QLabel(monBox);
    m_brightnessLabel->setMinimumWidth(40);
    connect(m_brightness, &QSlider::valueChanged, this, [this](int v) {
        m_s.monitorBrightness = v;
        settingsChanged();
    });
    brRow->addWidget(m_brightness, 1);
    brRow->addWidget(m_brightnessLabel);
    mf->addRow(tr("Brightness:"), brRow);

    m_monitorCount = new QSpinBox(monBox);
    m_monitorCount->setRange(0, 6);
    m_monitorCount->setValue(m_s.monitorCount);
    connect(m_monitorCount, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        m_s.monitorCount = v;
        settingsChanged();
    });
    mf->addRow(tr("Monitors:"), m_monitorCount);
    m_monitorWatts = new QLabel(monBox);
    mf->addRow(tr("Estimated draw:"), m_monitorWatts);
    bottom->addWidget(monBox, 1);

    // Session
    auto *sessBox = new QGroupBox(tr("Current session"), w);
    auto *sf = new QFormLayout(sessBox);
    m_sessStart = new QLabel(sessBox);
    m_sessDuration = new QLabel(sessBox);
    m_sessEnergy = new QLabel(sessBox);
    m_sessAvg = new QLabel(sessBox);
    m_sessCost = new QLabel(sessBox);
    m_sessBreakdown = new QLabel(sessBox);
    m_todayLabel = new QLabel(sessBox);
    sf->addRow(tr("Started:"), m_sessStart);
    sf->addRow(tr("Duration:"), m_sessDuration);
    sf->addRow(tr("Energy:"), m_sessEnergy);
    sf->addRow(tr("Breakdown:"), m_sessBreakdown);
    sf->addRow(tr("Average / peak:"), m_sessAvg);
    sf->addRow(tr("Cost:"), m_sessCost);
    sf->addRow(tr("Today (all sessions):"), m_todayLabel);
    auto *newSess = new QPushButton(tr("Start new session"), sessBox);
    connect(newSess, &QPushButton::clicked, this, [this] {
        finishSession();
        startNewSession();
    });
    sf->addRow(newSess);
    bottom->addWidget(sessBox, 1);

    root->addLayout(bottom);
    updateMonitorLabel();
    return w;
}

QWidget *MainWindow::buildCalendarTab()
{
    auto *w = new QWidget;
    auto *h = new QHBoxLayout(w);

    auto *left = new QVBoxLayout;
    m_calendar = new EnergyCalendar(w);
    left->addWidget(m_calendar, 1);
    m_monthSummary = new QLabel(w);
    m_monthSummary->setWordWrap(true);
    left->addWidget(m_monthSummary);
    h->addLayout(left, 3);

    auto *right = new QVBoxLayout;
    m_dayTitle = new QLabel(w);
    QFont f = m_dayTitle->font();
    f.setPointSizeF(f.pointSizeF() * 1.4);
    f.setBold(true);
    m_dayTitle->setFont(f);
    m_daySummary = new QLabel(w);
    m_daySummary->setWordWrap(true);
    m_daySessions = new QTreeWidget(w);
    m_daySessions->setHeaderLabels({tr("Start"), tr("End"), tr("Duration"), tr("Energy"), tr("Cost")});
    m_daySessions->setRootIsDecorated(false);
    m_daySessions->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    right->addWidget(m_dayTitle);
    right->addWidget(m_daySummary);
    right->addWidget(new QLabel(tr("Sessions on this day:"), w));
    right->addWidget(m_daySessions, 1);
    h->addLayout(right, 2);

    connect(m_calendar, &QCalendarWidget::selectionChanged, this,
            [this] { showDay(m_calendar->selectedDate()); });
    connect(m_calendar, &QCalendarWidget::currentPageChanged, this, [this](int, int) { refreshCalendar(); });
    return w;
}

QWidget *MainWindow::buildLogTab()
{
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    m_logTable = new QTableWidget(w);
    m_logTable->setColumnCount(11);
    m_logTable->setHorizontalHeaderLabels({tr("#"), tr("Start"), tr("End"), tr("Duration"),
                                           tr("Avg W"), tr("Peak W"), tr("CPU Wh"), tr("GPU Wh"),
                                           tr("Monitor Wh"), tr("Total kWh"), tr("Cost")});
    m_logTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_logTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_logTable->setAlternatingRowColors(true);
    m_logTable->verticalHeader()->hide();
    m_logTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_logTable->horizontalHeader()->setStretchLastSection(true);
    v->addWidget(m_logTable, 1);

    auto *h = new QHBoxLayout;
    m_logSummary = new QLabel(w);
    h->addWidget(m_logSummary, 1);
    auto *refresh = new QPushButton(tr("Refresh"), w);
    auto *exportBtn = new QPushButton(tr("Export CSV…"), w);
    auto *del = new QPushButton(tr("Delete selected"), w);
    connect(refresh, &QPushButton::clicked, this, &MainWindow::refreshLog);
    connect(exportBtn, &QPushButton::clicked, this, &MainWindow::exportCsv);
    connect(del, &QPushButton::clicked, this, &MainWindow::deleteSelectedSession);
    h->addWidget(refresh);
    h->addWidget(exportBtn);
    h->addWidget(del);
    v->addLayout(h);
    return w;
}

QWidget *MainWindow::buildSettingsTab()
{
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);

    auto dbl = [this, w](double *target, double min, double max, double step, int decimals,
                         const QString &suffix) {
        auto *sb = new QDoubleSpinBox(w);
        sb->setRange(min, max);
        sb->setSingleStep(step);
        sb->setDecimals(decimals);
        sb->setSuffix(suffix);
        sb->setValue(*target);
        connect(sb, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, target](double x) {
            *target = x;
            settingsChanged();
        });
        return sb;
    };
    auto chk = [this, w](bool *target, const QString &text) {
        auto *cb = new QCheckBox(text, w);
        cb->setChecked(*target);
        connect(cb, &QCheckBox::toggled, this, [this, target](bool on) {
            *target = on;
            settingsChanged();
        });
        return cb;
    };

    auto *meas = new QGroupBox(tr("Measurement"), w);
    auto *mf = new QFormLayout(meas);
    auto *interval = new QSpinBox(meas);
    interval->setRange(250, 10000);
    interval->setSingleStep(250);
    interval->setSuffix(tr(" ms"));
    interval->setValue(m_s.sampleIntervalMs);
    connect(interval, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int ms) {
        m_s.sampleIntervalMs = ms;
        settingsChanged();
    });
    mf->addRow(tr("Sample interval:"), interval);
    mf->addRow(tr("CPU source:"), new QLabel(m_pm.cpuSource(), meas));
    mf->addRow(tr("GPU source:"), new QLabel(m_pm.gpuSource(), meas));
    mf->addRow(tr("CPU TDP (estimate only):"), dbl(&m_s.cpuTdpW, 1, 500, 5, 0, tr(" W")));
    mf->addRow(tr("CPU idle (estimate only):"), dbl(&m_s.cpuIdleW, 0, 200, 1, 0, tr(" W")));
    mf->addRow(tr("GPU fallback (no sensor):"), dbl(&m_s.gpuFallbackW, 0, 600, 5, 0, tr(" W")));
    v->addWidget(meas);

    auto *sys = new QGroupBox(tr("Rest of the system"), w);
    auto *sysf = new QFormLayout(sys);
    sysf->addRow(tr("Other components (board, RAM, disks, fans):"),
                 dbl(&m_s.otherW, 0, 500, 5, 0, tr(" W")));
    sysf->addRow(tr("PSU efficiency:"), dbl(&m_s.psuEfficiency, 50, 100, 1, 0, tr(" %")));
    sysf->addRow(tr("Custom monitor min (0 % brightness):"),
                 dbl(&m_s.customMonitorMinW, 0, 300, 1, 0, tr(" W")));
    sysf->addRow(tr("Custom monitor max (100 % brightness):"),
                 dbl(&m_s.customMonitorMaxW, 0, 300, 1, 0, tr(" W")));
    v->addWidget(sys);

    auto *cost = new QGroupBox(tr("Electricity"), w);
    auto *cf = new QFormLayout(cost);
    cf->addRow(tr("Price per kWh:"), dbl(&m_s.pricePerKWh, 0, 10, 0.01, 3, QString()));
    auto *cur = new QLineEdit(m_s.currency, cost);
    cur->setMaxLength(5);
    connect(cur, &QLineEdit::textChanged, this, [this](const QString &t) {
        m_s.currency = t;
        settingsChanged();
    });
    cf->addRow(tr("Currency:"), cur);
    v->addWidget(cost);

    auto *beh = new QGroupBox(tr("Behaviour"), w);
    auto *bl = new QVBoxLayout(beh);
    bl->addWidget(chk(&m_s.closeToTray, tr("Keep running in the system tray when the window is closed")));
    bl->addWidget(chk(&m_s.startMinimized, tr("Start minimized to tray")));
    bl->addWidget(chk(&m_s.alwaysOnTop, tr("Keep window on top")));
    v->addWidget(beh);

    auto *db = new QLabel(tr("Log database: %1").arg(m_store->path()), w);
    db->setTextInteractionFlags(Qt::TextSelectableByMouse);
    db->setWordWrap(true);
    v->addWidget(db);
    v->addStretch();
    return w;
}

void MainWindow::setupTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;
    m_tray = new QSystemTrayIcon(this);
    auto *menu = new QMenu(this);
    menu->addAction(tr("Show"), this, [this] {
        showNormal();
        raise();
        activateWindow();
    });
    menu->addAction(tr("Start new session"), this, [this] {
        finishSession();
        startNewSession();
    });
    menu->addSeparator();
    menu->addAction(tr("Quit"), this, &MainWindow::quitApp);
    m_tray->setContextMenu(menu);
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r) {
        if (r == QSystemTrayIcon::Trigger || r == QSystemTrayIcon::DoubleClick) {
            if (isVisible() && !isMinimized()) {
                hide();
            } else {
                showNormal();
                raise();
                activateWindow();
            }
        }
    });
    updateTray(0, {}, 0);
    m_tray->show();
    qApp->setQuitOnLastWindowClosed(false);
}

// ---------------------------------------------------------------- measuring

void MainWindow::startNewSession()
{
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    m_session = SessionRecord();
    m_session.startTs = now;
    m_session.endTs = now;
    m_session.id = m_store->beginSession(now);
    m_activeSec = 0.0;
    m_accCpu = m_accGpu = m_accMon = m_accTotal = m_accSec = 0.0;
    m_lastTickMs = 0;
    m_finished = false;
    m_todayDate = QDate::currentDate();
    m_todayFlushedWh = m_store->dailyEnergy(m_todayDate, m_todayDate).value(m_todayDate, 0.0);
    if (m_graph)
        m_graph->clear();
    updateSessionLabels();
}

void MainWindow::finishSession()
{
    if (m_finished)
        return;
    flush();
    m_finished = true;
}

void MainWindow::tick()
{
    if (m_finished)
        return;
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const double dt = m_lastTickMs ? (nowMs - m_lastTickMs) / 1000.0 : 0.0;
    m_lastTickMs = nowMs;

    const PowerMonitor::Sample s = m_pm.sample(m_s.cpuTdpW, m_s.cpuIdleW, m_s.gpuFallbackW);
    const double monW = m_s.monitorWatts();
    const double eff = qBound(50.0, m_s.psuEfficiency, 100.0) / 100.0;
    const double wallW = (s.cpuW + s.gpuW + m_s.otherW) / eff + monW;

    // Skip integration across large gaps (suspend/hibernate, clock jumps).
    const double maxGap = qMax(10.0, 5.0 * m_s.sampleIntervalMs / 1000.0);
    if (dt > 0.0 && dt <= maxGap) {
        m_session.cpuWh += s.cpuW * dt / 3600.0;
        m_session.gpuWh += s.gpuW * dt / 3600.0;
        m_session.monitorWh += monW * dt / 3600.0;
        m_session.totalWh += wallW * dt / 3600.0;
        m_activeSec += dt;
        m_accCpu += s.cpuW * dt;
        m_accGpu += s.gpuW * dt;
        m_accMon += monW * dt;
        m_accTotal += wallW * dt;
        m_accSec += dt;
    }
    m_session.peakW = qMax(m_session.peakW, wallW);
    m_session.endTs = nowMs / 1000;
    m_session.activeSec = qRound64(m_activeSec);

    // Cards
    auto w = [](double v) { return QStringLiteral("%1 W").arg(v, 0, 'f', 1); };
    m_cpuCard.value->setText(w(s.cpuW));
    m_cpuCard.detail->setText(QStringLiteral("%1\nLoad %2 %")
                                  .arg(m_pm.cpuSource())
                                  .arg(s.cpuUtil * 100.0, 0, 'f', 0));
    m_gpuCard.value->setText(w(s.gpuW));
    m_gpuCard.detail->setText(m_pm.gpuSource());
    m_monCard.value->setText(w(monW));
    m_monCard.detail->setText(tr("%1 × %2\n%3 % brightness")
                                  .arg(m_s.monitorCount)
                                  .arg(monitorPresets().value(m_s.monitorPreset).name)
                                  .arg(m_s.monitorBrightness));
    m_totalCard.value->setText(w(wallW));
    m_totalCard.detail->setText(tr("incl. %1 W other, PSU %2 %\n≈ %3 / hour")
                                    .arg(m_s.otherW, 0, 'f', 0)
                                    .arg(m_s.psuEfficiency, 0, 'f', 0)
                                    .arg(fmtCost(wallW)));

    m_graph->addPoint(nowMs, {s.cpuW, s.gpuW, monW, wallW});
    updateSessionLabels();
    updateTray(wallW, s, monW);
}

void MainWindow::flush()
{
    if (m_finished || m_session.id < 0)
        return;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (m_accSec > 0.5) {
        m_store->addSample(m_session.id, now, m_accCpu / m_accSec, m_accGpu / m_accSec,
                           m_accMon / m_accSec, m_accTotal / m_accSec, m_accTotal / 3600.0);
    }
    m_accCpu = m_accGpu = m_accMon = m_accTotal = m_accSec = 0.0;
    m_session.endTs = now;
    m_store->updateSession(m_session);

    const QDate today = QDate::currentDate();
    m_todayDate = today;
    m_todayFlushedWh = m_store->dailyEnergy(today, today).value(today, 0.0);

}

void MainWindow::quitApp()
{
    m_quitting = true;
    finishSession();
    qApp->quit();
}

void MainWindow::settingsChanged()
{
    m_s.save();
    m_sampleTimer.setInterval(m_s.sampleIntervalMs);
    if (bool(windowFlags() & Qt::WindowStaysOnTopHint) != m_s.alwaysOnTop) {
        const bool wasVisible = isVisible();
        setWindowFlag(Qt::WindowStaysOnTopHint, m_s.alwaysOnTop);
        if (wasVisible)
            show();
    }
    updateMonitorLabel();
    updateSessionLabels();
}

void MainWindow::updateMonitorLabel()
{
    if (!m_brightnessLabel)
        return;
    m_brightnessLabel->setText(QStringLiteral("%1 %").arg(m_s.monitorBrightness));
    m_monitorWatts->setText(QStringLiteral("≈ %1 W").arg(m_s.monitorWatts(), 0, 'f', 1));
}

void MainWindow::updateSessionLabels()
{
    if (!m_sessStart)
        return;
    m_sessStart->setText(fmtTime(m_session.startTs, QStringLiteral("ddd d MMM yyyy, HH:mm:ss")));
    m_sessDuration->setText(fmtDuration(m_session.endTs - m_session.startTs));
    m_sessEnergy->setText(fmtEnergy(m_session.totalWh));
    m_sessBreakdown->setText(tr("CPU %1 · GPU %2 · Monitor %3")
                                 .arg(fmtEnergy(m_session.cpuWh), fmtEnergy(m_session.gpuWh),
                                      fmtEnergy(m_session.monitorWh)));
    m_sessAvg->setText(QStringLiteral("%1 W / %2 W")
                           .arg(m_activeSec > 0 ? m_session.totalWh * 3600.0 / m_activeSec : 0.0, 0, 'f', 1)
                           .arg(m_session.peakW, 0, 'f', 1));
    m_sessCost->setText(fmtCost(m_session.totalWh));

    double todayWh = m_todayFlushedWh + m_accTotal / 3600.0;
    if (QDate::currentDate() != m_todayDate)
        todayWh = m_accTotal / 3600.0;
    m_todayLabel->setText(QStringLiteral("%1  (%2)").arg(fmtEnergy(todayWh), fmtCost(todayWh)));
}

void MainWindow::updateTray(double wallW, const PowerMonitor::Sample &s, double monitorW)
{
    if (!m_tray)
        return;
    QPixmap pm(64, 64);
    pm.fill(Qt::transparent);
    {
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        const double t = qBound(0.0, wallW / 600.0, 1.0);
        p.setBrush(QColor::fromHsvF((1.0 - t) * 0.33, 0.8, 0.75));
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(QRectF(2, 2, 60, 60), 12, 12);
        QFont f;
        f.setBold(true);
        f.setPixelSize(wallW >= 1000 ? 22 : 28);
        p.setFont(f);
        p.setPen(Qt::white);
        p.drawText(QRect(0, 0, 64, 64), Qt::AlignCenter,
                   wallW > 0 ? QString::number(qRound(wallW)) : QStringLiteral("W"));
    }
    m_tray->setIcon(QIcon(pm));
    m_tray->setToolTip(tr("Power Meter\nTotal %1 W  (CPU %2 W, GPU %3 W, Monitor %4 W)\nSession %5, %6")
                           .arg(wallW, 0, 'f', 0)
                           .arg(s.cpuW, 0, 'f', 0)
                           .arg(s.gpuW, 0, 'f', 0)
                           .arg(monitorW, 0, 'f', 0)
                           .arg(fmtEnergy(m_session.totalWh), fmtCost(m_session.totalWh)));
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!m_quitting && m_s.closeToTray && m_tray && m_tray->isVisible()) {
        hide();
        event->ignore();
        if (!m_trayMessageShown) {
            m_tray->showMessage(tr("Power Meter"),
                                tr("Still measuring in the background. Right-click the tray icon to quit."),
                                QSystemTrayIcon::Information, 3000);
            m_trayMessageShown = true;
        }
        return;
    }
    finishSession();
    event->accept();
    qApp->quit();
}

// ---------------------------------------------------------------- calendar / log

void MainWindow::refreshCalendar()
{
    if (!m_calendar)
        return;
    flush();   // make sure the latest minute is in the database
    const QDate first(m_calendar->yearShown(), m_calendar->monthShown(), 1);
    const QDate last = first.addMonths(1).addDays(-1);
    // Include the adjacent weeks that are visible in the grid.
    const QMap<QDate, double> data = m_store->dailyEnergy(first.addDays(-7), last.addDays(14));
    m_calendar->setEnergy(data);

    double monthWh = 0.0;
    int days = 0;
    for (auto it = data.constBegin(); it != data.constEnd(); ++it) {
        if (it.key() >= first && it.key() <= last) {
            monthWh += it.value();
            ++days;
        }
    }
    m_monthSummary->setText(tr("<b>%1:</b> %2 over %3 day(s) — %4, avg %5 per active day")
                                .arg(QLocale().toString(first, QStringLiteral("MMMM yyyy")))
                                .arg(fmtEnergy(monthWh))
                                .arg(days)
                                .arg(fmtCost(monthWh))
                                .arg(fmtEnergy(days ? monthWh / days : 0.0)));
    showDay(m_calendar->selectedDate());
}

void MainWindow::showDay(const QDate &day)
{
    m_dayTitle->setText(QLocale().toString(day, QLocale::LongFormat));
    const double wh = m_store->dailyEnergy(day, day).value(day, 0.0);
    const qint64 from = dayStart(day), to = dayStart(day.addDays(1));
    const auto list = m_store->sessions(from, to);

    qint64 activeSec = 0;
    m_daySessions->clear();
    for (const SessionRecord &r : list) {
        // Only the part of the session that lies on this day counts towards its time.
        activeSec += qMax<qint64>(0, qMin(r.endTs, to) - qMax(r.startTs, from));
        auto *item = new QTreeWidgetItem(m_daySessions);
        const bool sameDay = QDateTime::fromSecsSinceEpoch(r.startTs).date() == day;
        item->setText(0, fmtTime(r.startTs, sameDay ? QStringLiteral("HH:mm") : QStringLiteral("d MMM HH:mm")));
        item->setText(1, r.id == m_session.id && !m_finished ? tr("running") : fmtTime(r.endTs, QStringLiteral("HH:mm")));
        item->setText(2, fmtDuration(r.endTs - r.startTs));
        item->setText(3, fmtEnergy(r.totalWh));
        item->setText(4, fmtCost(r.totalWh));
    }
    m_daySummary->setText(tr("Energy: <b>%1</b><br>Cost: <b>%2</b><br>Computer on: %3 in %4 session(s)")
                              .arg(fmtEnergy(wh), fmtCost(wh), fmtDuration(activeSec))
                              .arg(list.size()));
}

void MainWindow::refreshLog()
{
    flush();
    const auto list = m_store->sessions();
    m_logTable->setRowCount(list.size());
    double totalWh = 0.0;
    for (int row = 0; row < list.size(); ++row) {
        const SessionRecord &r = list[row];
        totalWh += r.totalWh;
        const QStringList cells = {
            QString::number(r.id),
            fmtTime(r.startTs),
            (r.id == m_session.id && !m_finished) ? tr("running") : fmtTime(r.endTs),
            fmtDuration(r.endTs - r.startTs),
            QString::number(r.avgW(), 'f', 1),
            QString::number(r.peakW, 'f', 1),
            QString::number(r.cpuWh, 'f', 1),
            QString::number(r.gpuWh, 'f', 1),
            QString::number(r.monitorWh, 'f', 1),
            QString::number(r.totalWh / 1000.0, 'f', 3),
            fmtCost(r.totalWh),
        };
        for (int c = 0; c < cells.size(); ++c) {
            auto *item = new QTableWidgetItem(cells[c]);
            if (c == 0)
                item->setData(Qt::UserRole, r.id);
            if (c >= 3)
                item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_logTable->setItem(row, c, item);
        }
    }
    m_logSummary->setText(tr("%1 session(s), %2 total, %3")
                              .arg(list.size())
                              .arg(fmtEnergy(totalWh), fmtCost(totalWh)));
}

void MainWindow::exportCsv()
{
    const QString file = QFileDialog::getSaveFileName(
        this, tr("Export sessions"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/power-sessions.csv"),
        tr("CSV files (*.csv)"));
    if (file.isEmpty())
        return;
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Export failed"), f.errorString());
        return;
    }
    flush();
    QTextStream out(&f);
    out << "id,start,end,active_seconds,avg_w,peak_w,cpu_wh,gpu_wh,monitor_wh,total_wh,cost\n";
    for (const SessionRecord &r : m_store->sessions()) {
        out << r.id << ','
            << QDateTime::fromSecsSinceEpoch(r.startTs).toString(Qt::ISODate) << ','
            << QDateTime::fromSecsSinceEpoch(r.endTs).toString(Qt::ISODate) << ','
            << r.activeSec << ','
            << QString::number(r.avgW(), 'f', 2) << ','
            << QString::number(r.peakW, 'f', 2) << ','
            << QString::number(r.cpuWh, 'f', 3) << ','
            << QString::number(r.gpuWh, 'f', 3) << ','
            << QString::number(r.monitorWh, 'f', 3) << ','
            << QString::number(r.totalWh, 'f', 3) << ','
            << QString::number(r.totalWh / 1000.0 * m_s.pricePerKWh, 'f', 4) << '\n';
    }
}

void MainWindow::deleteSelectedSession()
{
    const int row = m_logTable->currentRow();
    if (row < 0)
        return;
    const qint64 id = m_logTable->item(row, 0)->data(Qt::UserRole).toLongLong();
    if (id == m_session.id && !m_finished) {
        QMessageBox::information(this, tr("Delete session"),
                                 tr("The running session cannot be deleted. Start a new session first."));
        return;
    }
    if (QMessageBox::question(this, tr("Delete session"),
                              tr("Delete session #%1 and its samples?").arg(id)) != QMessageBox::Yes)
        return;
    m_store->deleteSession(id);
    refreshLog();
}

// ---------------------------------------------------------------- formatting

QString MainWindow::fmtEnergy(double wh) const
{
    if (wh >= 1000.0)
        return QStringLiteral("%1 kWh").arg(wh / 1000.0, 0, 'f', 3);
    return QStringLiteral("%1 Wh").arg(wh, 0, 'f', wh < 10 ? 2 : 1);
}

QString MainWindow::fmtCost(double wh) const
{
    const double cost = wh / 1000.0 * m_s.pricePerKWh;
    return QStringLiteral("%1 %2").arg(cost, 0, 'f', cost < 1.0 ? 4 : 2).arg(m_s.currency);
}

QString MainWindow::fmtDuration(qint64 sec)
{
    sec = qMax<qint64>(0, sec);
    const qint64 h = sec / 3600, m = (sec % 3600) / 60, s = sec % 60;
    if (h > 0)
        return QStringLiteral("%1 h %2 min").arg(h).arg(m, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1 min %2 s").arg(m).arg(s, 2, 10, QLatin1Char('0'));
}
