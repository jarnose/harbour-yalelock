#include "yalescanner.h"

#include "bluezcall.h"
#include "../protocol/lockprotocol.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>

namespace {

const QString kBluezService = QStringLiteral("org.bluez");
const QString kObjectManagerInterface = QStringLiteral("org.freedesktop.DBus.ObjectManager");
const QString kAdapterInterface = QStringLiteral("org.bluez.Adapter1");
const QString kDeviceInterface = QStringLiteral("org.bluez.Device1");

} // namespace

YaleScanner::YaleScanner(QObject *parent)
    : QObject(parent)
{
    registerBluezManagedObjectTypes();
    qRegisterMetaType<YaleScanner::Device>();

    QDBusConnection::systemBus().connect(
            kBluezService, QString(), kObjectManagerInterface, QStringLiteral("InterfacesAdded"), this,
            SLOT(onInterfacesAdded(QDBusObjectPath, BluezInterfaceMap)));

    m_poll.setInterval(2000);
    connect(&m_poll, &QTimer::timeout, this, &YaleScanner::refresh);
}

bool YaleScanner::looksLikeLock(const QVariantMap &props)
{
    const QStringList uuids = props.value(QStringLiteral("UUIDs")).toStringList();
    for (const QString &uuid : uuids) {
        if (uuid.compare(QLatin1String(LockProtocol::SERVICE_UUID_V3), Qt::CaseInsensitive) == 0
            || uuid.compare(QLatin1String(LockProtocol::SERVICE_UUID_V4), Qt::CaseInsensitive) == 0)
            return true;
    }
    // A lock's advertised local name is a 7-character code derived from its
    // serial number (see yalexs-ble's util.serial_to_local_name) - not
    // identifiable on sight, but short and visually distinct from everything
    // else BlueZ turns up, so list any 7-character name too.
    const QString name = props.value(QStringLiteral("Alias"), props.value(QStringLiteral("Name"))).toString();
    return name.length() == 7;
}

QString YaleScanner::adapterPath()
{
    if (!m_adapterPath.isEmpty())
        return m_adapterPath;

    QDBusInterface manager(kBluezService, QStringLiteral("/"), kObjectManagerInterface,
                           QDBusConnection::systemBus());
    QDBusReply<BluezObjectMap> reply = manager.call(QStringLiteral("GetManagedObjects"));
    if (!reply.isValid()) {
        emit errorOccurred(tr("Could not reach Bluetooth (org.bluez): %1").arg(reply.error().message()));
        return QString();
    }
    const BluezObjectMap objects = reply.value();
    for (auto it = objects.constBegin(); it != objects.constEnd(); ++it) {
        if (it.value().contains(kAdapterInterface)) {
            m_adapterPath = it.key().path();
            break;
        }
    }
    if (m_adapterPath.isEmpty())
        emit errorOccurred(tr("No Bluetooth adapter found"));
    return m_adapterPath;
}

void YaleScanner::emitIfLock(const QString &objectPath, const BluezInterfaceMap &interfaces)
{
    if (!interfaces.contains(kDeviceInterface))
        return;
    const QVariantMap props = interfaces.value(kDeviceInterface);
    if (!looksLikeLock(props))
        return;

    Device d;
    d.objectPath = objectPath;
    d.address = props.value(QStringLiteral("Address")).toString();
    d.name = props.value(QStringLiteral("Alias"), props.value(QStringLiteral("Name"), d.address)).toString();
    d.paired = props.value(QStringLiteral("Paired")).toBool();
    d.connected = props.value(QStringLiteral("Connected")).toBool();
    d.rssi = props.value(QStringLiteral("RSSI")).toInt();
    emit deviceUpdated(d);
}

void YaleScanner::refresh()
{
    QDBusInterface manager(kBluezService, QStringLiteral("/"), kObjectManagerInterface,
                           QDBusConnection::systemBus());
    QDBusReply<BluezObjectMap> reply = manager.call(QStringLiteral("GetManagedObjects"));
    if (!reply.isValid()) {
        emit errorOccurred(tr("Could not list Bluetooth devices: %1").arg(reply.error().message()));
        return;
    }
    const BluezObjectMap objects = reply.value();
    for (auto it = objects.constBegin(); it != objects.constEnd(); ++it)
        emitIfLock(it.key().path(), it.value());
}

void YaleScanner::onInterfacesAdded(const QDBusObjectPath &path, const BluezInterfaceMap &interfaces)
{
    emitIfLock(path.path(), interfaces);
}

void YaleScanner::startDiscovery()
{
    const QString path = adapterPath();
    if (path.isEmpty())
        return;
    m_discovering = true;
    m_poll.start();

    auto *watcher = new QDBusPendingCallWatcher(
            BluezCall::async(path, kAdapterInterface, QStringLiteral("StartDiscovery")), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        const QDBusPendingReply<> reply = *w;
        // InProgress means someone else (Settings) is already scanning, which
        // serves us just as well.
        if (reply.isError() && reply.error().name() != QLatin1String("org.bluez.Error.InProgress"))
            emit errorOccurred(tr("Could not start scanning: %1").arg(reply.error().message()));
        w->deleteLater();
    });
}

void YaleScanner::stopDiscovery()
{
    m_poll.stop();
    if (!m_discovering)
        return;
    m_discovering = false;
    const QString path = adapterPath();
    if (path.isEmpty())
        return;
    BluezCall::async(path, kAdapterInterface, QStringLiteral("StopDiscovery"));
}

QString YaleScanner::devicePathForAddress(const QString &address)
{
    QDBusInterface manager(kBluezService, QStringLiteral("/"), kObjectManagerInterface,
                           QDBusConnection::systemBus());
    QDBusReply<BluezObjectMap> reply = manager.call(QStringLiteral("GetManagedObjects"));
    if (!reply.isValid())
        return QString();
    const BluezObjectMap objects = reply.value();
    for (auto it = objects.constBegin(); it != objects.constEnd(); ++it) {
        const QVariantMap props = it.value().value(kDeviceInterface);
        if (!props.isEmpty()
            && props.value(QStringLiteral("Address")).toString().compare(address, Qt::CaseInsensitive) == 0)
            return it.key().path();
    }
    return QString();
}
