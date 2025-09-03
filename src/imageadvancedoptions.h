#pragma once

#include <QObject>

namespace ImageOptions {
Q_NAMESPACE

enum AdvancedOption {
    NoAdvancedOptions   = 0x0,
    IsRpiosCloudInit    = 0x1,
    EnableSsh           = 0x2,
    UserDefinedFirstRun = 0x3,
};
Q_ENUM_NS(AdvancedOption)

Q_DECLARE_FLAGS(AdvancedOptions, AdvancedOption)
Q_FLAG_NS(AdvancedOptions)
}

Q_DECLARE_OPERATORS_FOR_FLAGS(ImageOptions::AdvancedOptions)
