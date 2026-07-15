#include "vn300/vn300_binary_parser.h"

#include <QtEndian>

#include <array>
#include <cstring>
#include <optional>

namespace miata::data {
namespace {

constexpr unsigned char kSync = 0xFA;
constexpr qsizetype kMaximumPacketSize = 1024;
constexpr int kCommonGroup = 0;
constexpr int kAttitudeGroup = 4;

struct Header {
    qsizetype size = 0;
    qsizetype payloadSize = 0;
    std::array<quint32, 28> typeMasks{};
};

enum class HeaderState { Complete, Incomplete, Invalid };

int commonTypeSize(int bit) {
    static constexpr std::array<int, 15> sizes{
        8, 8, 8, 12, 16, 12, 24, 12, 12, 24, 20, 28, 2, 4, 8};
    return bit >= 0 && bit < static_cast<int>(sizes.size()) ? sizes[bit] : -1;
}

int attitudeTypeSize(int bit) {
    switch (bit) {
    case 1: return 12;  // Ypr
    case 2: return 16;  // Quaternion
    case 3: return 36;  // Dcm
    case 4: return 12;  // MagNed
    case 5: return 12;  // AccelNed
    case 6: return 12;  // LinBodyAcc
    case 7: return 12;  // LinAccelNed
    case 8: return 12;  // YprU
    case 12: return 12; // Heave
    case 13: return 4;  // AttU
    default: return -1;
    }
}

HeaderState parseHeader(const QByteArray& data, Header* header) {
    qsizetype cursor = 1;
    std::array<bool, 28> activeGroups{};
    bool extension = true;
    int groupByteIndex = 0;
    while (extension) {
        if (cursor >= data.size()) return HeaderState::Incomplete;
        if (groupByteIndex >= 4) return HeaderState::Invalid;
        const auto byte = static_cast<quint8>(data.at(cursor++));
        extension = (byte & 0x80U) != 0;
        for (int bit = 0; bit < 7; ++bit) {
            if ((byte & (1U << bit)) != 0) activeGroups[groupByteIndex * 7 + bit] = true;
        }
        ++groupByteIndex;
    }

    for (int group = 0; group < static_cast<int>(activeGroups.size()); ++group) {
        if (!activeGroups[group]) continue;
        if (group != kCommonGroup && group != kAttitudeGroup) return HeaderState::Invalid;

        bool typeExtension = true;
        int wordIndex = 0;
        while (typeExtension) {
            if (cursor + 2 > data.size()) return HeaderState::Incomplete;
            if (wordIndex >= 2) return HeaderState::Invalid;
            const quint16 word = qFromLittleEndian<quint16>(data.constData() + cursor);
            cursor += 2;
            typeExtension = (word & 0x8000U) != 0;
            const quint16 selected = word & 0x7FFFU;
            header->typeMasks[group] |= static_cast<quint32>(selected) << (wordIndex * 15);
            ++wordIndex;
        }
    }

    qsizetype payloadSize = 0;
    for (int group : {kCommonGroup, kAttitudeGroup}) {
        const quint32 mask = header->typeMasks[group];
        for (int bit = 0; bit < 30; ++bit) {
            if ((mask & (quint32{1} << bit)) == 0) continue;
            const int size = group == kCommonGroup ? commonTypeSize(bit) : attitudeTypeSize(bit);
            if (size < 0) return HeaderState::Invalid;
            payloadSize += size;
        }
    }
    if (payloadSize == 0 || cursor + payloadSize + 2 > kMaximumPacketSize) {
        return HeaderState::Invalid;
    }
    header->size = cursor;
    header->payloadSize = payloadSize;
    return HeaderState::Complete;
}

template <typename T>
T readLittleEndian(const char*& cursor) {
    T value{};
    std::memcpy(&value, cursor, sizeof(T));
#if Q_BYTE_ORDER == Q_BIG_ENDIAN
    if constexpr (sizeof(T) == 2) {
        auto bits = qFromLittleEndian<quint16>(cursor);
        std::memcpy(&value, &bits, sizeof(T));
    } else if constexpr (sizeof(T) == 4) {
        auto bits = qFromLittleEndian<quint32>(cursor);
        std::memcpy(&value, &bits, sizeof(T));
    } else if constexpr (sizeof(T) == 8) {
        auto bits = qFromLittleEndian<quint64>(cursor);
        std::memcpy(&value, &bits, sizeof(T));
    }
#endif
    cursor += sizeof(T);
    return value;
}

void add(QList<SignalSample>& out, const QString& name, double value,
         const QString& unit, qint64 timestamp) {
    out.append({name, value, unit, timestamp, SignalSource::Vn300});
}

void addVec3(QList<SignalSample>& out, const char*& cursor, const QString& prefix,
             const std::array<QString, 3>& suffixes, const QString& unit, qint64 timestamp) {
    for (const QString& suffix : suffixes) {
        add(out, prefix + suffix, readLittleEndian<float>(cursor), unit, timestamp);
    }
}

void decodeCommon(int bit, const char*& cursor, QList<SignalSample>& out, qint64 timestamp) {
    const auto axes = std::array<QString, 3>{QStringLiteral("_x"), QStringLiteral("_y"), QStringLiteral("_z")};
    const auto ned = std::array<QString, 3>{QStringLiteral("_north"), QStringLiteral("_east"), QStringLiteral("_down")};
    switch (bit) {
    case 0: add(out, QStringLiteral("VN300.time_startup"), readLittleEndian<quint64>(cursor) * 1e-9, QStringLiteral("s"), timestamp); break;
    case 1: add(out, QStringLiteral("VN300.time_gps"), readLittleEndian<quint64>(cursor) * 1e-9, QStringLiteral("s"), timestamp); break;
    case 2: add(out, QStringLiteral("VN300.time_sync_in"), readLittleEndian<quint64>(cursor) * 1e-9, QStringLiteral("s"), timestamp); break;
    case 3: {
        for (const QString& name : {QStringLiteral("VN300.yaw"), QStringLiteral("VN300.pitch"), QStringLiteral("VN300.roll")})
            add(out, name, readLittleEndian<float>(cursor), QStringLiteral("deg"), timestamp);
        break;
    }
    case 4: {
        for (const QString& name : {QStringLiteral("VN300.quat_x"), QStringLiteral("VN300.quat_y"), QStringLiteral("VN300.quat_z"), QStringLiteral("VN300.quat_s")})
            add(out, name, readLittleEndian<float>(cursor), {}, timestamp);
        break;
    }
    case 5: addVec3(out, cursor, QStringLiteral("VN300.angular_rate"), axes, QStringLiteral("rad/s"), timestamp); break;
    case 6:
        add(out, QStringLiteral("VN300.latitude"), readLittleEndian<double>(cursor), QStringLiteral("deg"), timestamp);
        add(out, QStringLiteral("VN300.longitude"), readLittleEndian<double>(cursor), QStringLiteral("deg"), timestamp);
        add(out, QStringLiteral("VN300.altitude"), readLittleEndian<double>(cursor), QStringLiteral("m"), timestamp);
        break;
    case 7: addVec3(out, cursor, QStringLiteral("VN300.velocity"), ned, QStringLiteral("m/s"), timestamp); break;
    case 8: addVec3(out, cursor, QStringLiteral("VN300.accel"), axes, QStringLiteral("m/s^2"), timestamp); break;
    case 9:
        addVec3(out, cursor, QStringLiteral("VN300.uncomp_accel"), axes, QStringLiteral("m/s^2"), timestamp);
        addVec3(out, cursor, QStringLiteral("VN300.uncomp_gyro"), axes, QStringLiteral("rad/s"), timestamp);
        break;
    case 10:
        addVec3(out, cursor, QStringLiteral("VN300.mag"), axes, QStringLiteral("G"), timestamp);
        add(out, QStringLiteral("VN300.temperature"), readLittleEndian<float>(cursor), QStringLiteral("deg C"), timestamp);
        add(out, QStringLiteral("VN300.pressure"), readLittleEndian<float>(cursor), QStringLiteral("kPa"), timestamp);
        break;
    case 11:
        add(out, QStringLiteral("VN300.delta_time"), readLittleEndian<float>(cursor), QStringLiteral("s"), timestamp);
        addVec3(out, cursor, QStringLiteral("VN300.delta_theta"), axes, QStringLiteral("deg"), timestamp);
        addVec3(out, cursor, QStringLiteral("VN300.delta_velocity"), axes, QStringLiteral("m/s"), timestamp);
        break;
    case 12: add(out, QStringLiteral("VN300.ins_status"), readLittleEndian<quint16>(cursor), {}, timestamp); break;
    case 13: add(out, QStringLiteral("VN300.sync_in_count"), readLittleEndian<quint32>(cursor), {}, timestamp); break;
    case 14: add(out, QStringLiteral("VN300.time_gps_pps"), readLittleEndian<quint64>(cursor) * 1e-9, QStringLiteral("s"), timestamp); break;
    default: break;
    }
}

QList<SignalDefinition> definitions() {
    QList<SignalDefinition> result;
    auto one = [&](const QString& name, const QString& unit = {}) { result.append({name, unit}); };
    for (const QString& name : {QStringLiteral("time_startup"), QStringLiteral("time_gps"), QStringLiteral("time_sync_in")}) one(QStringLiteral("VN300.") + name, QStringLiteral("s"));
    for (const QString& name : {QStringLiteral("yaw"), QStringLiteral("pitch"), QStringLiteral("roll")}) one(QStringLiteral("VN300.") + name, QStringLiteral("deg"));
    for (const QString& suffix : {QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("z"), QStringLiteral("s")}) one(QStringLiteral("VN300.quat_") + suffix);
    for (const QString& prefix : {QStringLiteral("angular_rate"), QStringLiteral("uncomp_gyro")}) for (const QString& axis : {QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("z")}) one(QStringLiteral("VN300.") + prefix + QLatin1Char('_') + axis, QStringLiteral("rad/s"));
    one(QStringLiteral("VN300.latitude"), QStringLiteral("deg")); one(QStringLiteral("VN300.longitude"), QStringLiteral("deg")); one(QStringLiteral("VN300.altitude"), QStringLiteral("m"));
    for (const QString& dir : {QStringLiteral("north"), QStringLiteral("east"), QStringLiteral("down")}) one(QStringLiteral("VN300.velocity_") + dir, QStringLiteral("m/s"));
    for (const QString& prefix : {QStringLiteral("accel"), QStringLiteral("uncomp_accel"), QStringLiteral("lin_body_accel")}) for (const QString& axis : {QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("z")}) one(QStringLiteral("VN300.") + prefix + QLatin1Char('_') + axis, QStringLiteral("m/s^2"));
    for (const QString& axis : {QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("z")}) one(QStringLiteral("VN300.mag_") + axis, QStringLiteral("G"));
    one(QStringLiteral("VN300.temperature"), QStringLiteral("deg C")); one(QStringLiteral("VN300.pressure"), QStringLiteral("kPa")); one(QStringLiteral("VN300.delta_time"), QStringLiteral("s"));
    for (const QString& axis : {QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("z")}) { one(QStringLiteral("VN300.delta_theta_") + axis, QStringLiteral("deg")); one(QStringLiteral("VN300.delta_velocity_") + axis, QStringLiteral("m/s")); }
    one(QStringLiteral("VN300.ins_status")); one(QStringLiteral("VN300.sync_in_count")); one(QStringLiteral("VN300.time_gps_pps"), QStringLiteral("s"));
    return result;
}

}  // namespace

QList<SignalSample> Vn300BinaryParser::consume(const QByteArray& bytes, qint64 timestamp) {
    buffer_.append(bytes);
    QList<SignalSample> output;
    while (true) {
        const qsizetype sync = buffer_.indexOf(static_cast<char>(kSync));
        if (sync < 0) { buffer_.clear(); break; }
        if (sync > 0) buffer_.remove(0, sync);

        Header header;
        const HeaderState state = parseHeader(buffer_, &header);
        if (state == HeaderState::Incomplete) break;
        if (state == HeaderState::Invalid) { ++formatErrorCount_; buffer_.remove(0, 1); continue; }
        const qsizetype packetSize = header.size + header.payloadSize + 2;
        if (buffer_.size() < packetSize) break;
        if (calculateCrc(buffer_.constData() + 1, packetSize - 1) != 0) {
            ++crcErrorCount_; buffer_.remove(0, 1); continue;
        }

        const char* cursor = buffer_.constData() + header.size;
        for (int group : {kCommonGroup, kAttitudeGroup}) {
            for (int bit = 0; bit < 30; ++bit) {
                if ((header.typeMasks[group] & (quint32{1} << bit)) == 0) continue;
                if (group == kCommonGroup) {
                    decodeCommon(bit, cursor, output, timestamp);
                } else if (bit == 6) {
                    const auto axes = std::array<QString, 3>{QStringLiteral("_x"), QStringLiteral("_y"), QStringLiteral("_z")};
                    addVec3(output, cursor, QStringLiteral("VN300.lin_body_accel"), axes, QStringLiteral("m/s^2"), timestamp);
                } else {
                    cursor += attitudeTypeSize(bit);
                }
            }
        }
        ++validPacketCount_;
        buffer_.remove(0, packetSize);
    }
    return output;
}

void Vn300BinaryParser::reset() { buffer_.clear(); validPacketCount_ = crcErrorCount_ = formatErrorCount_ = 0; }
quint64 Vn300BinaryParser::validPacketCount() const { return validPacketCount_; }
quint64 Vn300BinaryParser::crcErrorCount() const { return crcErrorCount_; }
quint64 Vn300BinaryParser::formatErrorCount() const { return formatErrorCount_; }
QList<SignalDefinition> Vn300BinaryParser::signalDefinitions() { return definitions(); }
QStringList Vn300BinaryParser::canonicalSignalNames() { QStringList out; for (const auto& d : definitions()) out.append(d.canonicalName); return out; }

quint16 Vn300BinaryParser::calculateCrc(const char* data, qsizetype length) {
    quint16 crc = 0;
    for (qsizetype i = 0; i < length; ++i) {
        crc = static_cast<quint16>((crc >> 8) | (crc << 8));
        crc ^= static_cast<quint8>(data[i]);
        crc ^= static_cast<quint8>(crc & 0xFFU) >> 4;
        crc ^= static_cast<quint16>(crc << 12);
        crc ^= static_cast<quint16>((crc & 0x00FFU) << 5);
    }
    return crc;
}

}  // namespace miata::data
