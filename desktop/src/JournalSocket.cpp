#include "JournalSocket.h"

#include <QJsonDocument>
#include <QNetworkRequest>

JournalSocket::JournalSocket(const QUrl &baseUrl, QObject *parent) : QObject(parent), m_baseUrl(baseUrl) {
    m_reconnect.setSingleShot(true);
    connect(&m_reconnect, &QTimer::timeout, this, &JournalSocket::connectNow);
    connect(&m_socket, &QWebSocket::connected, this, [this] {
        m_reconnectSeconds = 1;
        emit connectedChanged(true);
    });
    connect(&m_socket, &QWebSocket::disconnected, this, [this] {
        emit connectedChanged(false);
        if (m_taskId > 0) {
            m_reconnect.start(m_reconnectSeconds * 1000);
            m_reconnectSeconds = qMin(30, m_reconnectSeconds * 2);
        }
    });
    connect(&m_socket, &QWebSocket::textMessageReceived, this, [this](const QString &message) {
        const auto object = QJsonDocument::fromJson(message.toUtf8()).object();
        if (object.value(QStringLiteral("type")) == QStringLiteral("log_entry.created"))
            emit entryReceived(object.value(QStringLiteral("entry")).toObject());
    });
    connect(&m_socket, &QWebSocket::sslErrors, this, [this] { m_socket.close(); });
}

void JournalSocket::watchTask(qint64 taskId) {
    if (m_taskId == taskId && m_socket.state() == QAbstractSocket::ConnectedState) return;
    m_socket.close();
    m_taskId = taskId;
    m_reconnectSeconds = 1;
    connectNow();
}

void JournalSocket::connectNow() {
    if (m_taskId <= 0) return;
    QNetworkRequest request(m_baseUrl.resolved(QUrl(QStringLiteral("tasks/%1").arg(m_taskId))));
    if (!m_accessToken.isEmpty()) request.setRawHeader("Authorization", "Bearer " + m_accessToken.toUtf8());
    m_socket.open(request);
}

void JournalSocket::stop() {
    m_taskId = 0;
    m_reconnect.stop();
    m_socket.close();
}
