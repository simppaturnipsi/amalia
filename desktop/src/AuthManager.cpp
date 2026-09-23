#include "AuthManager.h"

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QHostAddress>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRandomGenerator>
#include <QTcpSocket>
#include <QUrlQuery>

AuthManager::AuthManager(const QJsonObject &oidc, QObject *parent)
    : QObject(parent), m_oidc(oidc) {
    connect(&m_callback, &QTcpServer::newConnection, this, [this] {
        auto *socket = m_callback.nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
            const QByteArray request = socket->readAll();
            const auto firstLine = request.left(request.indexOf("\r\n"));
            const auto parts = firstLine.split(' ');
            if (parts.size() < 2) return;
            const QUrl callback(QStringLiteral("http://127.0.0.1") + QString::fromUtf8(parts.at(1)));
            const QUrlQuery query(callback);
            const QString state = query.queryItemValue(QStringLiteral("state"));
            const QString code = query.queryItemValue(QStringLiteral("code"));
            const bool valid = !code.isEmpty() && state == m_state;
            const quint16 callbackPort = m_callback.serverPort();
            const QByteArray body = valid ? "Amalia-kirjautuminen onnistui. Voit sulkea tämän ikkunan."
                                          : "Amalia-kirjautuminen epäonnistui.";
            socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\nConnection: close\r\n\r\n" + body);
            socket->disconnectFromHost();
            m_callback.close();
            if (!valid) {
                emit authenticationFailed(tr("OIDC-vastauksen state/code ei kelpaa."));
                return;
            }
            const QUrl redirect(QStringLiteral("http://127.0.0.1:%1/callback").arg(callbackPort));
            exchangeCode(code, redirect);
        });
    });
}

QString AuthManager::base64Url(const QByteArray &data) {
    return QString::fromLatin1(data.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

bool AuthManager::domainSessionDetected() const {
#ifdef Q_OS_WIN
    return !qEnvironmentVariable("USERDNSDOMAIN").isEmpty();
#else
    return QProcess::execute(QStringLiteral("klist"), {QStringLiteral("-s")}) == 0;
#endif
}

void AuthManager::startDomainSso() {
    if (!domainSessionDetected()) {
        emit authenticationFailed(tr("Voimassa olevaa Kerberos-istuntoa ei löytynyt."));
        return;
    }
    // Requests carry no stored password/token; the HTTPS reverse proxy may use Kerberos Negotiate SSO.
    emit authenticated({}, true);
}

void AuthManager::startInteractiveOidc() {
    if (m_callback.isListening()) {
        emit authenticationFailed(tr("Selainkirjautuminen on jo käynnissä."));
        return;
    }
    beginAuthorization();
}

void AuthManager::beginAuthorization() {
    if (!m_callback.listen(QHostAddress::LocalHost, 0)) {
        emit authenticationFailed(tr("Paikallista OIDC-paluuporttia ei voitu avata."));
        return;
    }
    QByteArray verifierBytes(48, '\0');
    for (auto &byte : verifierBytes)
        byte = static_cast<char>(QRandomGenerator::global()->bounded(256));
    QByteArray stateBytes(24, '\0');
    for (auto &byte : stateBytes)
        byte = static_cast<char>(QRandomGenerator::global()->bounded(256));
    m_verifier = base64Url(verifierBytes);
    m_state = base64Url(stateBytes);
    const QString challenge = base64Url(QCryptographicHash::hash(m_verifier.toUtf8(), QCryptographicHash::Sha256));
    const QUrl redirect(QStringLiteral("http://127.0.0.1:%1/callback").arg(m_callback.serverPort()));
    QUrl authorize(m_oidc.value(QStringLiteral("authorizationEndpoint")).toString());
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    query.addQueryItem(QStringLiteral("client_id"), m_oidc.value(QStringLiteral("clientId")).toString());
    query.addQueryItem(QStringLiteral("redirect_uri"), redirect.toString());
    query.addQueryItem(QStringLiteral("scope"), m_oidc.value(QStringLiteral("scope")).toString());
    query.addQueryItem(QStringLiteral("state"), m_state);
    query.addQueryItem(QStringLiteral("code_challenge"), challenge);
    query.addQueryItem(QStringLiteral("code_challenge_method"), QStringLiteral("S256"));
    authorize.setQuery(query);
    QDesktopServices::openUrl(authorize);
}

void AuthManager::exchangeCode(const QString &code, const QUrl &redirectUri) {
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("authorization_code"));
    form.addQueryItem(QStringLiteral("client_id"), m_oidc.value(QStringLiteral("clientId")).toString());
    form.addQueryItem(QStringLiteral("code"), code);
    form.addQueryItem(QStringLiteral("redirect_uri"), redirectUri.toString());
    form.addQueryItem(QStringLiteral("code_verifier"), m_verifier);
    QNetworkRequest request(QUrl(m_oidc.value(QStringLiteral("tokenEndpoint")).toString()));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    auto *reply = m_network.post(request, form.query(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::sslErrors, reply, [reply] { reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [this, reply] { processTokenReply(reply); });
}

void AuthManager::processTokenReply(QNetworkReply *reply) {
    const auto data = reply->readAll();
    const auto error = reply->error();
    reply->deleteLater();
    if (error != QNetworkReply::NoError) {
        emit authenticationFailed(tr("OIDC-tokenin vaihto epäonnistui."));
        return;
    }
    const auto object = QJsonDocument::fromJson(data).object();
    m_accessToken = object.value(QStringLiteral("access_token")).toString();
    if (m_accessToken.isEmpty()) {
        emit authenticationFailed(tr("OIDC-palvelin ei palauttanut access tokenia."));
        return;
    }
    emit authenticated(m_accessToken, false);
}
