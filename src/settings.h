#pragma once

#include <QString>
#include <QVector>

struct MonitorPreset {
    QString name;
    double minW;   // at 0 % brightness
    double maxW;   // at 100 % brightness
};

// Built-in monitor models. The last entry is "Custom" and uses the
// user-defined min/max values from the settings.
const QVector<MonitorPreset> &monitorPresets();

struct AppSettings {
    int sampleIntervalMs = 1000;
    double pricePerKWh = 0.20;
    QString currency = QStringLiteral("€");

    // CPU estimate (used only when no hardware sensor is readable)
    double cpuTdpW = 65.0;
    double cpuIdleW = 8.0;
    // GPU fallback when no sensor is found
    double gpuFallbackW = 0.0;

    // Rest of the machine (motherboard, RAM, disks, fans) and PSU losses
    double otherW = 30.0;
    double psuEfficiency = 88.0;   // percent

    // Monitor
    int monitorPreset = 2;
    int monitorBrightness = 70;    // percent
    int monitorCount = 1;
    double customMonitorMinW = 15.0;
    double customMonitorMaxW = 40.0;

    // Behaviour
    bool closeToTray = true;
    bool startMinimized = false;
    bool alwaysOnTop = false;
    int graphWindowSec = 300;

    double monitorWatts() const;

    void load();
    void save() const;
};
