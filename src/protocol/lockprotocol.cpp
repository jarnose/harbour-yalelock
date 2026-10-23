#include "lockprotocol.h"

namespace LockProtocol {

namespace {

// StatusType values carried in byte[4] of a GETSTATUS (0xBB 0x02) response.
constexpr uint8_t STATUS_LOCK_ONLY = 0x02;
constexpr uint8_t STATUS_DOOR_ONLY = 0x2E;
constexpr uint8_t STATUS_DOOR_AND_LOCK = 0x2F;

LockStatus lockStatusFromByte(uint8_t v)
{
    switch (v) {
    case 0x02: return LockStatus::Unlocking;
    case 0x03: return LockStatus::Unlocked;
    case 0x04: return LockStatus::Locking;
    case 0x05: return LockStatus::Locked;
    case 0x07: return LockStatus::Jammed;
    case 0x09: return LockStatus::Unlatching;
    case 0x0A: return LockStatus::Unlatched;
    case 0x0C: return LockStatus::SecureMode;
    default: return LockStatus::Unknown;
    }
}

DoorStatus doorStatusFromByte(uint8_t v)
{
    switch (v) {
    case 0x01: return DoorStatus::Closed;
    case 0x02: return DoorStatus::Ajar;
    case 0x03: return DoorStatus::Open;
    default: return DoorStatus::Unknown;
    }
}

} // namespace

QByteArray buildOperationCommand(Command opcode, uint8_t cmdByte)
{
    QByteArray cmd(FRAME_LEN, char(0));
    cmd[0x00] = char(0xEE);
    cmd[0x01] = char(static_cast<uint8_t>(opcode));
    cmd[0x04] = char(cmdByte);
    cmd[0x10] = char(0x02);
    writeSimpleChecksum(cmd);
    return cmd;
}

QByteArray buildSecureCommand(uint8_t opcode, uint8_t keyIndex)
{
    QByteArray cmd(FRAME_LEN, char(0));
    cmd[0x00] = char(opcode);
    cmd[0x10] = char(0x0F);
    cmd[0x11] = char(keyIndex);
    return cmd;
}

uint8_t simpleChecksum(const QByteArray &frame)
{
    Q_ASSERT(frame.size() >= FRAME_LEN);
    uint32_t sum = 0;
    for (int i = 0; i < FRAME_LEN; ++i) {
        if (i == 0x03)
            continue; // the checksum byte itself reads as 0 while summing
        sum += uint8_t(frame[i]);
    }
    return uint8_t((0 - sum) & 0xFF);
}

void writeSimpleChecksum(QByteArray &frame)
{
    frame[0x03] = char(0);
    frame[0x03] = char(simpleChecksum(frame));
}

bool validateSimpleChecksum(const QByteArray &frame)
{
    if (frame.size() < FRAME_LEN)
        return false;
    uint32_t sum = 0;
    for (int i = 0; i < FRAME_LEN; ++i)
        sum += uint8_t(frame[i]);
    return (sum & 0xFF) == 0;
}

namespace {
quint32 le32(const QByteArray &buf, int offset)
{
    return quint32(uint8_t(buf[offset])) | (quint32(uint8_t(buf[offset + 1])) << 8)
         | (quint32(uint8_t(buf[offset + 2])) << 16) | (quint32(uint8_t(buf[offset + 3])) << 24);
}
void putLe32(QByteArray &buf, int offset, quint32 v)
{
    buf[offset] = char(v & 0xFF);
    buf[offset + 1] = char((v >> 8) & 0xFF);
    buf[offset + 2] = char((v >> 16) & 0xFF);
    buf[offset + 3] = char((v >> 24) & 0xFF);
}
} // namespace

quint32 securityChecksum(const QByteArray &frame)
{
    Q_ASSERT(frame.size() >= FRAME_LEN);
    const quint32 v1 = le32(frame, 0x00);
    const quint32 v2 = le32(frame, 0x04);
    const quint32 v3 = le32(frame, 0x08);
    return quint32(0 - (v1 + v2 + v3));
}

void writeSecureChecksum(QByteArray &frame)
{
    putLe32(frame, 0x0C, 0);
    putLe32(frame, 0x0C, securityChecksum(frame));
}

bool validateSecurityChecksum(const QByteArray &frame)
{
    if (frame.size() < FRAME_LEN)
        return false;
    const quint32 stored = le32(frame, 0x0C);
    QByteArray zeroed = frame;
    putLe32(zeroed, 0x0C, 0);
    return securityChecksum(zeroed) == stored;
}

ParsedState parseState(const QByteArray &frame)
{
    ParsedState result;
    if (frame.size() < FRAME_LEN)
        return result;

    const uint8_t flag = uint8_t(frame[0x00]);
    const uint8_t opcode = uint8_t(frame[0x01]);
    if (flag != 0xAA && flag != 0xBB)
        return result;

    const auto op = static_cast<Command>(opcode);

    if (flag == 0xBB && (op == Command::Lock || op == Command::Unlock)) {
        result.recognized = true;
        result.isOpResponse = true;
        result.opResultCode = uint8_t(frame[RESULT_BYTE]);
        result.opSucceeded = (result.opResultCode == 0x00);
        if (!result.opSucceeded) {
            // Every non-zero result needs a person at the lock to clear, so
            // all of them surface the same way: jammed.
            result.hasLockStatus = true;
            result.lock = LockStatus::Jammed;
        }
        return result;
    }

    if (flag == 0xAA && (op == Command::Lock || op == Command::Unlock)) {
        // Command acknowledgment only; the real result follows as a 0xBB
        // op-response above.
        result.recognized = true;
        return result;
    }

    if (flag == 0xBB && op == Command::GetStatus) {
        result.recognized = true;
        const uint8_t statusType = uint8_t(frame[0x04]);
        if (statusType == STATUS_LOCK_ONLY) {
            result.hasLockStatus = true;
            result.lock = lockStatusFromByte(uint8_t(frame[0x08]));
        } else if (statusType == STATUS_DOOR_ONLY) {
            result.hasDoorStatus = true;
            result.door = doorStatusFromByte(uint8_t(frame[0x08]));
        } else if (statusType == STATUS_DOOR_AND_LOCK) {
            result.hasLockStatus = true;
            result.hasDoorStatus = true;
            result.lock = lockStatusFromByte(uint8_t(frame[0x08]));
            result.door = doorStatusFromByte(uint8_t(frame[0x09]));
        }
        // STATUS_BATTERY (0x0F) and anything else: recognized, no lock/door state.
        return result;
    }

    if (flag == 0xAA && op == Command::GetStatus) {
        result.recognized = true; // ack only
        return result;
    }

    return result;
}

QString lockStatusName(LockStatus status)
{
    switch (status) {
    case LockStatus::Unlocking: return QStringLiteral("unlocking");
    case LockStatus::Unlocked: return QStringLiteral("unlocked");
    case LockStatus::Locking: return QStringLiteral("locking");
    case LockStatus::Locked: return QStringLiteral("locked");
    case LockStatus::Jammed: return QStringLiteral("jammed");
    case LockStatus::Unlatching: return QStringLiteral("unlatching");
    case LockStatus::Unlatched: return QStringLiteral("unlatched");
    case LockStatus::SecureMode: return QStringLiteral("secureMode");
    default: return QStringLiteral("unknown");
    }
}

QString doorStatusName(DoorStatus status)
{
    switch (status) {
    case DoorStatus::Closed: return QStringLiteral("closed");
    case DoorStatus::Ajar: return QStringLiteral("ajar");
    case DoorStatus::Open: return QStringLiteral("open");
    default: return QStringLiteral("unknown");
    }
}

} // namespace LockProtocol
