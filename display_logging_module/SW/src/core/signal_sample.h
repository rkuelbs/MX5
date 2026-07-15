#pragma once

#include <QMetaType>
#include <QString>
#include <QVariant>

namespace miata::data {

enum class SignalSource {
    Can,
    Vn300,
    Replay,
    Derived,
};

struct SignalSample {
    QString canonicalName;
    QVariant value;
    QString unit;
    qint64 monotonicTimestampNs = 0;
    SignalSource source = SignalSource::Can;
};

}  // namespace miata::data

Q_DECLARE_METATYPE(miata::data::SignalSample)
