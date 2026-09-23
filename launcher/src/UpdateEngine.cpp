#include "UpdateEngine.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <QVersionNumber>
#include <QStringList>
#include <algorithm>
#include <cstdio>

#include <openssl/evp.h>
#include <openssl/pem.h>

bool UpdateMetadata::isValid() const {
    const QStringList values{version, channel, platform, file, minimumSupportedVersion};
    const bool safeValues = std::all_of(values.begin(), values.end(), [](const QString &value) {
        return !value.contains('\n') && !value.contains('\r');
    });
    return safeValues && !version.isEmpty() && (channel == QStringLiteral("stable") || channel == QStringLiteral("test")) &&
           (platform == QStringLiteral("windows") || platform == QStringLiteral("ubuntu")) &&
           !file.isEmpty() && sha256.size() == 32 && !signature.isEmpty();
}

QByteArray UpdateMetadata::signedPayload() const {
    return QByteArray("AMALIA-UPDATE-V1\n") + version.toUtf8() + '\n' + channel.toUtf8() + '\n' +
        platform.toUtf8() + '\n' + file.toUtf8() + '\n' + sha256.toHex() + '\n' +
        minimumSupportedVersion.toUtf8() + '\n' + (mandatory ? "1\n" : "0\n");
}

UpdateEngine::UpdateEngine(const QJsonObject &configuration, QObject *parent)
    : QObject(parent), m_configuration(configuration) {
    const QString configuredRoot = configuration.value(QStringLiteral("installRoot")).toString(QStringLiteral(".."));
    if (configuredRoot == QStringLiteral("@USER_DATA@"))
        m_root = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    else
        m_root = QDir::isAbsolutePath(configuredRoot) ? configuredRoot
            : QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(configuredRoot);
}

QString UpdateEngine::platformName() const {
#ifdef Q_OS_WIN
    return QStringLiteral("windows");
#else
    return QStringLiteral("ubuntu");
#endif
}

QByteArray UpdateEngine::fetch(const QUrl &url, bool *ok) {
    *ok = false;
    const bool localAllowed = m_configuration.value(QStringLiteral("allowLocalTestRepository")).toBool(false);
    if (url.scheme() == QStringLiteral("file")) {
        if (!localAllowed) { m_error = tr("Paikallinen päivitysrepository ei ole sallittu."); return {}; }
        QFile file(url.toLocalFile());
        if (!file.open(QIODevice::ReadOnly)) { m_error = file.errorString(); return {}; }
        *ok = true;
        return file.readAll();
    }
    if (url.scheme() != QStringLiteral("https")) {
        m_error = tr("Päivitysrepositoryn pitää käyttää HTTPS:ää.");
        return {};
    }
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "AmaliaLauncher/" AMALIA_LAUNCHER_VERSION);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    auto *reply = m_network.get(request);
    bool tlsFailure = false;
    bool sizeFailure = false;
    QByteArray data;
    const qint64 maximum = url.path().contains(QStringLiteral("/metadata/"))
        ? 1024 * 1024 : static_cast<qint64>(m_configuration.value(QStringLiteral("maxDownloadBytes")).toDouble(1073741824));
    connect(reply, &QNetworkReply::sslErrors, reply, [reply, &tlsFailure] { tlsFailure = true; reply->abort(); });
    connect(reply, &QNetworkReply::readyRead, reply, [reply, &data, &sizeFailure, maximum] {
        data += reply->readAll();
        if (data.size() > maximum) { sizeFailure = true; reply->abort(); }
    });
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    connect(&timeout, &QTimer::timeout, reply, &QNetworkReply::abort);
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timeout.start(15000);
    loop.exec();
    data += reply->readAll();
    const auto networkError = reply->error();
    reply->deleteLater();
    if (tlsFailure) { m_error = tr("Päivityspalvelimen TLS-varmenne ei kelpaa."); return {}; }
    if (sizeFailure) { m_error = tr("Päivityslataus ylittää sallitun koon."); return {}; }
    if (networkError != QNetworkReply::NoError) { m_error = tr("Päivityspalvelimeen ei saada yhteyttä."); return {}; }
    *ok = true;
    return data;
}

UpdateMetadata UpdateEngine::checkRepository(bool *ok) {
    const QString channel = m_configuration.value(QStringLiteral("channel")).toString(QStringLiteral("stable"));
    const QUrl base(m_configuration.value(QStringLiteral("repositoryBaseUrl")).toString());
    const QUrl metadataUrl = base.resolved(QUrl(QStringLiteral("metadata/%1-%2.json").arg(channel, platformName())));
    const auto data = fetch(metadataUrl, ok);
    if (!*ok) return {};
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        *ok = false; m_error = tr("Päivitysmetadata on virheellinen."); return {};
    }
    const auto object = document.object();
    UpdateMetadata metadata{
        object["version"].toString(), object["channel"].toString(), object["platform"].toString(),
        object["file"].toString(), QByteArray::fromHex(object["sha256"].toString().toLatin1()),
        QByteArray::fromBase64(object["signature"].toString().toLatin1()),
        object["minimum_supported_version"].toString(), object["mandatory"].toBool(false),
    };
    if (!metadata.isValid() || metadata.channel != channel || metadata.platform != platformName()) {
        *ok = false; m_error = tr("Päivitysmetadata ei vastaa kanavaa tai alustaa.");
    }
    return metadata;
}

