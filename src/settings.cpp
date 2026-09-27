#include "settings.h"

#include <QSettings>
#include <QtGlobal>

const QVector<MonitorPreset> &monitorPresets()
{
    // Rough typical draw figures from spec sheets / measurements.
    static const QVector<MonitorPreset> presets = {
        {QStringLiteral("Laptop panel 14–16\""),         2.0,  8.0},
        {QStringLiteral("21.5\" 1080p LED"),             10.0, 20.0},
        {QStringLiteral("24\" 1080p IPS"),               12.0, 25.0},
        {QStringLiteral("24\" 1080p 144 Hz gaming"),     15.0, 30.0},
        {QStringLiteral("27\" 1440p IPS"),               16.0, 35.0},
        {QStringLiteral("27\" 1440p 240 Hz gaming"),     20.0, 45.0},
        {QStringLiteral("27\" 4K IPS"),                  20.0, 45.0},
        {QStringLiteral("27\" QD-OLED / WOLED"),         25.0, 90.0},
        {QStringLiteral("32\" 4K VA / IPS"),             25.0, 60.0},
        {QStringLiteral("34\" ultrawide 1440p"),         25.0, 55.0},
        {QStringLiteral("49\" super-ultrawide"),         40.0, 100.0},
        {QStringLiteral("Custom"),                       0.0,  0.0},
    };
    return presets;
}

double AppSettings::monitorWatts() const
{
    const auto &presets = monitorPresets();
    const int idx = qBound(0, monitorPreset, presets.size() - 1);
    double minW = presets[idx].minW;
    double maxW = presets[idx].maxW;
    if (idx == presets.size() - 1) {
        minW = customMonitorMinW;
        maxW = customMonitorMaxW;
    }
    const double b = qBound(0, monitorBrightness, 100) / 100.0;
    return monitorCount * (minW + (maxW - minW) * b);
}

void AppSettings::load()
{
    QSettings s;
    AppSettings d;
    sampleIntervalMs = s.value("sampleIntervalMs", d.sampleIntervalMs).toInt();
    pricePerKWh = s.value("pricePerKWh", d.pricePerKWh).toDouble();
    currency = s.value("currency", d.currency).toString();
    cpuTdpW = s.value("cpuTdpW", d.cpuTdpW).toDouble();
    cpuIdleW = s.value("cpuIdleW", d.cpuIdleW).toDouble();
    gpuFallbackW = s.value("gpuFallbackW", d.gpuFallbackW).toDouble();
    otherW = s.value("otherW", d.otherW).toDouble();
    psuEfficiency = s.value("psuEfficiency", d.psuEfficiency).toDouble();
    monitorPreset = s.value("monitorPreset", d.monitorPreset).toInt();
    monitorBrightness = s.value("monitorBrightness", d.monitorBrightness).toInt();
    monitorCount = s.value("monitorCount", d.monitorCount).toInt();
    customMonitorMinW = s.value("customMonitorMinW", d.customMonitorMinW).toDouble();
    customMonitorMaxW = s.value("customMonitorMaxW", d.customMonitorMaxW).toDouble();
    closeToTray = s.value("closeToTray", d.closeToTray).toBool();
    startMinimized = s.value("startMinimized", d.startMinimized).toBool();
    alwaysOnTop = s.value("alwaysOnTop", d.alwaysOnTop).toBool();
    graphWindowSec = s.value("graphWindowSec", d.graphWindowSec).toInt();
}

void AppSettings::save() const
{
    QSettings s;
    s.setValue("sampleIntervalMs", sampleIntervalMs);
    s.setValue("pricePerKWh", pricePerKWh);
    s.setValue("currency", currency);
    s.setValue("cpuTdpW", cpuTdpW);
    s.setValue("cpuIdleW", cpuIdleW);
    s.setValue("gpuFallbackW", gpuFallbackW);
    s.setValue("otherW", otherW);
    s.setValue("psuEfficiency", psuEfficiency);
    s.setValue("monitorPreset", monitorPreset);
    s.setValue("monitorBrightness", monitorBrightness);
    s.setValue("monitorCount", monitorCount);
    s.setValue("customMonitorMinW", customMonitorMinW);
    s.setValue("customMonitorMaxW", customMonitorMaxW);
    s.setValue("closeToTray", closeToTray);
    s.setValue("startMinimized", startMinimized);
    s.setValue("alwaysOnTop", alwaysOnTop);
    s.setValue("graphWindowSec", graphWindowSec);
}
