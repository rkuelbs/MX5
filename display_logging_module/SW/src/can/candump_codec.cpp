#include "can/candump_codec.h"

#include <QRegularExpression>

namespace miata::data {
namespace {

const QRegularExpression kCandumpLine(
    QStringLiteral(R"(^\s*\((\d+)(?:\.(\d{1,9}))?\)\s+(\S+)\s+([0-9A-Fa-f]+)#([0-9A-Fa-f]*)\s*$)"));

QString paddedNanoseconds(QString fractional) {
    fractional = fractional.leftJustified(9, QLatin1Char('0'), true);
    return fractional;
}

}  // namespace

std::optional<CanFrameRecord> CandumpCodec::parseLine(
    QStringView line,
    QString* errorMessage,
    bool* ignored) {
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (ignored != nullptr) {
        *ignored = false;
    }

    const QString text = line.toString().trimmed();
    if (text.isEmpty() || text.startsWith(QLatin1Char(';'))) {
        if (ignored != nullptr) {
            *ignored = true;
        }
        return std::nullopt;
    }

    const QRegularExpressionMatch match = kCandumpLine.match(text);
    if (!match.hasMatch()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("invalid candump line");
        }
        return std::nullopt;
    }

    bool secondsOk = false;
    bool nanosOk = false;
    bool idOk = false;
    const qint64 seconds = match.captured(1).toLongLong(&secondsOk);
    const QString nanosText = paddedNanoseconds(match.captured(2));
    const qint64 nanoseconds = nanosText.isEmpty() ? 0 : nanosText.toLongLong(&nanosOk);
    const quint32 frameId = match.captured(4).toUInt(&idOk, 16);
    const QByteArray payload = QByteArray::fromHex(match.captured(5).toLatin1());

    if (match.captured(2).isEmpty()) {
        nanosOk = true;
    }
    if (!secondsOk || !nanosOk || !idOk || frameId > 0x1FFFFFFFU) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("timestamp or CAN identifier is out of range");
        }
        return std::nullopt;
    }
    if ((match.captured(5).size() % 2) != 0 || payload.size() > 8) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("classic CAN payload must contain 0 to 8 complete bytes");
        }
        return std::nullopt;
    }

    QCanBusFrame frame(frameId, payload);
    frame.setExtendedFrameFormat(frameId > 0x7FFU);
    return CanFrameRecord{
        frame,
        seconds * 1'000'000'000LL + nanoseconds,
        match.captured(3),
    };
}

QString CandumpCodec::formatLine(const CanFrameRecord& record) {
    const qint64 seconds = record.monotonicTimestampNs / 1'000'000'000LL;
    const qint64 nanoseconds = record.monotonicTimestampNs % 1'000'000'000LL;
    const int idWidth = record.frame.hasExtendedFrameFormat() ? 8 : 3;
    return QStringLiteral("(%1.%2) %3 %4#%5")
        .arg(seconds)
        .arg(nanoseconds, 9, 10, QLatin1Char('0'))
        .arg(record.interfaceName.isEmpty() ? QStringLiteral("can0") : record.interfaceName)
        .arg(record.frame.frameId(), idWidth, 16, QLatin1Char('0'))
        .arg(QString::fromLatin1(record.frame.payload().toHex().toUpper()));
}

}  // namespace miata::data
