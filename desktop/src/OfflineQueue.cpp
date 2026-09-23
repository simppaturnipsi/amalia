#include "OfflineQueue.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

OfflineQueue::OfflineQueue(QObject *parent) : QObject(parent), m_connectionName(QUuid::createUuid().toString()) {}

OfflineQueue::~OfflineQueue() {
    if (m_database.isValid()) m_database.close();
    const auto name = m_connectionName;
    m_database = QSqlDatabase();
    QSqlDatabase::removeDatabase(name);
}

bool OfflineQueue::open(const QString &path) {
    m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_database.setDatabaseName(path);
    if (!m_database.open()) {
        m_error = m_database.lastError().text();
        return false;
    }
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral("PRAGMA journal_mode=WAL")) ||
        !query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS offline_entries("
                                  "idempotency_key TEXT PRIMARY KEY,task_id INTEGER NOT NULL,payload_json TEXT NOT NULL,"
                                  "status TEXT NOT NULL CHECK(status IN ('pending','sending','synced')),created_at TEXT NOT NULL,"
                                  "synced_at TEXT,server_entry_id INTEGER,last_error TEXT)")) ||
        !query.exec(QStringLiteral("UPDATE offline_entries SET status='pending' WHERE status='sending'"))) {
        m_error = query.lastError().text();
        return false;
    }
    emit countChanged(pendingCount());
    return true;
}

QString OfflineQueue::enqueue(qint64 taskId, const QString &text, const QString &workstationId) {
    const QString key = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QJsonObject payload{
        {QStringLiteral("idempotency_key"), key},
        {QStringLiteral("client_timestamp"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("workstation_id"), workstationId},
        {QStringLiteral("text"), text},
    };
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("INSERT INTO offline_entries(idempotency_key,task_id,payload_json,status,created_at) "
                                 "VALUES(?,?,?,'pending',?)"));
    query.addBindValue(key);
    query.addBindValue(taskId);
    query.addBindValue(QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)));
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    if (!query.exec()) return {};
    emit countChanged(pendingCount());
    return key;
}

QList<PendingEntry> OfflineQueue::pending(int limit) const {
    QList<PendingEntry> result;
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT idempotency_key,task_id,payload_json FROM offline_entries "
                                 "WHERE status='pending' ORDER BY created_at LIMIT ?"));
    query.addBindValue(limit);
    if (!query.exec()) return result;
    while (query.next()) {
        result.append({query.value(0).toString(), query.value(1).toLongLong(),
                       QJsonDocument::fromJson(query.value(2).toString().toUtf8()).object()});
    }
    return result;
}

void OfflineQueue::markSending(const QString &key) {
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("UPDATE offline_entries SET status='sending',last_error=NULL WHERE idempotency_key=?"));
    query.addBindValue(key);
    query.exec();
}

void OfflineQueue::markSynced(const QString &key, qint64 serverEntryId) {
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("UPDATE offline_entries SET status='synced',synced_at=?,server_entry_id=?,last_error=NULL "
                                 "WHERE idempotency_key=?"));
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    query.addBindValue(serverEntryId);
    query.addBindValue(key);
    query.exec();
    emit countChanged(pendingCount());
}

void OfflineQueue::markPending(const QString &key, const QString &error) {
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("UPDATE offline_entries SET status='pending',last_error=? WHERE idempotency_key=?"));
    query.addBindValue(error.left(500));
    query.addBindValue(key);
    query.exec();
    emit countChanged(pendingCount());
}

int OfflineQueue::pendingCount() const {
    QSqlQuery query(m_database);
    return query.exec(QStringLiteral("SELECT COUNT(*) FROM offline_entries WHERE status!='synced'")) && query.next()
        ? query.value(0).toInt() : 0;
}
