#include "locklistmodel.h"

LockListModel::LockListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int LockListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_rows.size();
}

QVariant LockListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return QVariant();

    const Row &row = m_rows.at(index.row());
    switch (role) {
    case IdRole: return row.entry.id;
    case NameRole: return row.entry.name;
    case BleAddressRole: return row.entry.bleAddress;
    case KeyIndexRole: return row.entry.keyIndex;
    case ConnectionStateRole: return row.connectionState;
    case LockStatusRole: return row.lockStatus;
    case DoorStatusRole: return row.doorStatus;
    case BusyRole: return row.busy;
    case LastErrorRole: return row.lastError;
    default: return QVariant();
    }
}

QHash<int, QByteArray> LockListModel::roleNames() const
{
    return {
        { IdRole, "id" },
        { NameRole, "name" },
        { BleAddressRole, "bleAddress" },
        { KeyIndexRole, "keyIndex" },
        { ConnectionStateRole, "connectionState" },
        { LockStatusRole, "lockStatus" },
        { DoorStatusRole, "doorStatus" },
        { BusyRole, "busy" },
        { LastErrorRole, "lastError" },
    };
}

void LockListModel::setLocks(const QVector<LockEntry> &locks)
{
    beginResetModel();
    m_rows.clear();
    m_rows.reserve(locks.size());
    for (const LockEntry &e : locks) {
        Row r;
        r.entry = e;
        m_rows.append(r);
    }
    endResetModel();
    emit countChanged();
}

void LockListModel::addLock(const LockEntry &entry)
{
    beginInsertRows(QModelIndex(), m_rows.size(), m_rows.size());
    Row r;
    r.entry = entry;
    m_rows.append(r);
    endInsertRows();
    emit countChanged();
}

void LockListModel::removeLock(const QString &id)
{
    const int row = indexOfId(id);
    if (row < 0)
        return;
    beginRemoveRows(QModelIndex(), row, row);
    m_rows.removeAt(row);
    endRemoveRows();
    emit countChanged();
}

void LockListModel::updateName(const QString &id, const QString &name)
{
    const int row = indexOfId(id);
    if (row < 0)
        return;
    m_rows[row].entry.name = name;
    emitRowChanged(row, NameRole);
}

void LockListModel::setConnectionState(const QString &id, const QString &state)
{
    const int row = indexOfId(id);
    if (row < 0)
        return;
    m_rows[row].connectionState = state;
    emitRowChanged(row, ConnectionStateRole);
}

void LockListModel::setLockStatus(const QString &id, const QString &status)
{
    const int row = indexOfId(id);
    if (row < 0)
        return;
    m_rows[row].lockStatus = status;
    emitRowChanged(row, LockStatusRole);
}

void LockListModel::setDoorStatus(const QString &id, const QString &status)
{
    const int row = indexOfId(id);
    if (row < 0)
        return;
    m_rows[row].doorStatus = status;
    emitRowChanged(row, DoorStatusRole);
}

void LockListModel::setBusy(const QString &id, bool busy)
{
    const int row = indexOfId(id);
    if (row < 0)
        return;
    m_rows[row].busy = busy;
    emitRowChanged(row, BusyRole);
}

void LockListModel::setLastError(const QString &id, const QString &error)
{
    const int row = indexOfId(id);
    if (row < 0)
        return;
    m_rows[row].lastError = error;
    emitRowChanged(row, LastErrorRole);
}

QVariantMap LockListModel::get(int row) const
{
    QVariantMap map;
    if (row < 0 || row >= m_rows.size())
        return map;
    const QHash<int, QByteArray> roles = roleNames();
    for (auto it = roles.constBegin(); it != roles.constEnd(); ++it)
        map.insert(QString::fromUtf8(it.value()), data(index(row), it.key()));
    return map;
}

int LockListModel::indexOfId(const QString &id) const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).entry.id == id)
            return i;
    }
    return -1;
}

void LockListModel::emitRowChanged(int row, int role)
{
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, { role });
}
