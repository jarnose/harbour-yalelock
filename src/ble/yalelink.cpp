#include "yalelink.h"

#include "bluezcall.h"
#include "bluezmanagedobjects.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QDBusVariant>

using LockProtocol::Command;

namespace {

const QString kBluezService = QStringLiteral("org.bluez");
const QString kPropertiesInterface = QStringLiteral("org.freedesktop.DBus.Properties");
const QString kDeviceInterface = QStringLiteral("org.bluez.Device1");
const QString kCharInterface = QStringLiteral("org.bluez.GattCharacteristic1");
const QString kObjectManagerInterface = QStringLiteral("org.freedesktop.DBus.ObjectManager");

const QString kErrInProgress = QStringLiteral("org.bluez.Error.InProgress");
const QString kErrAlreadyConnected = QStringLiteral("org.bluez.Error.AlreadyConnected");

constexpr int kConnectTimeoutMs = 20000;
constexpr int kServicesTimeoutMs = 15000;
constexpr int kSubscribeTimeoutMs = 10000;
constexpr int kHandshakeTimeoutMs = 10000;
constexpr int kAckTimeoutMs = 10000;       // GETSTATUS: ack + answer
constexpr int kOperationTimeoutMs = 15000; // LOCK/UNLOCK: motor can take a few seconds

} // namespace

YaleLink::YaleLink(QObject *parent)
    : QObject(parent)
{
    registerBluezManagedObjectTypes();
    m_stepTimer.setSingleShot(true);
    connect(&m_stepTimer, &QTimer::timeout, this, &YaleLink::onStepTimeout);
    m_secureTimeout.setSingleShot(true);
    m_dataTimeout.setSingleShot(true);
}

YaleLink::~YaleLink()
{
    close(false);
}

QString YaleLink::stepName(Step step)
{
    switch (step) {
    case Step::Idle: return QStringLiteral("Idle");
    case Step::Connecting: return QStringLiteral("Connecting");
    case Step::WaitServices: return QStringLiteral("WaitServices");
    case Step::Subscribing: return QStringLiteral("Subscribing");
    case Step::Handshaking: return QStringLiteral("Handshaking");
    case Step::Running: return QStringLiteral("Running");
    }
    return QStringLiteral("?");
}

bool YaleLink::isReady() const
{
    return m_step == Step::Running;
}

QVariant YaleLink::deviceProperty(const QString &name) const
{
    if (m_devicePath.isEmpty())
        return QVariant();
    QDBusInterface props(kBluezService, m_devicePath, kPropertiesInterface, QDBusConnection::systemBus());
    QDBusReply<QDBusVariant> reply = props.call(QStringLiteral("Get"), kDeviceInterface, name);
    return reply.isValid() ? reply.value().variant() : QVariant();
}

void YaleLink::enterStep(Step step, int timeoutMs)
{
    m_step = step;
    if (timeoutMs > 0)
        m_stepTimer.start(timeoutMs);
    else
        m_stepTimer.stop();
}

void YaleLink::onStepTimeout()
{
    switch (m_step) {
    case Step::Connecting:
        fail(tr("Could not reach the lock. Is it nearby and powered?"));
        break;
    case Step::WaitServices:
        fail(tr("The lock connected but did not finish Bluetooth service discovery."));
        break;
    case Step::Subscribing:
        fail(tr("The lock did not open its command channel."));
        break;
    case Step::Handshaking:
        fail(tr("The lock did not respond to the security handshake."));
        break;
    default:
        break;
    }
}

