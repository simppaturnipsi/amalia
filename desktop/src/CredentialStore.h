#pragma once

#include <QObject>

class CredentialStore final : public QObject {
    Q_OBJECT
public:
    explicit CredentialStore(QObject *parent = nullptr);
    bool isAvailable() const;
    void readSecret(const QString &key);
    void writeSecret(const QString &key, const QString &value);
    void deleteSecret(const QString &key);

signals:
    void secretRead(const QString &key, const QString &value, const QString &error);
    void writeFinished(const QString &key, const QString &error);
};