InstalledVersion UpdateEngine::readVersionFile(const QString &name) const {
    QFile file(QDir(m_root).filePath(name));
    if (!file.open(QIODevice::ReadOnly)) return {};
    const auto object = QJsonDocument::fromJson(file.readAll()).object();
    return {object["version"].toString(), object["executable"].toString()};
}

bool UpdateEngine::writeVersionFile(const QString &name, const InstalledVersion &version) {
    QDir().mkpath(m_root);
    QSaveFile file(QDir(m_root).filePath(name));
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(QJsonDocument(QJsonObject{{"version", version.version}, {"executable", version.executable}}).toJson());
    return file.commit();
}

bool UpdateEngine::verifyPackage(const QByteArray &package, const UpdateMetadata &metadata) const {
    if (QCryptographicHash::hash(package, QCryptographicHash::Sha256) != metadata.sha256) return false;
    QString keyPath = m_configuration.value(QStringLiteral("publicKey")).toString();
    if (!QDir::isAbsolutePath(keyPath)) keyPath = QDir(QCoreApplication::applicationDirPath()).filePath(keyPath);
    QFile keyFile(keyPath);
    if (!keyFile.open(QIODevice::ReadOnly)) return false;
    const QByteArray keyBytes = keyFile.readAll();
    BIO *bio = BIO_new_mem_buf(keyBytes.constData(), keyBytes.size());
    EVP_PKEY *key = bio ? PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr) : nullptr;
    BIO_free(bio);
    if (!key) return false;
    EVP_MD_CTX *context = EVP_MD_CTX_new();
    const QByteArray signedPayload = metadata.signedPayload();
    const bool valid = context && EVP_DigestVerifyInit(context, nullptr, nullptr, nullptr, key) == 1 &&
        EVP_DigestVerify(context, reinterpret_cast<const unsigned char *>(metadata.signature.constData()), metadata.signature.size(),
                         reinterpret_cast<const unsigned char *>(signedPayload.constData()), signedPayload.size()) == 1;
    EVP_MD_CTX_free(context);
    EVP_PKEY_free(key);
    return valid;
}

