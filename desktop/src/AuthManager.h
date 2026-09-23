#pragma once

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QTcpServer>
#include <QUrl>

class QNetworkReply;

class AuthManager final : public QObject {
    Q_OBJECT
public:
    explicit AuthManager(const QJsonObject &oidc, QObject *parent = nullptr);
    void startDomainSso();
    void startInteractiveOidc();
    QString accessToken() const { return m_accessToken; }
    bool domainSessionDetected() const;

signals:
    void authenticated(const QString &accessToken, bool domainSso);
    void authenticationFailed(const QString &message);

private:
    void beginAuthorization();
    void exchangeCode(const QString &code, const QUrl &redirectUri);
    void processTokenReply(QNetworkReply *reply);
    static QString base64Url(const QByteArray &data);

    QJsonObject m_oidc;
    QNetworkAccessManager m_network;
    QTcpServer m_callback;
    QString m_verifier;
    QString m_state;
    QString m_accessToken;
};