void YaleLink::open(const QString &devicePath, const QByteArray &offlineKey16, quint8 keyIndex)
{
    close(false);
    m_devicePath = devicePath;
    m_offlineKey = offlineKey16;
    m_keyIndex = keyIndex;

    QDBusConnection::systemBus().connect(
            kBluezService, m_devicePath, kPropertiesInterface, QStringLiteral("PropertiesChanged"), this,
            SLOT(onDevicePropertiesChanged(QString, QVariantMap, QStringList)));
    m_deviceHooked = true;

    m_connected = deviceProperty(QStringLiteral("Connected")).toBool();
    m_servicesResolved = deviceProperty(QStringLiteral("ServicesResolved")).toBool();

    if (m_connected) {
        enterStep(Step::WaitServices, kServicesTimeoutMs);
        proceed();
        return;
    }

    enterStep(Step::Connecting, kConnectTimeoutMs);
    const quint64 generation = m_generation;
    auto *watcher = new QDBusPendingCallWatcher(
            BluezCall::async(m_devicePath, kDeviceInterface, QStringLiteral("Connect"), {}, kConnectTimeoutMs), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, generation](QDBusPendingCallWatcher *w) {
        const QDBusPendingReply<> reply = *w;
        w->deleteLater();
        if (generation != m_generation)
            return;
        if (reply.isError() && reply.error().name() != kErrInProgress
            && reply.error().name() != kErrAlreadyConnected) {
            fail(tr("Could not connect to the lock: %1").arg(reply.error().message()));
            return;
        }
        m_connected = deviceProperty(QStringLiteral("Connected")).toBool();
        m_servicesResolved = deviceProperty(QStringLiteral("ServicesResolved")).toBool();
        enterStep(Step::WaitServices, kServicesTimeoutMs);
        proceed();
    });
}

void YaleLink::proceed()
{
    if (m_step != Step::WaitServices && m_step != Step::Connecting)
        return;
    if (!m_connected)
        return;
    if (m_step == Step::Connecting)
        enterStep(Step::WaitServices, kServicesTimeoutMs);
    if (!m_servicesResolved)
        return;
    subscribeBoth();
}

void YaleLink::onDevicePropertiesChanged(const QString &interface, const QVariantMap &changed,
                                         const QStringList &invalidated)
{
    Q_UNUSED(invalidated);
    if (interface != kDeviceInterface)
        return;
    if (changed.contains(QStringLiteral("Connected"))) {
        m_connected = changed.value(QStringLiteral("Connected")).toBool();
        if (!m_connected) {
            m_servicesResolved = false;
            if (m_step != Step::Idle) {
                const bool wasRunning = m_step == Step::Running;
                const QString where = stepName(m_step);
                close(false);
                if (wasRunning)
                    emit linkLost();
                else
                    emit failed(tr("The lock dropped the connection (during %1).").arg(where));
            }
            return;
        }
    }
    if (changed.contains(QStringLiteral("ServicesResolved")))
        m_servicesResolved = changed.value(QStringLiteral("ServicesResolved")).toBool();
    proceed();
}

QString YaleLink::discoverCharacteristics()
{
    QDBusInterface manager(kBluezService, QStringLiteral("/"), kObjectManagerInterface,
                           QDBusConnection::systemBus());
    QDBusReply<BluezObjectMap> reply = manager.call(QStringLiteral("GetManagedObjects"));
    if (!reply.isValid())
        return tr("Could not list Bluetooth services: %1").arg(reply.error().message());

    const QString devicePrefix = m_devicePath + QLatin1Char('/');
    const BluezObjectMap objects = reply.value();

    QMap<QString, QString> characteristics; // uuid (lower) -> object path
    for (auto it = objects.constBegin(); it != objects.constEnd(); ++it) {
        const QString path = it.key().path();
        if (!path.startsWith(devicePrefix))
            continue;
        if (it.value().contains(kCharInterface))
            characteristics.insert(it.value().value(kCharInterface).value(QStringLiteral("UUID")).toString().toLower(),
                                   path);
    }

    m_writePath = characteristics.value(QLatin1String(LockProtocol::WRITE_CHARACTERISTIC));
    m_readPath = characteristics.value(QLatin1String(LockProtocol::READ_CHARACTERISTIC));
    m_secureWritePath = characteristics.value(QLatin1String(LockProtocol::SECURE_WRITE_CHARACTERISTIC));
    m_secureReadPath = characteristics.value(QLatin1String(LockProtocol::SECURE_READ_CHARACTERISTIC));

    if (m_writePath.isEmpty() || m_readPath.isEmpty() || m_secureWritePath.isEmpty() || m_secureReadPath.isEmpty())
        return tr("This device does not offer the Yale lock command service. Is it a Yale Access / "
                  "August lock, and is it in range?");
    return QString();
}

