#include "powermonitor.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

namespace {

bool readNumber(const QString &path, double &out)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    bool ok = false;
    out = QString::fromLatin1(f.readAll()).trimmed().toDouble(&ok);
    return ok;
}

QString readText(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QString::fromLatin1(f.readAll()).trimmed();
}

bool isReadable(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly);
}

// Anything above this is treated as a glitch (counter reset after suspend etc.)
constexpr double kMaxPlausibleW = 3000.0;

} // namespace

bool PowerMonitor::EnergyCounter::read(double &watts)
{
    double raw = 0.0;
    if (!readNumber(path, raw))
        return false;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    bool ok = false;
    if (lastRaw >= 0.0 && now > lastMs) {
        double delta = raw - lastRaw;
        if (delta < 0.0 && wrapRaw > 0.0)
            delta += wrapRaw;
        const double dt = (now - lastMs) / 1000.0;
        const double w = delta * toJoules / dt;
        if (delta >= 0.0 && w < kMaxPlausibleW) {
            lastW = w;
            ok = true;
        }
    }
    lastRaw = raw;
    lastMs = now;
    watts = lastW;
    return ok;
}

PowerMonitor::PowerMonitor()
{
    detectCpu();
    detectGpu();
    readCpuUtil();   // prime the utilisation delta
}

PowerMonitor::~PowerMonitor()
{
    if (m_nvmlOk && m_nvmlShutdown)
        m_nvmlShutdown();
}

void PowerMonitor::detectCpu()
{
#ifdef Q_OS_LINUX
    // --- RAPL -------------------------------------------------------------
    QDir powercap(QStringLiteral("/sys/class/powercap"));
    bool permissionProblem = false;
    const QStringList zones = powercap.entryList(QStringList() << QStringLiteral("intel-rapl:*"),
                                                 QDir::Dirs | QDir::System | QDir::NoDotAndDotDot);
    for (const QString &zone : zones) {
        const QString base = powercap.absoluteFilePath(zone);
        const QString name = readText(base + QStringLiteral("/name"));
        const bool topLevel = zone.count(QLatin1Char(':')) == 1;
        const bool isPackage = topLevel && name.startsWith(QStringLiteral("package"));
        const bool isUncore = !topLevel && name == QStringLiteral("uncore");
        if (!isPackage && !isUncore)
            continue;

        EnergyCounter c;
        c.path = base + QStringLiteral("/energy_uj");
        readNumber(base + QStringLiteral("/max_energy_range_uj"), c.wrapRaw);
        if (!isReadable(c.path)) {
            permissionProblem = true;
            continue;
        }
        (isPackage ? m_raplPackages : m_raplUncore).append(c);
    }
    if (!m_raplPackages.isEmpty()) {
        m_cpuSource = QStringLiteral("RAPL (%1 package%2)")
                          .arg(m_raplPackages.size())
                          .arg(m_raplPackages.size() > 1 ? "s" : "");
        for (auto &c : m_raplPackages) { double w; c.read(w); }
        for (auto &c : m_raplUncore) { double w; c.read(w); }
        return;
    }
    if (permissionProblem) {
        m_hints << QStringLiteral(
            "CPU energy counters exist but are not readable. Run once:\n"
            "  sudo chmod o+r /sys/class/powercap/intel-rapl:*/energy_uj "
            "/sys/class/powercap/intel-rapl:*/*/energy_uj\n"
            "(or add a udev rule) to get real CPU watts instead of an estimate.");
    }

    // --- hwmon drivers that report CPU power directly (zenpower) ----------
    QDir hwmon(QStringLiteral("/sys/class/hwmon"));
    for (const QString &h : hwmon.entryList(QDir::Dirs | QDir::System | QDir::NoDotAndDotDot)) {
        const QString base = hwmon.absoluteFilePath(h);
        const QString name = readText(base + QStringLiteral("/name"));
        if (name != QStringLiteral("zenpower"))
            continue;
        for (const QString f : {QStringLiteral("/power1_input"), QStringLiteral("/power2_input")}) {
            if (isReadable(base + f))
                m_cpuPowerFiles.append({base + f, 1e-6});
        }
    }
    if (!m_cpuPowerFiles.isEmpty()) {
        m_cpuSource = QStringLiteral("zenpower hwmon");
        return;
    }
#endif
    m_cpuSource = QStringLiteral("Estimate (utilisation × TDP)");
}

