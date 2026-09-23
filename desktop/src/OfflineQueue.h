#pragma once

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QSqlDatabase>

struct PendingEntry {
    QString idempotencyKey;
    qint64 taskId;
    QJsonObject payload;
};

class OfflineQueue final : public QObject {
    Q_OBJECT
public:
    explicit OfflineQueue(QObject *parent = nullptr);
    ~OfflineQueue() override;
    bool open(const QString &path);
    QString enqueue(qint64 taskId, const QString &text, const QString &workstationId);
    QList<PendingEntry> pending(int limit = 100) const;
    void markSending(const QString &key);
    void markSynced(const QString &key, qint64 serverEntryId);
    void markPending(const QString &key, const QString &error = {});
    int pendingCount() const;
    QString errorString() const { return m_error; }

signals:
    void countChanged(int pending);

private:
    QSqlDatabase m_database;
    QString m_connectionName;
    QString m_error;
};
