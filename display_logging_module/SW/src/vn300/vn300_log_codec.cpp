#include "vn300/vn300_log_codec.h"

#include <QRegularExpression>

namespace miata::data {
namespace {

const QRegularExpression kLine(
    QStringLiteral(R"(^\s*\((\d+)(?:\.(\d{1,9}))?\)\s+([0-9A-Fa-f]+)\s*$)"));

}  // namespace

std::optional<Vn300PacketRecord> Vn300LogCodec::parseLine(
    QStringView line,
    QString* errorMessage,
    bool* ignored) {
    if (errorMessage) errorMessage->clear();
    if (ignored) *ignored = false;
    const QString text = line.toString().trimmed();
    if (text.isEmpty() || text.startsWith(QLatin1Char(';'))) {
        if (ignored) *ignored = true;
        return std::nullopt;
    }
    const auto match = kLine.match(text);
    if (!match.hasMatch()) {
        if (errorMessage) *errorMessage = QStringLiteral("invalid VN300 replay line");
        return std::nullopt;
    }
    bool secondsOk = false;
    bool nanosOk = true;
    const qint64 seconds = match.captured(1).toLongLong(&secondsOk);
    QString fraction = match.captured(2);
    if (!fraction.isEmpty()) {
        fraction = fraction.leftJustified(9, QLatin1Char('0'), true);
    }
    const qint64 nanos = fraction.isEmpty() ? 0 : fraction.toLongLong(&nanosOk);
    const QString hex = match.captured(3);
    const QByteArray packet = QByteArray::fromHex(hex.toLatin1());
    if (!secondsOk || !nanosOk || (hex.size() % 2) != 0 || packet.isEmpty()
        || static_cast<quint8>(packet.front()) != 0xFAU) {
        if (errorMessage) *errorMessage = QStringLiteral("invalid timestamp or VN300 packet");
        return std::nullopt;
    }
    return Vn300PacketRecord{packet, seconds * 1'000'000'000LL + nanos};
}

QString Vn300LogCodec::formatLine(const Vn300PacketRecord& record) {
    const qint64 seconds = record.monotonicTimestampNs / 1'000'000'000LL;
    const qint64 nanos = record.monotonicTimestampNs % 1'000'000'000LL;
    return QStringLiteral("(%1.%2) %3")
        .arg(seconds)
        .arg(nanos, 9, 10, QLatin1Char('0'))
        .arg(QString::fromLatin1(record.packet.toHex().toUpper()));
}

}  // namespace miata::data