bool PowerMonitor::initNvml()
{
#ifdef Q_OS_WIN
    m_nvmlLib.setFileName(QStringLiteral("nvml"));
    if (!m_nvmlLib.load()) {
        m_nvmlLib.setFileName(QStringLiteral("C:/Program Files/NVIDIA Corporation/NVSMI/nvml.dll"));
        if (!m_nvmlLib.load())
            return false;
    }
#else
    m_nvmlLib.setFileNameAndVersion(QStringLiteral("nvidia-ml"), 1);
    if (!m_nvmlLib.load())
        return false;
#endif
    using InitFn = int (*)();
    using CountFn = int (*)(unsigned int *);
    using HandleFn = int (*)(unsigned int, void **);
    using NameFn = int (*)(void *, char *, unsigned int);

    auto init = reinterpret_cast<InitFn>(m_nvmlLib.resolve("nvmlInit_v2"));
    auto count = reinterpret_cast<CountFn>(m_nvmlLib.resolve("nvmlDeviceGetCount_v2"));
    auto handle = reinterpret_cast<HandleFn>(m_nvmlLib.resolve("nvmlDeviceGetHandleByIndex_v2"));
    auto name = reinterpret_cast<NameFn>(m_nvmlLib.resolve("nvmlDeviceGetName"));
    m_nvmlGetPower = reinterpret_cast<int (*)(void *, unsigned int *)>(
        m_nvmlLib.resolve("nvmlDeviceGetPowerUsage"));
    m_nvmlShutdown = reinterpret_cast<int (*)()>(m_nvmlLib.resolve("nvmlShutdown"));
    if (!init || !count || !handle || !m_nvmlGetPower)
        return false;
    if (init() != 0)
        return false;
    m_nvmlOk = true;

    unsigned int n = 0;
    if (count(&n) != 0 || n == 0)
        return false;
    QStringList names;
    for (unsigned int i = 0; i < n; ++i) {
        void *dev = nullptr;
        unsigned int mw = 0;
        if (handle(i, &dev) != 0 || m_nvmlGetPower(dev, &mw) != 0)
            continue;   // device without power readout
        m_nvmlDevices.append(dev);
        char buf[96] = {};
        if (name && name(dev, buf, sizeof(buf)) == 0)
            names << QString::fromLatin1(buf);
    }
    if (m_nvmlDevices.isEmpty())
        return false;
    m_gpuSource = QStringLiteral("NVML: ") + (names.isEmpty() ? QStringLiteral("NVIDIA GPU")
                                                              : names.join(QStringLiteral(", ")));
    return true;
}

void PowerMonitor::detectGpu()
{
    if (initNvml())
        return;

#ifdef Q_OS_LINUX
    // --- DRM hwmon sensors (amdgpu, i915, xe) -----------------------------
    QDir drm(QStringLiteral("/sys/class/drm"));
    const QRegularExpression cardRe(QStringLiteral("^card\\d+$"));
    QStringList drivers;
    for (const QString &card : drm.entryList(QDir::Dirs | QDir::System | QDir::NoDotAndDotDot)) {
        if (!cardRe.match(card).hasMatch())
            continue;
        QDir hw(drm.absoluteFilePath(card) + QStringLiteral("/device/hwmon"));
        for (const QString &h : hw.entryList(QDir::Dirs | QDir::System | QDir::NoDotAndDotDot)) {
            const QString base = hw.absoluteFilePath(h);
            const QString drv = readText(base + QStringLiteral("/name"));
            bool found = false;
            for (const QString f : {QStringLiteral("/power1_average"), QStringLiteral("/power1_input")}) {
                double v;
                if (readNumber(base + f, v)) {
                    m_gpuPowerFiles.append({base + f, 1e-6});
                    found = true;
                    break;
                }
            }
            if (!found && isReadable(base + QStringLiteral("/energy1_input"))) {
                EnergyCounter c;
                c.path = base + QStringLiteral("/energy1_input");
                double w;
                c.read(w);
                m_gpuEnergy.append(c);
                found = true;
            }
            if (found)
                drivers << drv;
        }
    }
    if (!drivers.isEmpty()) {
        m_gpuSource = QStringLiteral("hwmon: ") + drivers.join(QStringLiteral(", "));
        return;
    }

    // --- Intel iGPU via RAPL uncore ----------------------------------------
    if (!m_raplUncore.isEmpty()) {
        m_useUncoreAsGpu = true;
        m_gpuSource = QStringLiteral("RAPL uncore (integrated GPU)");
        return;
    }
#endif
    m_gpuSource = QStringLiteral("No sensor – fixed fallback value");
}

