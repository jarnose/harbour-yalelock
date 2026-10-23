#pragma once

#include <QByteArray>
#include <QString>
#include <cstdint>

// The BLE link-layer protocol spoken by Yale Access / August "Unity" smart
// locks - the lock family behind the Yale Home app's AAEcosystem SDK
// (com.aaecosystemsdk.ble2.*, native libaugustLockComm.so).
//
// Ported from the Apache-2.0 licensed yalexs-ble project
// (https://github.com/Yale-Libs/yalexs-ble), which has reverse-engineered and
// maintained this exact protocol for years as the engine behind Home
// Assistant's official Yale/August Bluetooth integration - and is itself
// hosted under the "Yale-Libs" GitHub org. See THIRD-PARTY-NOTICES.md.
//
// Nothing here was taken from decompiled Yale Home app code. Static analysis
// of the app's native library (checksum/opcode disassembly) and its Kotlin
// classes (GATT UUIDs, enum values) only confirmed independently that this
// already-published project's protocol matches the Yale Home app bit for
// bit, which is what this file implements.
namespace LockProtocol {

// GATT UUIDs. A lock advertises one of four generations; this app only
// targets the two still sold as of this lock's firmware (V3_SUMMER_2017 /
// V4_SPRING_2023 in the app's own enum) - both share the same four
// characteristic UUIDs and differ only in the advertised service UUID.
constexpr char SERVICE_UUID_V3[] = "0000fe24-0000-1000-8000-00805f9b34fb";
constexpr char SERVICE_UUID_V4[] = "0000fcbf-0000-1000-8000-00805f9b34fb";
constexpr char WRITE_CHARACTERISTIC[] = "bd4ac611-0b45-11e3-8ffd-0800200c9a66";
constexpr char READ_CHARACTERISTIC[] = "bd4ac612-0b45-11e3-8ffd-0800200c9a66";
constexpr char SECURE_WRITE_CHARACTERISTIC[] = "bd4ac613-0b45-11e3-8ffd-0800200c9a66";
constexpr char SECURE_READ_CHARACTERISTIC[] = "bd4ac614-0b45-11e3-8ffd-0800200c9a66";

// Every frame on either channel, sent or received, is exactly this many
// bytes: a 16-byte encrypted block plus two trailing plain bytes.
constexpr int FRAME_LEN = 18;

// Byte offset of the operation result in a 0xBB op-response (0 = success;
// the MECH_* range 0x1E-0x23 is a motor stall/jam).
constexpr int RESULT_BYTE = 0x0F;

enum class Command : uint8_t {
    GetStatus = 0x02,
    Unlock = 0x0A,
    Lock = 0x0B,
};

enum class LockStatus { Unknown, Unlocking, Unlocked, Locking, Locked, Jammed, Unlatching, Unlatched, SecureMode };
enum class DoorStatus { Unknown, Closed, Ajar, Open };

// ---- Frame construction -------------------------------------------------

// Builds an 18-byte plain-channel command frame: byte0=0xEE, byte1=opcode,
// byte4=cmdByte, byte0x10=0x02, checksum written into byte 3. Ready to
// encrypt with SecureSession::encryptData().
QByteArray buildOperationCommand(Command opcode, uint8_t cmdByte = 0);

// Builds an 18-byte secure-channel handshake frame: byte0=opcode,
// byte0x10=0x0F, byte0x11=keyIndex. The caller fills bytes 4-12 with its
// payload and must call writeSecureChecksum() before sending.
QByteArray buildSecureCommand(uint8_t opcode, uint8_t keyIndex);

// ---- Checksums -----------------------------------------------------------

// Plain-channel ("simple") checksum: with byte[3] treated as 0, byte[3] =
// -(sum of all FRAME_LEN bytes) mod 256.
uint8_t simpleChecksum(const QByteArray &frame);
void writeSimpleChecksum(QByteArray &frame);
bool validateSimpleChecksum(const QByteArray &frame);

// Secure-channel checksum: a 32-bit little-endian word at bytes 0x0C-0x10,
// equal to -(the three preceding 32-bit LE words at 0x00, 0x04, 0x08) mod
// 2^32.
quint32 securityChecksum(const QByteArray &frame);
void writeSecureChecksum(QByteArray &frame);
bool validateSecurityChecksum(const QByteArray &frame);

// ---- Response parsing ------------------------------------------------

struct ParsedState {
    bool recognized = false;     // false: unknown frame, surface for diagnosis
    bool hasLockStatus = false;  // true: `lock` below is valid
    bool hasDoorStatus = false;  // true: `door` below is valid
    LockStatus lock = LockStatus::Unknown;
    DoorStatus door = DoorStatus::Unknown;
    bool isOpResponse = false;   // 0xBB response for LOCK/UNLOCK
    bool opSucceeded = false;
    uint8_t opResultCode = 0;
};

// Parses one decrypted 18-byte notification frame from the plain data
// channel (frame[0] is 0xAA - command ack - or 0xBB - op-response/status
// push). Only GETSTATUS/LOCK/UNLOCK are decoded; anything else comes back
// with recognized=false.
ParsedState parseState(const QByteArray &frame);

QString lockStatusName(LockStatus status);
QString doorStatusName(DoorStatus status);

} // namespace LockProtocol
