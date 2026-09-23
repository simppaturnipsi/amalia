#pragma once

#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QWebSocket>

class JournalSocket final : public QObject {
    Q_OBJECT
public:
    explicit JournalSocket(const QUrl &baseUrl, QObject *parent = nullptr);
    void setAccessToken(const QString &token) { m_accessToken = token; }
    void watchTask(qint64 taskId);
    void stop();

signals:
    void connectedChanged(bool connected);
    void entryReceived(const QJsonObject &entry);

private:
    void connectNow();
    QUrl m_baseUrl;
    QString m_accessToken;
    qint64 m_taskId{0};
    int m_reconnectSeconds{1};
    QWebSocket m_socket;
    QTimer m_reconnect;
};