void YaleLink::subscribeBoth()
{
    enterStep(Step::Subscribing, kSubscribeTimeoutMs);
    emit trace(QStringLiteral("connected, services resolved; discovering characteristics"));

    const QString problem = discoverCharacteristics();
    if (!problem.isEmpty()) {
        fail(problem);
        return;
    }
    emit trace(QStringLiteral("found all 4 characteristics; subscribing"));

    if (!m_secureNotifyHooked) {
        QDBusConnection::systemBus().connect(
                kBluezService, m_secureReadPath, kPropertiesInterface, QStringLiteral("PropertiesChanged"), this,
                SLOT(onSecureNotify(QString, QVariantMap, QStringList)));
        m_secureNotifyHooked = true;
    }
    if (!m_dataNotifyHooked) {
        QDBusConnection::systemBus().connect(
                kBluezService, m_readPath, kPropertiesInterface, QStringLiteral("PropertiesChanged"), this,
                SLOT(onDataNotify(QString, QVariantMap, QStringList)));
        m_dataNotifyHooked = true;
    }

    // Order matters: the secure channel's notification must be live before
    // the data channel's, mirroring yalexs-ble's connect() - the lock seems
    // to expect the phone listening on both before the handshake starts.
    const quint64 generation = m_generation;
    auto *secureWatcher = new QDBusPendingCallWatcher(
            BluezCall::async(m_secureReadPath, kCharInterface, QStringLiteral("StartNotify")), this);
    connect(secureWatcher, &QDBusPendingCallWatcher::finished, this, [this, generation](QDBusPendingCallWatcher *w) {
        const QDBusPendingReply<> reply = *w;
        w->deleteLater();
        if (generation != m_generation)
            return;
        if (reply.isError()) {
            fail(tr("Could not subscribe to the lock's secure channel: %1").arg(reply.error().message()));
            return;
        }
        m_secureSubscribed = true;
        emit trace(QStringLiteral("secure channel subscribed; subscribing data channel"));

        auto *dataWatcher = new QDBusPendingCallWatcher(
                BluezCall::async(m_readPath, kCharInterface, QStringLiteral("StartNotify")), this);
        connect(dataWatcher, &QDBusPendingCallWatcher::finished, this, [this, generation](QDBusPendingCallWatcher *w2) {
            const QDBusPendingReply<> reply2 = *w2;
            w2->deleteLater();
            if (generation != m_generation)
                return;
            if (reply2.isError()) {
                fail(tr("Could not subscribe to the lock's data channel: %1").arg(reply2.error().message()));
                return;
            }
            m_dataSubscribed = true;
            emit trace(QStringLiteral("data channel subscribed; starting handshake"));
            startHandshake();
        });
    });
}

void YaleLink::startHandshake()
{
    enterStep(Step::Handshaking, kHandshakeTimeoutMs);

    QString error;
    if (!m_secureCipher.hasKey())
        m_secureCipher.setKey(m_offlineKey);

    m_handshakeRandom = secureRandomBytes(16);
    if (m_handshakeRandom.size() != 16) {
        fail(tr("Could not generate a random handshake key."));
        return;
    }

    // Step 1: SEC_LOCK_TO_MOBILE_KEY_EXCHANGE, encrypted with the offline key.
    QByteArray cmd1 = LockProtocol::buildSecureCommand(0x01, m_keyIndex);
    cmd1.replace(0x04, 8, m_handshakeRandom.left(8));
    LockProtocol::writeSecureChecksum(cmd1);
    emit trace(QStringLiteral("sending SEC_LOCK_TO_MOBILE_KEY_EXCHANGE (key index %1)").arg(m_keyIndex));

    sendSecure(cmd1, [this](const QByteArray &resp1) {
        emit trace(QStringLiteral("got handshake step 1 response: %1").arg(QString::fromLatin1(resp1.toHex())));
        if (uint8_t(resp1[0x00]) != 0x02) {
            fail(tr("The lock rejected the handshake (wrong offline key or key index)."));
            return;
        }
        QByteArray sessionKey(16, char(0));
        sessionKey.replace(0x00, 8, m_handshakeRandom.left(8));
        sessionKey.replace(0x08, 8, resp1.mid(0x04, 8));
        m_secureCipher.setKey(sessionKey);

        // Step 2: SEC_INITIALIZATION_COMMAND, now under the derived session key.
        QByteArray cmd3 = LockProtocol::buildSecureCommand(0x03, m_keyIndex);
        cmd3.replace(0x04, 8, m_handshakeRandom.mid(8, 8));
        LockProtocol::writeSecureChecksum(cmd3);
        emit trace(QStringLiteral("sending SEC_INITIALIZATION_COMMAND"));

        sendSecure(cmd3, [this, sessionKey](const QByteArray &resp3) {
            emit trace(QStringLiteral("got handshake step 2 response: %1").arg(QString::fromLatin1(resp3.toHex())));
            if (uint8_t(resp3[0x00]) != 0x04) {
                fail(tr("The lock rejected the handshake (wrong offline key or key index)."));
                return;
            }
            QString err;
            if (!m_dataCipher.setKey(sessionKey, &err)) {
                fail(tr("Could not start the session cipher: %1").arg(err));
                return;
            }
            enterStep(Step::Running, 0);
            emit ready();
            runNextQueuedOperation();
        });
    });
}

