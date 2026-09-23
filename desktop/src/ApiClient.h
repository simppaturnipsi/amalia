#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QUrl>

class QNetworkReply;
class QNetworkRequest;

class ApiClient final : public QObject {
    Q_OBJECT
public:
    explicit ApiClient(const QUrl &baseUrl, QObject *parent = nullptr);
    void setAccessToken(const QString &token);
    void loadTasks();
    void createTask(const QString &name, const QString &number);
    void loadEntries(qint64 taskId, int afterSequence = 0);
    void appendEntry(qint64 taskId, const QJsonObject &payload);
    void exportTask(qint64 taskId, const QString &format);
    void deleteTask(qint64 taskId);

signals:
    void authenticationReady();
    void tasksLoaded(const QJsonArray &tasks);
    void taskCreated(const QJsonObject &task);
    void entriesLoaded(qint64 taskId, const QJsonArray &entries);
    void entryAccepted(const QString &idempotencyKey, const QJsonObject &entry);
    void entryFailed(const QString &idempotencyKey, const QString &message, bool retryable);
    void exportReady(const QByteArray &content, const QString &contentType, const QString &suggestedName);
    void taskDeleted(qint64 taskId);
    void requestFailed(const QString &message);
    void onlineChanged(bool online);

private:
    QNetworkRequest request(const QString &relativePath) const;
    QNetworkReply *sendJson(const QString &relativePath, const QJsonObject &object);
    static QString safeError(QNetworkReply *reply);

    QUrl m_baseUrl;
    QString m_accessToken;
    QNetworkAccessManager m_network;
    bool m_online{true};
};