bool UpdateEngine::installPackage(const QByteArray &package, const UpdateMetadata &metadata, InstalledVersion *installed) {
    QDir root(m_root);
    root.mkpath(QStringLiteral("versions"));
    const QString staging = root.filePath(QStringLiteral("versions/.staging-") + metadata.version);
    QDir(staging).removeRecursively();
    QDir().mkpath(staging);
    QTemporaryFile archive(root.filePath(QStringLiteral("amalia-update-XXXXXX.zip")));
    archive.setAutoRemove(true);
    if (!archive.open() || archive.write(package) != package.size() || !archive.flush()) return false;
#ifdef Q_OS_WIN
    const QString archivePath = QDir::toNativeSeparators(archive.fileName()).replace("'", "''");
    const QString stagingPath = QDir::toNativeSeparators(staging).replace("'", "''");
    const QString command = QStringLiteral(
        "Add-Type -AssemblyName System.IO.Compression.FileSystem;"
        "$z=[IO.Compression.ZipFile]::OpenRead('%1');"
        "$bad=$z.Entries|Where-Object { [IO.Path]::IsPathRooted($_.FullName) -or $_.FullName -match '(^|[\\/])\\.\\.([\\/]|$)' };"
        "if($bad){$z.Dispose();exit 2};$z.Dispose();"
        "Expand-Archive -LiteralPath '%1' -DestinationPath '%2' -Force").arg(archivePath, stagingPath);
    const int extracted = QProcess::execute(QStringLiteral("powershell.exe"), {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"), QStringLiteral("-Command"), command});
    const QString executable = QStringLiteral("amalia-desktop.exe");
#else
    QProcess listing;
    listing.start(QStringLiteral("/usr/bin/unzip"), {QStringLiteral("-Z1"), archive.fileName()});
    listing.waitForFinished(30000);
    bool unsafe = listing.exitStatus() != QProcess::NormalExit || listing.exitCode() != 0;
    for (const auto &name : QString::fromUtf8(listing.readAllStandardOutput()).split('\n', Qt::SkipEmptyParts)) {
        const QString cleaned = QDir::cleanPath(name);
        if (name.startsWith('/') || name.contains('\\') || cleaned == QStringLiteral("..") || cleaned.startsWith(QStringLiteral("../"))) {
            unsafe = true; break;
        }
    }
    const int extracted = unsafe ? 2 : QProcess::execute(QStringLiteral("/usr/bin/unzip"), {QStringLiteral("-q"), archive.fileName(), QStringLiteral("-d"), staging});
    const QString executable = QStringLiteral("amalia-desktop");
#endif
    if (extracted != 0 || !QFile::exists(QDir(staging).filePath(executable))) {
        QDir(staging).removeRecursively(); return false;
    }
    const QString destination = root.filePath(QStringLiteral("versions/") + metadata.version);
    if (QDir(destination).exists()) QDir(destination).removeRecursively();
    if (!QDir().rename(staging, destination)) return false;
    *installed = {metadata.version, QStringLiteral("versions/%1/%2").arg(metadata.version, executable)};
    return true;
}

QString UpdateEngine::absoluteExecutable(const InstalledVersion &version) const {
    return QDir(m_root).absoluteFilePath(version.executable);
}

bool UpdateEngine::launchAndConfirm(const InstalledVersion &version, bool requireAcknowledgement) {
    const QString executable = absoluteExecutable(version);
    if (!QFile::exists(executable)) { m_error = tr("Asennettua Amalia-versiota ei löydy."); return false; }
    const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString ack = QDir(m_root).filePath(QStringLiteral("startup-") + token + QStringLiteral(".ack"));
    QFile::remove(ack);
    qint64 pid = 0;
    if (!QProcess::startDetached(executable, {QStringLiteral("--startup-token"), token,
                                              QStringLiteral("--startup-ack"), ack}, QFileInfo(executable).absolutePath(), &pid)) {
        m_error = tr("Amaliaa ei voitu käynnistää."); return false;
    }
    if (!requireAcknowledgement) return true;
    QElapsedTimer elapsed;
    elapsed.start();
    const int timeout = m_configuration.value(QStringLiteral("startupTimeoutSeconds")).toInt(20) * 1000;
    while (elapsed.elapsed() < timeout) {
        QFile file(ack);
        if (file.open(QIODevice::ReadOnly) && file.readAll() == token.toUtf8()) { QFile::remove(ack); return true; }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        QThread::msleep(100);
    }
#ifdef Q_OS_WIN
    QProcess::execute(QStringLiteral("taskkill.exe"), {QStringLiteral("/PID"), QString::number(pid), QStringLiteral("/T"), QStringLiteral("/F")});
#else
    QProcess::execute(QStringLiteral("/bin/kill"), {QStringLiteral("-TERM"), QString::number(pid)});
#endif
    m_error = tr("Uusi versio ei vahvistanut käynnistymistään.");
    return false;
}

bool UpdateEngine::rollbackAndLaunch() {
    const auto previous = readVersionFile(QStringLiteral("previous.json"));
    if (!previous.isValid()) return false;
    writeVersionFile(QStringLiteral("current.json"), previous);
    return launchAndConfirm(previous, false);
}

bool UpdateEngine::run() {
    auto current = readVersionFile(QStringLiteral("current.json"));
    if (!current.isValid()) {
        const QString bootstrap = m_configuration.value(QStringLiteral("bootstrapExecutable")).toString();
        if (!bootstrap.isEmpty()) {
            const QString executable = QDir::isAbsolutePath(bootstrap) ? bootstrap
                : QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(bootstrap);
            if (QFile::exists(executable))
                current = {m_configuration.value(QStringLiteral("bootstrapVersion")).toString(), executable};
        }
    }
    bool repositoryAvailable = false;
    const auto metadata = checkRepository(&repositoryAvailable);
    if (!repositoryAvailable) return current.isValid() && launchAndConfirm(current, false);
    const bool newer = !current.isValid() || QVersionNumber::fromString(metadata.version) > QVersionNumber::fromString(current.version);
    if (!newer) return launchAndConfirm(current, false);
    bool downloaded = false;
    const QUrl base(m_configuration.value(QStringLiteral("repositoryBaseUrl")).toString());
    const auto package = fetch(base.resolved(QUrl(metadata.file)), &downloaded);
    if (!downloaded || !verifyPackage(package, metadata)) {
        m_error = tr("Päivityspaketin eheys tai allekirjoitus ei kelpaa.");
        return current.isValid() && launchAndConfirm(current, false);
    }
    InstalledVersion installed;
    if (!installPackage(package, metadata, &installed)) {
        m_error = tr("Päivityspaketin asennus epäonnistui.");
        return current.isValid() && launchAndConfirm(current, false);
    }
    if (current.isValid()) writeVersionFile(QStringLiteral("previous.json"), current);
    if (!writeVersionFile(QStringLiteral("current.json"), installed)) return rollbackAndLaunch();
    if (launchAndConfirm(installed, true)) return true;
    return rollbackAndLaunch();
}
