#pragma once

#include <QLibrary>
#include <QString>
#include <QStringList>
#include <QVector>

// Reads CPU and GPU power draw from whatever the platform exposes:
//
//  CPU: Linux RAPL (/sys/class/powercap/intel-rapl:*, works on Intel and
//       recent AMD), zenpower hwmon, otherwise an estimate from CPU
//       utilisation and the configured TDP.
//  GPU: NVIDIA via NVML (loaded at runtime, no SDK needed), AMD via the
//       amdgpu hwmon power sensor, Intel Arc/Xe via the i915/xe hwmon
//       energy counter, Intel iGPU via RAPL "uncore", otherwise a fixed
//       fallback value.
class PowerMonitor
{
public:
    struct Sample {
        double cpuW = 0.0;
        double gpuW = 0.0;
        double cpuUtil = 0.0;      // 0..1
        bool cpuMeasured = false;  // true = hardware sensor, false = estimate
        bool gpuMeasured = false;
    };

    PowerMonitor();
    ~PowerMonitor();

    Sample sample(double cpuTdpW, double cpuIdleW, double gpuFallbackW);

    QString cpuSource() const { return m_cpuSource; }
    QString gpuSource() const { return m_gpuSource; }
    // Human readable hints, e.g. how to grant permission to RAPL counters.
    QStringList hints() const { return m_hints; }

private:
    // A monotonically increasing energy counter that may wrap around.
    struct EnergyCounter {
        QString path;
        double toJoules = 1e-6;      // counters are in micro-joules
        double wrapRaw = 0.0;        // raw value at which the counter wraps
        double lastRaw = -1.0;
        qint64 lastMs = 0;
        double lastW = 0.0;
        bool read(double &watts);    // returns false if no valid delta yet
    };

    // An instantaneous power sensor (hwmon power*_input / power*_average).
    struct PowerFile {
        QString path;
        double toWatts = 1e-6;       // hwmon reports micro-watts
    };

    void detectCpu();
    void detectGpu();
    bool initNvml();
    double readCpuUtil();

    // CPU
    QVector<EnergyCounter> m_raplPackages;
    QVector<EnergyCounter> m_raplUncore;   // Intel integrated graphics
    QVector<PowerFile> m_cpuPowerFiles;    // zenpower etc.
    double m_lastUtil = 0.0;
    unsigned long long m_prevIdle = 0, m_prevTotal = 0;

    // GPU
    QVector<PowerFile> m_gpuPowerFiles;
    QVector<EnergyCounter> m_gpuEnergy;
    bool m_useUncoreAsGpu = false;

    // NVML (dynamically loaded)
    QLibrary m_nvmlLib;
    bool m_nvmlOk = false;
    QVector<void *> m_nvmlDevices;
    int (*m_nvmlGetPower)(void *, unsigned int *) = nullptr;
    int (*m_nvmlShutdown)() = nullptr;

    QString m_cpuSource;
    QString m_gpuSource;
    QStringList m_hints;
};
