#pragma once

#include <QObject>

namespace ImageOptions {
Q_NAMESPACE

// One bit each, and no two sharing one.
//
// UserDefinedFirstRun was 0x3, which is IsRpiosCloudInit and EnableSsh
// together: setting it set both of those as well. The command line asks for
// it whenever it is given a first-run script, so a run with --first-run-script
// already reads as SSH enabled and as a cloud-init image. Nothing tests those
// two today, which is the only reason it has not shown -- the first caller to
// ask would get the wrong answer with nothing to explain it.
enum AdvancedOption {
    NoAdvancedOptions   = 0x0,
    IsRpiosCloudInit    = 0x1,
    EnableSsh           = 0x2,
    EnableSecureBoot    = 0x4,
    UserDefinedFirstRun = 0x8,
};
Q_ENUM_NS(AdvancedOption)

Q_DECLARE_FLAGS(AdvancedOptions, AdvancedOption)
Q_FLAG_NS(AdvancedOptions)
}

Q_DECLARE_OPERATORS_FOR_FLAGS(ImageOptions::AdvancedOptions)