double PowerMonitor::readCpuUtil()
{
    unsigned long long idle = 0, total = 0;
#if defined(Q_OS_LINUX)
    QFile f(QStringLiteral("/proc/stat"));
    if (!f.open(QIODevice::ReadOnly))
        return m_lastUtil;
    const QString line = QString::fromLatin1(f.readLine());
    const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parts.size() < 8 || parts[0] != QStringLiteral("cpu"))
        return m_lastUtil;
    // user nice system idle iowait irq softirq steal
    for (int i = 1; i <= 8 && i < parts.size(); ++i)
        total += parts[i].toULongLong();
    idle = parts[4].toULongLong() + parts[5].toULongLong();
#elif defined(Q_OS_WIN)
    FILETIME fIdle, fKernel, fUser;
    if (!GetSystemTimes(&fIdle, &fKernel, &fUser))
        return m_lastUtil;
    auto toU = [](const FILETIME &ft) {
        return (static_cast<unsigned long long>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    };
    idle = toU(fIdle);
    total = toU(fKernel) + toU(fUser);   // kernel time includes idle time
#else
    return 0.0;
#endif
    const unsigned long long dTotal = total - m_prevTotal;
    const unsigned long long dIdle = idle - m_prevIdle;
    if (m_prevTotal != 0 && dTotal > 0 && total >= m_prevTotal)
        m_lastUtil = qBound(0.0, 1.0 - double(dIdle) / double(dTotal), 1.0);
    m_prevTotal = total;
    m_prevIdle = idle;
    return m_lastUtil;
}

PowerMonitor::Sample PowerMonitor::sample(double cpuTdpW, double cpuIdleW, double gpuFallbackW)
{
    Sample s;
    s.cpuUtil = readCpuUtil();

    // ---- CPU ----
    double uncoreW = 0.0;
    for (auto &c : m_raplUncore) {
        double w = 0.0;
        c.read(w);
        uncoreW += w;
    }
    if (!m_raplPackages.isEmpty()) {
        double sum = 0.0;
        for (auto &c : m_raplPackages) {
            double w = 0.0;
            c.read(w);
            sum += w;
        }
        // The package domain includes the iGPU; attribute that part to the GPU.
        if (m_useUncoreAsGpu)
            sum = qMax(0.0, sum - uncoreW);
        s.cpuW = sum;
        s.cpuMeasured = true;
    } else if (!m_cpuPowerFiles.isEmpty()) {
        double sum = 0.0;
        for (const auto &p : m_cpuPowerFiles) {
            double v = 0.0;
            if (readNumber(p.path, v))
                sum += v * p.toWatts;
        }
        s.cpuW = sum;
        s.cpuMeasured = true;
    } else {
        s.cpuW = cpuIdleW + (qMax(cpuTdpW, cpuIdleW) - cpuIdleW) * s.cpuUtil;
    }

    // ---- GPU ----
    if (m_nvmlOk && !m_nvmlDevices.isEmpty()) {
        double sum = 0.0;
        for (void *dev : m_nvmlDevices) {
            unsigned int mw = 0;
            if (m_nvmlGetPower(dev, &mw) == 0)
                sum += mw / 1000.0;
        }
        s.gpuW = sum;
        s.gpuMeasured = true;
    } else if (!m_gpuPowerFiles.isEmpty() || !m_gpuEnergy.isEmpty()) {
        double sum = 0.0;
        for (const auto &p : m_gpuPowerFiles) {
            double v = 0.0;
            if (readNumber(p.path, v))
                sum += v * p.toWatts;
        }
        for (auto &c : m_gpuEnergy) {
            double w = 0.0;
            c.read(w);
            sum += w;
        }
        s.gpuW = sum;
        s.gpuMeasured = true;
    } else if (m_useUncoreAsGpu) {
        s.gpuW = uncoreW;
        s.gpuMeasured = true;
    } else {
        s.gpuW = gpuFallbackW;
    }
    return s;
}