void YaleLink::sendSecure(const QByteArray &plainFrame18, std::function<void(const QByteArray &)> onResponse)
{
    QByteArray encrypted16;
    QString error;
    if (!m_secureCipher.encryptBlock(plainFrame18.left(16), &encrypted16, &error)) {
        fail(tr("Could not encrypt the handshake command: %1").arg(error));
        return;
    }
    const QByteArray wire = encrypted16 + plainFrame18.mid(16, 2);

    m_pendingSecureCb = std::move(onResponse);
    m_secureTimeout.disconnect();
    connect(&m_secureTimeout, &QTimer::timeout, this, [this]() {
        m_pendingSecureCb = nullptr;
        fail(tr("The lock did not answer the handshake in time."));
    });
    m_secureTimeout.start(kHandshakeTimeoutMs);

    const quint64 generation = m_generation;
    auto *watcher = new QDBusPendingCallWatcher(
            BluezCall::async(m_secureWritePath, kCharInterface, QStringLiteral("WriteValue"), { wire, QVariantMap() }),
            this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, generation](QDBusPendingCallWatcher *w) {
        const QDBusPendingReply<> reply = *w;
        w->deleteLater();
        if (generation != m_generation)
            return;
        if (reply.isError()) {
            m_pendingSecureCb = nullptr;
            fail(tr("Could not write to the lock: %1").arg(reply.error().message()));
        }
    });
}

void YaleLink::onSecureNotify(const QString &interface, const QVariantMap &changed, const QStringList &invalidated)
{
    Q_UNUSED(invalidated);
    if (interface != kCharInterface || !changed.contains(QStringLiteral("Value")))
        return;
    const QByteArray wire = changed.value(QStringLiteral("Value")).toByteArray();
    if (wire.size() != LockProtocol::FRAME_LEN || !m_pendingSecureCb)
        return;

    QByteArray plain16;
    QString error;
    if (!m_secureCipher.decryptBlock(wire.left(16), &plain16, &error))
        return; // garbled frame; let the timeout handle it
    const QByteArray frame = plain16 + wire.mid(16, 2);
    if (!LockProtocol::validateSecurityChecksum(frame))
        return; // same

    m_secureTimeout.stop();
    auto cb = m_pendingSecureCb;
    m_pendingSecureCb = nullptr;
    if (cb)
        cb(frame);
}

void YaleLink::sendData(const QByteArray &plainFrame18, std::function<bool(const QByteArray &)> matcher,
                        std::function<void(bool, const QByteArray &, const QString &)> onDone, int timeoutMs)
{
    QByteArray encrypted16;
    QString error;
    if (!m_dataCipher.encryptBlock(plainFrame18.left(16), &encrypted16, &error)) {
        if (onDone)
            onDone(false, QByteArray(), error);
        return;
    }
    const QByteArray wire = encrypted16 + plainFrame18.mid(16, 2);

    m_pendingDataMatcher = std::move(matcher);
    m_pendingDataCb = onDone;
    m_dataTimeout.disconnect();
    connect(&m_dataTimeout, &QTimer::timeout, this, [this]() {
        m_pendingDataMatcher = nullptr;
        auto cb = m_pendingDataCb;
        m_pendingDataCb = nullptr;
        if (cb)
            cb(false, QByteArray(), tr("The lock did not answer in time."));
    });
    m_dataTimeout.start(timeoutMs);

    const quint64 generation = m_generation;
    auto *watcher = new QDBusPendingCallWatcher(
            BluezCall::async(m_writePath, kCharInterface, QStringLiteral("WriteValue"), { wire, QVariantMap() }), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, generation](QDBusPendingCallWatcher *w) {
        const QDBusPendingReply<> reply = *w;
        w->deleteLater();
        if (generation != m_generation)
            return;
        if (reply.isError()) {
            m_dataTimeout.stop();
            m_pendingDataMatcher = nullptr;
            auto cb = m_pendingDataCb;
            m_pendingDataCb = nullptr;
            if (cb)
                cb(false, QByteArray(), reply.error().message());
        }
    });
}

