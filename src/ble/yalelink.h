#pragma once

#include "../protocol/lockcipher.h"
#include "../protocol/lockprotocol.h"

#include <QByteArray>
#include <QDBusObjectPath>
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantMap>
#include <functional>

// The BLE link to one Yale/August lock over BlueZ's D-Bus API, merging
// transport (connect, discover the 4 characteristics, write, notify) and the
// LockProtocol session on top of it - unlike harbour-edgesync's EdgeLink,
// which keeps those apart because GFDI is a real multi-frame transport, this
// protocol is a plain request/response exchange on two independent channels
// (secure and data), so one state machine for both is simpler to follow.
//
// No pairing/bonding step: the lock does not require an encrypted BLE link,
// because LockProtocol's own AES handshake (the offline key) is the real
// authentication - see lockprotocol.h.
class YaleLink : public QObject
{
    Q_OBJECT
public:
    explicit YaleLink(QObject *parent = nullptr);
    ~YaleLink() override;

    // Connects to the lock at devicePath and runs the AES handshake with its
    // offline key/index (see lockprotocol.h). Emits ready() or failed().
    void open(const QString &devicePath, const QByteArray &offlineKey16, quint8 keyIndex);
    void close(bool disconnectDevice);

    bool isReady() const;

    // One request in flight at a time; a second call before the first's
    // callback fires is queued and run after it.
    void lock(std::function<void(bool ok, const QString &error)> callback);
    void unlock(std::function<void(bool ok, const QString &error)> callback);
    void refreshStatus();

signals:
    void ready();
    void failed(const QString &message);
    void linkLost();
    void lockStatusChanged(int status);  // LockProtocol::LockStatus
    void doorStatusChanged(int status);  // LockProtocol::DoorStatus
    void trace(const QString &line);

private slots:
    void onDevicePropertiesChanged(const QString &interface, const QVariantMap &changed,
                                   const QStringList &invalidated);
    void onSecureNotify(const QString &interface, const QVariantMap &changed, const QStringList &invalidated);
    void onDataNotify(const QString &interface, const QVariantMap &changed, const QStringList &invalidated);
    void onStepTimeout();

private:
    enum class Step { Idle, Connecting, WaitServices, Subscribing, Handshaking, Running };

    void enterStep(Step step, int timeoutMs);
    static QString stepName(Step step);
    void proceed();
    QString discoverCharacteristics();
    void subscribeBoth();
    void startHandshake();
    void fail(const QString &message);
    QVariant deviceProperty(const QString &name) const;

    // Writes an unencrypted 18-byte frame to the secure channel, encrypting
    // bytes 0-15 with m_secureCipher first. onResponse gets the next frame on
    // that channel whose checksum validates - used only during the
    // handshake, which never has more than one exchange in flight and (per
    // LockProtocol) has nothing else to match against.
    void sendSecure(const QByteArray &plainFrame18, std::function<void(const QByteArray &)> onResponse);

    // Same on the data channel, with m_dataCipher, but a command ack (0xAA)
    // arrives before the real op-response (0xBB) for LOCK/UNLOCK/GETSTATUS -
    // matcher decides which decrypted frame answers this command; frames that
    // fail the checksum or the matcher are ignored and the wait continues
    // until timeoutMs.
    void sendData(const QByteArray &plainFrame18, std::function<bool(const QByteArray &)> matcher,
                  std::function<void(bool ok, const QByteArray &response, const QString &error)> onDone,
                  int timeoutMs);

    // Queues a data-channel exchange: only one runs at a time (the lock
    // itself serializes them), queued the way yalexs-ble's Session does with
    // its asyncio.Lock.
    void enqueue(std::function<void()> run);
    void runNextQueuedOperation();
    void finishOperation();

    QString m_devicePath;
    QString m_writePath, m_readPath, m_secureWritePath, m_secureReadPath;
    bool m_deviceHooked = false;
    bool m_secureNotifyHooked = false, m_dataNotifyHooked = false;
    bool m_secureSubscribed = false, m_dataSubscribed = false;
    bool m_connected = false, m_servicesResolved = false;
    Step m_step = Step::Idle;
    QTimer m_stepTimer;
    quint64 m_generation = 0;

    SecureChannelCipher m_secureCipher;
    DataChannelCipher m_dataCipher;
    QByteArray m_offlineKey;
    quint8 m_keyIndex = 0;
    QByteArray m_handshakeRandom; // 16 bytes generated at handshake start

    std::function<void(const QByteArray &)> m_pendingSecureCb;
    QTimer m_secureTimeout;
    std::function<bool(const QByteArray &)> m_pendingDataMatcher;
    std::function<void(bool, const QByteArray &, const QString &)> m_pendingDataCb;
    QTimer m_dataTimeout;

    // lock()/unlock()/refreshStatus() calls queued behind one already running.
    QList<std::function<void()>> m_opQueue;
    bool m_opInFlight = false;
};
