#include "ocr/OcrScheduler.h"

#include <QDir>
#include <QFile>
#include <QThread>

#include <cstdlib>

namespace purrfind {
namespace {
QString readTrimmed(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()).trimmed() : QString();
}
}

PowerState OcrScheduler::powerState()
{
    PowerState state;
    const QDir supplies("/sys/class/power_supply");
    for (const auto &name : supplies.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString base = supplies.absoluteFilePath(name);
        if (readTrimmed(base + "/type") != "Battery") continue;
        state.batteryPresent = true;
        bool ok = false;
        const int percent = readTrimmed(base + "/capacity").toInt(&ok);
        if (ok) state.percent = state.percent < 0 ? percent : qMin(state.percent, percent);
        const QString status = readTrimmed(base + "/status").toLower();
        if (status == "discharging" || status == "not charging") state.onBattery = true;
    }
    return state;
}

bool OcrScheduler::mayRun(const ConfigData &config, QString *reason)
{
    if (config.ocrPaused) { if (reason) *reason = "paused"; return false; }
    const auto power = powerState();
    if (config.ocrReduceOnBattery && config.ocrPauseBelowThirtyPercent
        && power.onBattery && power.percent >= 0 && power.percent < 30) {
        if (reason) *reason = "battery below 30%";
        return false;
    }
    double load = 0.0;
    const int processors = qMax(1, QThread::idealThreadCount());
    if (::getloadavg(&load, 1) == 1 && load > processors * 1.25) {
        if (reason) *reason = "high system load";
        return false;
    }
    return true;
}

QString OcrScheduler::effectiveProfile(const ConfigData &config)
{
    if (config.ocrReduceOnBattery && powerState().onBattery) return "low";
    return config.ocrResourceProfile;
}

int OcrScheduler::threadLimit(const QString &profile)
{
    if (profile == "high") return qBound(2, QThread::idealThreadCount(), 4);
    if (profile == "normal") return qBound(1, QThread::idealThreadCount(), 2);
    return 1;
}

} // namespace purrfind