void YaleLink::onDataNotify(const QString &interface, const QVariantMap &changed, const QStringList &invalidated)
{
    Q_UNUSED(invalidated);
    if (interface != kCharInterface || !changed.contains(QStringLiteral("Value")))
        return;
    const QByteArray wire = changed.value(QStringLiteral("Value")).toByteArray();
    if (wire.size() != LockProtocol::FRAME_LEN || !m_dataCipher.hasKey())
        return;

    QByteArray plain16;
    QString error;
    if (!m_dataCipher.decryptBlock(wire.left(16), &plain16, &error))
        return; // a desynchronized CBC chain recovers on the next good frame
    const QByteArray frame = plain16 + wire.mid(16, 2);
    if (!LockProtocol::validateSimpleChecksum(frame))
        return;

    // Every recognized frame updates state, independent of whether a wait is
    // armed - a status push the lock sends unprompted still counts.
    const LockProtocol::ParsedState parsed = LockProtocol::parseState(frame);
    if (parsed.hasLockStatus)
        emit lockStatusChanged(int(parsed.lock));
    if (parsed.hasDoorStatus)
        emit doorStatusChanged(int(parsed.door));

    if (m_pendingDataMatcher && !m_pendingDataMatcher(frame))
        return; // not the frame this command is waiting for; keep waiting

    if (m_pendingDataCb) {
        m_dataTimeout.stop();
        m_pendingDataMatcher = nullptr;
        auto cb = m_pendingDataCb;
        m_pendingDataCb = nullptr;
        cb(true, frame, QString());
    }
}

void YaleLink::enqueue(std::function<void()> run)
{
    m_opQueue.append(std::move(run));
    runNextQueuedOperation();
}

void YaleLink::runNextQueuedOperation()
{
    if (m_opInFlight || m_opQueue.isEmpty() || m_step != Step::Running)
        return;
    m_opInFlight = true;
    const auto run = m_opQueue.takeFirst();
    run();
}

void YaleLink::finishOperation()
{
    m_opInFlight = false;
    runNextQueuedOperation();
}

namespace {
bool isOpResponseFor(Command opcode, const QByteArray &frame)
{
    return frame.size() >= 2 && uint8_t(frame[0]) == 0xBB && uint8_t(frame[1]) == uint8_t(opcode);
}
} // namespace

void YaleLink::lock(std::function<void(bool, const QString &)> callback)
{
    enqueue([this, callback]() {
        const QByteArray frame = LockProtocol::buildOperationCommand(Command::Lock);
        sendData(frame, [](const QByteArray &f) { return isOpResponseFor(Command::Lock, f); },
                 [this, callback](bool ok, const QByteArray &response, const QString &error) {
                     finishOperation();
                     if (!ok) {
                         if (callback)
                             callback(false, error);
                         return;
                     }
                     const auto parsed = LockProtocol::parseState(response);
                     // A successful op-response carries no state of its own
                     // (see LockProtocol::parseState) - success itself is
                     // the only proof needed that the lock is now Locked.
                     if (parsed.opSucceeded)
                         emit lockStatusChanged(int(LockProtocol::LockStatus::Locked));
                     if (callback)
                         callback(parsed.opSucceeded,
                                   parsed.opSucceeded
                                           ? QString()
                                           : tr("The lock reported an error (code 0x%1).")
                                                     .arg(parsed.opResultCode, 2, 16, QLatin1Char('0')));
                 },
                 kOperationTimeoutMs);
    });
}

