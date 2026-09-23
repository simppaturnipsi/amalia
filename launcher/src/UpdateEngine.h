#pragma once

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QUrl>

struct InstalledVersion {
    QString version;
    QString executable;
    bool isValid() const { return !version.isEmpty() && !executable.isEmpty(); }
};

struct UpdateMetadata {
    QString version;
    QString channel;
    QString platform;
    QString file;
    QByteArray sha256;
    QByteArray signature;
    QString minimumSupportedVersion;
    bool mandatory{false};
    bool isValid() const;
    QByteArray signedPayload() const;
};

class UpdateEngine final : public QObject {
    Q_OBJECT
public:
    explicit UpdateEngine(const QJsonObject &configuration, QObject *parent = nullptr);
    bool run();
    QString errorString() const { return m_error; }

private:
    QByteArray fetch(const QUrl &url, bool *ok);
    UpdateMetadata checkRepository(bool *ok);
    InstalledVersion readVersionFile(const QString &name) const;
    bool writeVersionFile(const QString &name, const InstalledVersion &version);
    bool verifyPackage(const QByteArray &package, const UpdateMetadata &metadata) const;
    bool installPackage(const QByteArray &package, const UpdateMetadata &metadata, InstalledVersion *installed);
    bool launchAndConfirm(const InstalledVersion &version, bool requireAcknowledgement);
    bool rollbackAndLaunch();
    QString absoluteExecutable(const InstalledVersion &version) const;
    QString platformName() const;

    QJsonObject m_configuration;
    QNetworkAccessManager m_network;
    QString m_root;
    QString m_error;
};
