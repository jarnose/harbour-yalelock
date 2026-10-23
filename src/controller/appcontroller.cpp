#include "appcontroller.h"

#include "../protocol/lockprotocol.h"

#include <algorithm>

#include <QDebug>
#include <QDir>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>

namespace {

QString locksDbPath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(dir).filePath(QStringLiteral("locks.sqlite"));
}

} // namespace

AppController::AppController(QObject *parent)
    : QObject(parent)
    , m_store(new LockStore(locksDbPath()))
    , m_vault(new SecretVault(this))
    , m_model(new LockListModel(this))
    , m_scanner(new YaleScanner(this))
    , m_activeLink(new YaleLink(this))
{
    QString error;
    if (!m_store->open(&error))
        emit errorOccurred(tr("Failed to open database: %1").arg(error));

    connect(m_scanner, &YaleScanner::deviceUpdated, this, [this](const YaleScanner::Device &d) {
        QVariantMap map;
        map.insert(QStringLiteral("name"), d.name);
        map.insert(QStringLiteral("address"), d.address);
        map.insert(QStringLiteral("rssi"), d.rssi);
        map.insert(QStringLiteral("paired"), d.paired);
        emit deviceFound(map);
    });
    connect(m_scanner, &YaleScanner::errorOccurred, this, &AppController::errorOccurred);

    connect(m_activeLink, &YaleLink::ready, this, [this]() {
        m_model->setConnectionState(m_activeLockId, QStringLiteral("connected"));
        // The handshake alone says nothing about the lock/door state - ask
        // for it explicitly rather than leaving the list/cover on
        // "unknown" until the next lock()/unlock() call.
        m_activeLink->refreshStatus();
        const auto queue = m_onReadyQueue;
        m_onReadyQueue.clear();
        for (const auto &fn : queue)
            fn();
    });
    connect(m_activeLink, &YaleLink::failed, this, [this](const QString &message) {
        if (!m_activeLockId.isEmpty()) {
            m_model->setConnectionState(m_activeLockId, QStringLiteral("error"));
            m_model->setLastError(m_activeLockId, message);
            m_model->setBusy(m_activeLockId, false);
        }
        m_onReadyQueue.clear();
        emit errorOccurred(message);
    });
    connect(m_activeLink, &YaleLink::linkLost, this, [this]() {
        if (!m_activeLockId.isEmpty()) {
            m_model->setConnectionState(m_activeLockId, QStringLiteral("idle"));
            m_model->setBusy(m_activeLockId, false);
        }
        m_onReadyQueue.clear();
    });
    connect(m_activeLink, &YaleLink::lockStatusChanged, this, [this](int status) {
        if (!m_activeLockId.isEmpty())
            m_model->setLockStatus(m_activeLockId,
                                   LockProtocol::lockStatusName(LockProtocol::LockStatus(status)));
    });
    connect(m_activeLink, &YaleLink::doorStatusChanged, this, [this](int status) {
        if (!m_activeLockId.isEmpty())
            m_model->setDoorStatus(m_activeLockId,
                                   LockProtocol::doorStatusName(LockProtocol::DoorStatus(status)));
    });
    // Goes to the journal (qDebug -> stderr -> journald for this app), not
    // to the UI: step-by-step BLE/handshake progress for diagnosing a real
    // lock on-device, where there is no debugger attached.
    connect(m_activeLink, &YaleLink::trace, this, [](const QString &line) {
        qDebug("YaleLink: %s", qUtf8Printable(line));
    });

    loadLocks();

    m_vault->ensureCollection([this](bool ok, const QString &err) {
        if (!ok)
            emit errorOccurred(tr("Could not open the secure key vault: %1").arg(err));
        m_ready = true;
        emit readyChanged();
    });
}

AppController::~AppController()
{
    teardownActiveLink();
}

void AppController::loadLocks()
{
    QString error;
    const QVector<LockEntry> entries = m_store->loadAll(&error);
    if (!error.isEmpty())
        emit errorOccurred(tr("Could not load saved locks: %1").arg(error));
    m_model->setLocks(entries);
}

QString AppController::newLockId()
{
    return QUuid::createUuid().toString().remove(QLatin1Char('{')).remove(QLatin1Char('}'));
}

bool AppController::addLock(const QString &name, const QString &bleAddress, const QString &offlineKeyHex,
                            int keyIndex)
{
    const QByteArray key = QByteArray::fromHex(offlineKeyHex.toLatin1());
    if (key.size() != 16) {
        emit errorOccurred(tr("The offline key must be exactly 32 hex characters (16 bytes)."));
        return false;
    }
    if (keyIndex < 0 || keyIndex > 255) {
        emit errorOccurred(tr("The key index must be between 0 and 255."));
        return false;
    }
    if (bleAddress.trimmed().isEmpty() || name.trimmed().isEmpty()) {
        emit errorOccurred(tr("Name and Bluetooth address are required."));
        return false;
    }

    LockEntry entry;
    entry.id = newLockId();
    entry.name = name.trimmed();
    entry.bleAddress = bleAddress.trimmed();
    entry.keyIndex = keyIndex;
    entry.sortOrder = m_store->nextSortOrder();

    QString error;
    if (!m_store->insert(entry, &error)) {
        emit errorOccurred(tr("Could not save the lock: %1").arg(error));
        return false;
    }
    m_model->addLock(entry);

    m_vault->storeSecret(entry.id, key, [this, entry](bool ok, const QString &err) {
        if (!ok)
            emit errorOccurred(tr("Saved the lock, but could not store its key securely: %1").arg(err));
    });
    return true;
}