void YaleLink::unlock(std::function<void(bool, const QString &)> callback)
{
    enqueue([this, callback]() {
        const QByteArray frame = LockProtocol::buildOperationCommand(Command::Unlock);
        sendData(frame, [](const QByteArray &f) { return isOpResponseFor(Command::Unlock, f); },
                 [this, callback](bool ok, const QByteArray &response, const QString &error) {
                     finishOperation();
                     if (!ok) {
                         if (callback)
                             callback(false, error);
                         return;
                     }
                     const auto parsed = LockProtocol::parseState(response);
                     if (parsed.opSucceeded)
                         emit lockStatusChanged(int(LockProtocol::LockStatus::Unlocked));
                     if (callback)
                         callback(parsed.opSucceeded,
                                   parsed.opSucceeded
                                           ? QString()
                                           : tr("The lock reported an error (code 0x%1).")
                                                     .arg(parsed.opResultCode, 2, 16, QLatin1Char('0')));
                 },
                 kOperationTimeoutMs);
    });
}

void YaleLink::refreshStatus()
{
    enqueue([this]() {
        // cmdByte 0x02 asks for door-and-lock status together.
        const QByteArray frame = LockProtocol::buildOperationCommand(Command::GetStatus, 0x02);
        sendData(frame, [](const QByteArray &f) { return isOpResponseFor(Command::GetStatus, f); },
                 [this](bool ok, const QByteArray &response, const QString &error) {
                     finishOperation();
                     if (!ok) {
                         emit trace(tr("Status request failed: %1").arg(error));
                         return;
                     }
                     // onDataNotify() already emitted lockStatusChanged/doorStatusChanged
                     // for this frame; nothing else to do.
                     Q_UNUSED(response);
                 },
                 kAckTimeoutMs);
    });
}

void YaleLink::fail(const QString &message)
{
    if (m_step == Step::Idle)
        return;
    close(false);
    emit failed(message);
}

void YaleLink::close(bool disconnectDevice)
{
    m_stepTimer.stop();
    m_secureTimeout.stop();
    m_dataTimeout.stop();
    ++m_generation;

    // StopNotify is not optional - see the BlueZ note in bluezcall.h's
    // sibling projects: a dropped signal hook without it still counts as a
    // notifying client, and the next session's notifications arrive doubled.
    if (m_secureNotifyHooked) {
        QDBusConnection::systemBus().disconnect(
                kBluezService, m_secureReadPath, kPropertiesInterface, QStringLiteral("PropertiesChanged"), this,
                SLOT(onSecureNotify(QString, QVariantMap, QStringList)));
        m_secureNotifyHooked = false;
    }
    if (m_dataNotifyHooked) {
        QDBusConnection::systemBus().disconnect(
                kBluezService, m_readPath, kPropertiesInterface, QStringLiteral("PropertiesChanged"), this,
                SLOT(onDataNotify(QString, QVariantMap, QStringList)));
        m_dataNotifyHooked = false;
    }
    if (m_secureSubscribed && !m_secureReadPath.isEmpty())
        BluezCall::call(m_secureReadPath, kCharInterface, QStringLiteral("StopNotify"));
    if (m_dataSubscribed && !m_readPath.isEmpty())
        BluezCall::call(m_readPath, kCharInterface, QStringLiteral("StopNotify"));
    m_secureSubscribed = false;
    m_dataSubscribed = false;

    if (m_deviceHooked) {
        QDBusConnection::systemBus().disconnect(
                kBluezService, m_devicePath, kPropertiesInterface, QStringLiteral("PropertiesChanged"), this,
                SLOT(onDevicePropertiesChanged(QString, QVariantMap, QStringList)));
        m_deviceHooked = false;
    }
    if (disconnectDevice && !m_devicePath.isEmpty())
        BluezCall::async(m_devicePath, kDeviceInterface, QStringLiteral("Disconnect"));

    m_pendingSecureCb = nullptr;
    m_pendingDataMatcher = nullptr;
    m_pendingDataCb = nullptr;
    m_opQueue.clear();
    m_opInFlight = false;

    m_writePath.clear();
    m_readPath.clear();
    m_secureWritePath.clear();
    m_secureReadPath.clear();
    m_devicePath.clear();
    m_connected = false;
    m_servicesResolved = false;
    m_step = Step::Idle;
}
