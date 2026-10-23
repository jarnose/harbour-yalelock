#pragma once

#include "bluezmanagedobjects.h"

#include <QObject>
#include <QString>
#include <QTimer>

// Finds Yale Access / August BLE locks through BlueZ's own D-Bus API
// (org.bluez.*), not Qt Bluetooth: Sailfish OS 5.1's SDK target has no
// pkgconfig(Qt5Bluetooth). Same approach as harbour-edgesync, harbour-ourasync
// and harbour-suuntosync.
//
// A lock is recognised by one of the two GATT service UUIDs its firmware
// advertises (see LockProtocol::SERVICE_UUID_V3/V4) when BlueZ has seen it
// already, and otherwise by its local BLE name: the lock advertises a
// 7-character name derived from its serial number (see util::serialToLocalName
// in the yalexs-ble project this protocol is ported from), which is not
// identifiable on sight but is at least short and stable - good enough to
// list and let the person match against their lock's label.
class YaleScanner : public QObject
{
    Q_OBJECT
public:
    struct Device
    {
        QString objectPath;
        QString address;
        QString name;
        bool paired = false;
        bool connected = false;
        int rssi = 0; // dBm, 0 when BlueZ has no reading
    };

    explicit YaleScanner(QObject *parent = nullptr);

    // Re-reads every device BlueZ knows and emits deviceUpdated() for locks.
    void refresh();

    // Scanning also polls refresh() every couple of seconds: RSSI and the
    // paired/connected flags change without a new object appearing.
    void startDiscovery();
    void stopDiscovery();
    bool discovering() const { return m_discovering; }

    // Object path of the BlueZ device with this MAC address, or an empty string.
    QString devicePathForAddress(const QString &address);

    static bool looksLikeLock(const QVariantMap &deviceProperties);

signals:
    void deviceUpdated(const YaleScanner::Device &device);
    void errorOccurred(const QString &message);

private slots:
    void onInterfacesAdded(const QDBusObjectPath &path, const BluezInterfaceMap &interfaces);

private:
    QString adapterPath();
    void emitIfLock(const QString &objectPath, const BluezInterfaceMap &interfaces);

    QString m_adapterPath;
    QTimer m_poll;
    bool m_discovering = false;
};

Q_DECLARE_METATYPE(YaleScanner::Device)