void AppController::removeLock(const QString &id)
{
    if (m_activeLockId == id)
        teardownActiveLink();

    QString error;
    if (!m_store->remove(id, &error)) {
        emit errorOccurred(tr("Could not remove the lock: %1").arg(error));
        return;
    }
    m_vault->deleteSecret(id, [this](bool ok, const QString &err) {
        if (!ok)
            emit errorOccurred(tr("Could not remove the lock's stored key: %1").arg(err));
    });
    m_model->removeLock(id);
}

void AppController::renameLock(const QString &id, const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return;
    QString error;
    if (!m_store->updateName(id, trimmed, &error)) {
        emit errorOccurred(tr("Could not rename the lock: %1").arg(error));
        return;
    }
    m_model->updateName(id, trimmed);
}

void AppController::startScan()
{
    m_scanner->startDiscovery();
    m_scanning = true;
    emit scanningChanged();
}

void AppController::stopScan()
{
    m_scanner->stopDiscovery();
    m_scanning = false;
    emit scanningChanged();
}

void AppController::ensureConnectedThen(const QString &id, std::function<void()> whenReady)
{
    if (m_activeLockId == id && m_activeLink->isReady()) {
        whenReady();
        return;
    }
    if (m_activeLockId == id) {
        // Already connecting (or re-handshaking) with this lock.
        m_onReadyQueue.append(std::move(whenReady));
        return;
    }

    QString error;
    const QVector<LockEntry> entries = m_store->loadAll(&error);
    const auto it = std::find_if(entries.constBegin(), entries.constEnd(),
                                  [&id](const LockEntry &e) { return e.id == id; });
    if (it == entries.constEnd()) {
        emit errorOccurred(tr("Unknown lock."));
        return;
    }
    const LockEntry entry = *it;

    if (!m_activeLockId.isEmpty())
        m_model->setConnectionState(m_activeLockId, QStringLiteral("idle"));

    m_onReadyQueue.clear();
    m_activeLockId = id;
    m_model->setConnectionState(id, QStringLiteral("connecting"));
    m_onReadyQueue.append(std::move(whenReady));

    m_vault->loadSecret(id, [this, id, entry](bool ok, const QByteArray &data, const QString &err) {
        if (m_activeLockId != id)
            return; // superseded by a request for a different lock meanwhile
        if (!ok) {
            m_model->setConnectionState(id, QStringLiteral("error"));
            m_model->setLastError(id, err);
            m_onReadyQueue.clear();
            emit errorOccurred(tr("Could not read the saved key for \"%1\": %2").arg(entry.name, err));
            return;
        }
        resolveDevicePathThen(entry, [this, id, entry, data](const QString &devicePath) {
            if (m_activeLockId != id)
                return; // superseded by a request for a different lock meanwhile
            if (devicePath.isEmpty()) {
                m_model->setConnectionState(id, QStringLiteral("error"));
                const QString message =
                        tr("\"%1\" (%2) hasn't been seen by Bluetooth yet. Make sure it's nearby and "
                           "powered, then try again.")
                                .arg(entry.name, entry.bleAddress);
                m_model->setLastError(id, message);
                m_onReadyQueue.clear();
                emit errorOccurred(message);
                return;
            }
            m_activeLink->open(devicePath, data, quint8(entry.keyIndex));
        });
    });
}

void AppController::resolveDevicePathThen(const LockEntry &entry, std::function<void(const QString &)> callback)
{
    const QString immediate = m_scanner->devicePathForAddress(entry.bleAddress);
    if (!immediate.isEmpty()) {
        callback(immediate);
        return;
    }

    // BlueZ hasn't seen this address yet (e.g. since the last reboot) - run
    // one short discovery burst and check again before giving up. Doesn't
    // touch the scanning property / scanningChanged: this is a background
    // retry, not the user-visible scan on the Add Lock page, and leaving
    // that alone matters if the ScanPage happens to be open at the same time.
    const bool wasAlreadyDiscovering = m_scanner->discovering();
    m_scanner->startDiscovery();
    QTimer::singleShot(4000, this, [this, entry, callback, wasAlreadyDiscovering]() {
        const QString resolved = m_scanner->devicePathForAddress(entry.bleAddress);
        if (!wasAlreadyDiscovering)
            m_scanner->stopDiscovery();
        callback(resolved);
    });
}

void AppController::teardownActiveLink()
{
    m_activeLink->close(false);
    if (!m_activeLockId.isEmpty())
        m_model->setConnectionState(m_activeLockId, QStringLiteral("idle"));
    m_activeLockId.clear();
    m_onReadyQueue.clear();
}

void AppController::connectLock(const QString &id)
{
    ensureConnectedThen(id, []() {});
}

void AppController::disconnectLock(const QString &id)
{
    if (m_activeLockId == id)
        teardownActiveLink();
}

void AppController::lockDoor(const QString &id)
{
    m_model->setBusy(id, true);
    ensureConnectedThen(id, [this, id]() {
        m_activeLink->lock([this, id](bool ok, const QString &error) {
            m_model->setBusy(id, false);
            if (!ok) {
                m_model->setLastError(id, error);
                emit errorOccurred(error);
            } else {
                m_model->setLastError(id, QString());
            }
        });
    });
}

void AppController::unlockDoor(const QString &id)
{
    m_model->setBusy(id, true);
    ensureConnectedThen(id, [this, id]() {
        m_activeLink->unlock([this, id](bool ok, const QString &error) {
            m_model->setBusy(id, false);
            if (!ok) {
                m_model->setLastError(id, error);
                emit errorOccurred(error);
            } else {
                m_model->setLastError(id, QString());
            }
        });
    });
}

void AppController::refreshStatus(const QString &id)
{
    ensureConnectedThen(id, [this]() { m_activeLink->refreshStatus(); });
}
