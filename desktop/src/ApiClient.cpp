#include "ApiClient.h"

#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>

ApiClient::ApiClient(const QUrl &baseUrl, QObject *parent) : QObject(parent), m_baseUrl(baseUrl) {}

void ApiClient::setAccessToken(const QString &token) {
    m_accessToken = token;
    emit authenticationReady();
}

QNetworkRequest ApiClient::request(const QString &relativePath) const {
    const QUrl url = m_baseUrl.resolved(QUrl(relativePath));
    QNetworkRequest result(url);
    result.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    result.setRawHeader("Accept", "application/json");
    if (!m_accessToken.isEmpty()) result.setRawHeader("Authorization", "Bearer " + m_accessToken.toUtf8());
    return result;
}

QNetworkReply *ApiClient::sendJson(const QString &relativePath, const QJsonObject &object) {
    auto *reply = m_network.post(request(relativePath), QJsonDocument(object).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::sslErrors, reply, [reply] { reply->abort(); });
    return reply;
}

QString ApiClient::safeError(QNetworkReply *reply) {
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    return status ? tr("HTTP-virhe %1").arg(status) : tr("Verkkoyhteys ei ole käytettävissä");
}

void ApiClient::loadTasks() {
    auto *reply = m_network.get(request(QStringLiteral("tasks")));
    connect(reply, &QNetworkReply::sslErrors, reply, [reply] { reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        const bool ok = reply->error() == QNetworkReply::NoError;
        if (ok) emit tasksLoaded(QJsonDocument::fromJson(reply->readAll()).array());
        else emit requestFailed(safeError(reply));
        if (m_online != ok) { m_online = ok; emit onlineChanged(ok); }
        reply->deleteLater();
    });
}

void ApiClient::createTask(const QString &name, const QString &number) {
    auto *reply = sendJson(QStringLiteral("tasks"), {{QStringLiteral("name"), name}, {QStringLiteral("task_number"), number}});
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (reply->error() == QNetworkReply::NoError)
            emit taskCreated(QJsonDocument::fromJson(reply->readAll()).object());
        else emit requestFailed(safeError(reply));
        reply->deleteLater();
    });
}

void ApiClient::loadEntries(qint64 taskId, int afterSequence) {
    auto *reply = m_network.get(request(QStringLiteral("tasks/%1/entries?after_sequence=%2").arg(taskId).arg(afterSequence)));
    connect(reply, &QNetworkReply::sslErrors, reply, [reply] { reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, taskId] {
        if (reply->error() == QNetworkReply::NoError)
            emit entriesLoaded(taskId, QJsonDocument::fromJson(reply->readAll()).array());
        else emit requestFailed(safeError(reply));
        reply->deleteLater();
    });
}

void ApiClient::appendEntry(qint64 taskId, const QJsonObject &payload) {
    const QString key = payload.value(QStringLiteral("idempotency_key")).toString();
    auto *reply = sendJson(QStringLiteral("tasks/%1/entries").arg(taskId), payload);
    connect(reply, &QNetworkReply::finished, this, [this, reply, key] {
        if (reply->error() == QNetworkReply::NoError) {
            emit entryAccepted(key, QJsonDocument::fromJson(reply->readAll()).object());
            if (!m_online) { m_online = true; emit onlineChanged(true); }
        } else {
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const bool retryable = status == 0 || status == 429 || status >= 500;
            emit entryFailed(key, safeError(reply), retryable);
            if (status == 0 && m_online) { m_online = false; emit onlineChanged(false); }
        }
        reply->deleteLater();
    });
}

void ApiClient::exportTask(qint64 taskId, const QString &format) {
    auto *reply = m_network.post(request(QStringLiteral("tasks/%1/export/%2").arg(taskId).arg(format)), QByteArray{});
    connect(reply, &QNetworkReply::sslErrors, reply, [reply] { reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (reply->error() == QNetworkReply::NoError) {
            emit exportReady(reply->readAll(), reply->header(QNetworkRequest::ContentTypeHeader).toString(),
                             reply->rawHeader("Content-Disposition"));
        } else emit requestFailed(safeError(reply));
        reply->deleteLater();
    });
}

void ApiClient::deleteTask(qint64 taskId) {
    auto *reply = m_network.deleteResource(request(QStringLiteral("tasks/%1?confirm=true").arg(taskId)));
    connect(reply, &QNetworkReply::sslErrors, reply, [reply] { reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, taskId] {
        if (reply->error() == QNetworkReply::NoError) emit taskDeleted(taskId);
        else emit requestFailed(safeError(reply));
        reply->deleteLater();
    });
}
