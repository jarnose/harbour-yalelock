#pragma once

#include "../ble/yalelink.h"
#include "../ble/yalescanner.h"
#include "../model/lockentry.h"
#include "../model/locklistmodel.h"
#include "../model/lockstore.h"
#include "../secrets/secretvault.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <functional>

// QML-facing façade tying together local metadata storage (LockStore), the
// Sailfish Secrets vault (SecretVault, for the offline handshake keys), the
// BLE scanner (YaleScanner) and one active lock connection (YaleLink) at a
// time - a phone talks to one lock at a time, so there is no need to keep
// several connections open together.
class AppController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject *lockModel READ lockModelObject CONSTANT)
    Q_PROPERTY(bool ready READ isReady NOTIFY readyChanged)
    Q_PROPERTY(bool scanning READ isScanning NOTIFY scanningChanged)

public:
    explicit AppController(QObject *parent = nullptr);
    ~AppController() override;

    QObject *lockModelObject() const { return m_model; }
    bool isReady() const { return m_ready; }
    bool isScanning() const { return m_scanning; }

    // offlineKeyHex: 32 hex characters (16 bytes). keyIndex: 0-255.
    Q_INVOKABLE bool addLock(const QString &name, const QString &bleAddress, const QString &offlineKeyHex,
                              int keyIndex);
    Q_INVOKABLE void removeLock(const QString &id);
    Q_INVOKABLE void renameLock(const QString &id, const QString &name);

    Q_INVOKABLE void startScan();
    Q_INVOKABLE void stopScan();

    // Connects to the lock and runs the handshake; emits connectionFailed()
    // or a lockModel connectionState change to "connected".
    Q_INVOKABLE void connectLock(const QString &id);
    Q_INVOKABLE void disconnectLock(const QString &id);

    // Each connects first if needed.
    Q_INVOKABLE void lockDoor(const QString &id);
    Q_INVOKABLE void unlockDoor(const QString &id);
    Q_INVOKABLE void refreshStatus(const QString &id);

signals:
    void readyChanged();
    void scanningChanged();
    void errorOccurred(const QString &message);
    // name, address, rssi, paired - QML appends these to its own ListModel
    // for the add-lock BLE picker rather than this app owning a second
    // C++-side model just for a transient scan list.
    void deviceFound(const QVariantMap &device);

private:
    void loadLocks();
    void ensureConnectedThen(const QString &id, std::function<void()> whenReady);
    // Resolves entry.bleAddress to BlueZ's own D-Bus object path (what
    // YaleLink::open() actually needs - see its call site). BlueZ only
    // knows this once it has seen the device at least once; if it hasn't,
    // this runs one short discovery burst and retries before giving up, so
    // a lock that has simply fallen out of BlueZ's cache (e.g. since boot)
    // doesn't need a manual trip through the Add Lock scanner first.
    void resolveDevicePathThen(const LockEntry &entry, std::function<void(const QString &devicePath)> callback);
    void teardownActiveLink();
    static QString newLockId();

    LockStore *m_store;
    SecretVault *m_vault;
    LockListModel *m_model;
    YaleScanner *m_scanner;
    bool m_ready = false;
    bool m_scanning = false;

    YaleLink *m_activeLink = nullptr;
    QString m_activeLockId;
    // Queued "once connected, do this" callbacks (lockDoor()/unlockDoor()
    // called before the handshake finishes).
    QList<std::function<void()>> m_onReadyQueue;
};
