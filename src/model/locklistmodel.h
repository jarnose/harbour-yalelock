#pragma once

#include "lockentry.h"

#include <QAbstractListModel>
#include <QVariantMap>
#include <QVector>

// Presentation model for the saved-lock list. Transient fields (connection
// state, last-known lock/door status, "operation in progress") live only
// here - AppController updates them from YaleLink's signals - while the
// durable fields come from LockStore via setLocks()/addLock()/removeLock().
class LockListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        BleAddressRole,
        KeyIndexRole,
        ConnectionStateRole, // "idle" | "connecting" | "connected" | "error"
        LockStatusRole,      // LockProtocol::lockStatusName(), or "" if unknown
        DoorStatusRole,      // LockProtocol::doorStatusName(), or "" if unknown
        BusyRole,            // true while a lock()/unlock() is in flight
        LastErrorRole,
    };
    Q_ENUM(Roles)

    explicit LockListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setLocks(const QVector<LockEntry> &locks);
    void addLock(const LockEntry &entry);
    void removeLock(const QString &id);
    void updateName(const QString &id, const QString &name);

    void setConnectionState(const QString &id, const QString &state);
    void setLockStatus(const QString &id, const QString &status);
    void setDoorStatus(const QString &id, const QString &status);
    void setBusy(const QString &id, bool busy);
    void setLastError(const QString &id, const QString &error);

    Q_INVOKABLE QVariantMap get(int row) const;
    int indexOfId(const QString &id) const;

signals:
    void countChanged();

private:
    struct Row
    {
        LockEntry entry;
        QString connectionState = QStringLiteral("idle");
        QString lockStatus;
        QString doorStatus;
        bool busy = false;
        QString lastError;
    };

    void emitRowChanged(int row, int role);

    QVector<Row> m_rows;
};
