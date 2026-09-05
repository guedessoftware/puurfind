#pragma once

#include "core/Config.h"

namespace purrfind {

struct PowerState {
    bool batteryPresent{false};
    bool onBattery{false};
    int percent{-1};
};

class OcrScheduler {
public:
    static PowerState powerState();
    static bool mayRun(const ConfigData &config, QString *reason = nullptr);
    static QString effectiveProfile(const ConfigData &config);
    static int threadLimit(const QString &profile);
};

} // namespace purrfind
