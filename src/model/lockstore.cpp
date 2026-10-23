#include "lockstore.h"

#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

LockStore::LockStore(const QString &dbPath)
    : m_dbPath(dbPath)
    , m_connectionName(QStringLiteral("yalelock_locks"))
{
}

bool LockStore::open(QString *error)
{
    QDir().mkpath(QFileInfo(m_dbPath).absolutePath());

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    db.setDatabaseName(m_dbPath);
    if (!db.open()) {
        if (error)
            *error = db.lastError().text();
        return false;
    }

    QSqlQuery q(db);
    const bool ok = q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS locks ("
            "  id TEXT PRIMARY KEY,"
            "  name TEXT NOT NULL,"
            "  ble_address TEXT NOT NULL,"
            "  key_index INTEGER NOT NULL,"
            "  sort_order INTEGER NOT NULL"
            ")"));
    if (!ok) {
        if (error)
            *error = q.lastError().text();
        return false;
    }
    return true;
}

QVector<LockEntry> LockStore::loadAll(QString *error) const
{
    QVector<LockEntry> result;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral(
                "SELECT id, name, ble_address, key_index, sort_order FROM locks ORDER BY sort_order ASC"))) {
        if (error)
            *error = q.lastError().text();
        return result;
    }
    int maxOrder = -1;
    while (q.next()) {
        LockEntry e;
        e.id = q.value(0).toString();
        e.name = q.value(1).toString();
        e.bleAddress = q.value(2).toString();
        e.keyIndex = q.value(3).toInt();
        e.sortOrder = q.value(4).toInt();
        maxOrder = qMax(maxOrder, e.sortOrder);
        result.append(e);
    }
    m_maxSortOrder = maxOrder;
    return result;
}

int LockStore::nextSortOrder() const
{
    if (m_maxSortOrder < 0) {
        QString error;
        loadAll(&error);
    }
    return m_maxSortOrder + 1;
}

bool LockStore::insert(const LockEntry &entry, QString *error)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
            "INSERT INTO locks (id, name, ble_address, key_index, sort_order) VALUES (?, ?, ?, ?, ?)"));
    q.addBindValue(entry.id);
    q.addBindValue(entry.name);
    q.addBindValue(entry.bleAddress);
    q.addBindValue(entry.keyIndex);
    q.addBindValue(entry.sortOrder);
    if (!q.exec()) {
        if (error)
            *error = q.lastError().text();
        return false;
    }
    m_maxSortOrder = qMax(m_maxSortOrder, entry.sortOrder);
    return true;
}

bool LockStore::remove(const QString &id, QString *error)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral("DELETE FROM locks WHERE id = ?"));
    q.addBindValue(id);
    if (!q.exec()) {
        if (error)
            *error = q.lastError().text();
        return false;
    }
    if (q.numRowsAffected() == 0) {
        if (error)
            *error = QStringLiteral("no row matched id '%1'").arg(id);
        return false;
    }
    return true;
}

bool LockStore::updateName(const QString &id, const QString &name, QString *error)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(QStringLiteral("UPDATE locks SET name = ? WHERE id = ?"));
    q.addBindValue(name);
    q.addBindValue(id);
    if (!q.exec()) {
        if (error)
            *error = q.lastError().text();
        return false;
    }
    return true;
}
