#pragma once

#include "lockentry.h"

#include <QString>
#include <QVector>

// Local SQLite storage for saved-lock *metadata* (name, BLE address, key
// index). None of this is secret - the offline handshake key bytes are
// stored separately in the Sailfish Secrets vault, keyed by LockEntry::id.
class LockStore
{
public:
    explicit LockStore(const QString &dbPath);

    bool open(QString *error);

    QVector<LockEntry> loadAll(QString *error) const;
    bool insert(const LockEntry &entry, QString *error);
    bool remove(const QString &id, QString *error);
    bool updateName(const QString &id, const QString &name, QString *error);
    int nextSortOrder() const;

private:
    QString m_dbPath;
    QString m_connectionName;
    mutable int m_maxSortOrder = -1;
};
