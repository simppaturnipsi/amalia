#include "CredentialStore.h"

#include <QMetaObject>

#ifdef AMALIA_HAVE_QTKEYCHAIN
#if __has_include(<qt6keychain/keychain.h>)
#include <qt6keychain/keychain.h>
#else
#include <qtkeychain/keychain.h>
#endif
#endif

CredentialStore::CredentialStore(QObject *parent) : QObject(parent) {}

bool CredentialStore::isAvailable() const {
#ifdef AMALIA_HAVE_QTKEYCHAIN
    return true;
#else
    return false;
#endif
}

void CredentialStore::readSecret(const QString &key) {
#ifdef AMALIA_HAVE_QTKEYCHAIN
    auto *job = new QKeychain::ReadPasswordJob(QStringLiteral("fi.vapepa.amalia"), this);
    job->setKey(key);
    connect(job, &QKeychain::Job::finished, this, [this, job, key] {
        emit secretRead(key, job->error() == QKeychain::NoError ? job->textData() : QString(),
                        job->error() == QKeychain::NoError ? QString() : job->errorString());
    });
    job->start();
#else
    QMetaObject::invokeMethod(this, [this, key] {
        emit secretRead(key, {}, tr("QtKeychain ei ole käytettävissä; tietoa ei tallenneta turvattomasti."));
    }, Qt::QueuedConnection);
#endif
}

void CredentialStore::writeSecret(const QString &key, const QString &value) {
#ifdef AMALIA_HAVE_QTKEYCHAIN
    auto *job = new QKeychain::WritePasswordJob(QStringLiteral("fi.vapepa.amalia"), this);
    job->setKey(key);
    job->setTextData(value);
    connect(job, &QKeychain::Job::finished, this, [this, job, key] {
        emit writeFinished(key, job->error() == QKeychain::NoError ? QString() : job->errorString());
    });
    job->start();
#else
    Q_UNUSED(value)
    QMetaObject::invokeMethod(this, [this, key] {
        emit writeFinished(key, tr("QtKeychain ei ole käytettävissä; tietoa ei tallennettu."));
    }, Qt::QueuedConnection);
#endif
}

void CredentialStore::deleteSecret(const QString &key) {
#ifdef AMALIA_HAVE_QTKEYCHAIN
    auto *job = new QKeychain::DeletePasswordJob(QStringLiteral("fi.vapepa.amalia"), this);
    job->setKey(key);
    connect(job, &QKeychain::Job::finished, this, [this, job, key] {
        emit writeFinished(key, job->error() == QKeychain::NoError ? QString() : job->errorString());
    });
    job->start();
#else
    QMetaObject::invokeMethod(this, [this, key] { emit writeFinished(key, {}); }, Qt::QueuedConnection);
#endif
}
