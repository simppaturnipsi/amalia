#include "MainWindow.h"
#include "ApiClient.h"
#include "AuthManager.h"
#include "CredentialStore.h"
#include "JournalSocket.h"
#include "JournalWidget.h"
#include "OfflineQueue.h"
#include "WebAppWindow.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFormLayout>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QScreen>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWindow>

#include <memory>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle(tr("Amalia"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/amalia.svg")));
    resize(1440, 900);
}

MainWindow::~MainWindow() = default;

QJsonObject MainWindow::loadRuntimeConfig() const {
    const QStringList candidates{
        QCoreApplication::applicationDirPath() + QStringLiteral("/config/amalia.json"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/../share/amalia/config/amalia.json"),
        QStringLiteral(":/amalia.json"),
    };
    for (const auto &path : candidates) {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            const auto document = QJsonDocument::fromJson(file.readAll());
            if (document.isObject()) return document.object();
        }
    }
    return {};
}

bool MainWindow::initialize() {
    QString externalCatalog;
    for (const auto &candidate : {
             QCoreApplication::applicationDirPath() + QStringLiteral("/config/apps.json"),
             QCoreApplication::applicationDirPath() + QStringLiteral("/../share/amalia/config/apps.json")}) {
        if (QFile::exists(candidate)) { externalCatalog = candidate; break; }
    }
    if (!m_catalog.load(externalCatalog)) {
        QMessageBox::critical(this, tr("Amalia"), m_catalog.errorString());
        return false;
    }
    m_runtime = loadRuntimeConfig();
    if (m_runtime.isEmpty()) {
        QMessageBox::critical(this, tr("Amalia"), tr("Ajonaikaista asetusta amalia.json ei löytynyt."));
        return false;
    }
    m_workspace = new QMdiArea(this);
    m_workspace->setViewMode(QMdiArea::SubWindowView);
    m_workspace->setOption(QMdiArea::DontMaximizeSubWindowOnActivation, true);
    setCentralWidget(m_workspace);

    m_credentials = new CredentialStore(this);
    m_auth = new AuthManager(m_runtime.value(QStringLiteral("oidc")).toObject(), this);
    m_api = new ApiClient(QUrl(m_runtime.value(QStringLiteral("apiBaseUrl")).toString()), this);
    m_offline = new OfflineQueue(this);
    const auto dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    if (!m_offline->open(dataDir + QStringLiteral("/offline-queue.db"))) {
        QMessageBox::critical(this, tr("Amalia"), tr("Offline-jonoa ei voitu avata: %1").arg(m_offline->errorString()));
        return false;
    }
    m_socket = new JournalSocket(QUrl(m_runtime.value(QStringLiteral("webSocketBaseUrl")).toString()), this);
    connect(m_auth, &AuthManager::authenticated, this, [this](const QString &token, bool domainSso) {
        m_api->setAccessToken(token);
        m_socket->setAccessToken(token);
        statusBar()->showMessage(domainSso ? tr("AD/Kerberos SSO käytössä") : tr("OIDC-kirjautuminen valmis"), 5000);
    });
    connect(m_auth, &AuthManager::authenticationFailed, this, [this](const QString &message) {
        statusBar()->showMessage(message);
        QMessageBox::warning(this, tr("Kirjautuminen"), message);
    });
    buildApplicationsMenu();
    auto *settingsMenu = menuBar()->addMenu(tr("Asetukset"));
    settingsMenu->addAction(tr("Kirjaudu…"), this, &MainWindow::showLoginDialog);
    settingsMenu->addSeparator();
    settingsMenu->addAction(tr("Ohto-tunnukset (secure storage)…"), this, &MainWindow::configureOhtoCredentials);
    settingsMenu->addAction(tr("Unohda minut…"), this, &MainWindow::forgetUser);
    settingsMenu->addSeparator();
    settingsMenu->addAction(tr("Tietoja…"), this, &MainWindow::showAbout);
    restoreWorkspace();
    QTimer::singleShot(0, this, &MainWindow::showLoginDialog);
    return true;
}

void MainWindow::showLoginDialog() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Kirjautuminen"));
    dialog.setModal(true);
    dialog.setMinimumWidth(480);

    auto *layout = new QVBoxLayout(&dialog);
    auto *description = new QLabel(
        tr("Amalia ei hae tallennettuja kirjautumistietoja automaattisesti. "
           "Valitse kirjautumistapa tai jatka ilman kirjautumista."),
        &dialog);
    description->setWordWrap(true);
    layout->addWidget(description);

    auto *browserLogin = new QRadioButton(
        tr("Selainkirjautuminen (OIDC + PKCE)"), &dialog);
    browserLogin->setToolTip(
        tr("Avaa järjestelmän selaimen. Amalia ei vastaanota eikä tallenna salasanaasi."));
    browserLogin->setChecked(true);
    layout->addWidget(browserLogin);

    auto *domainLogin = new QRadioButton(
        tr("Toimialuekirjautuminen (olemassa oleva Kerberos-istunto)"), &dialog);
    domainLogin->setToolTip(
        tr("Käyttää työasemalla jo olevaa Kerberos-istuntoa vasta valinnan jälkeen."));
    layout->addWidget(domainLogin);

    auto *buttons = new QDialogButtonBox(&dialog);
    auto *loginButton = buttons->addButton(tr("Kirjaudu"), QDialogButtonBox::AcceptRole);
    buttons->addButton(tr("Jatka ilman kirjautumista"), QDialogButtonBox::RejectRole);
    loginButton->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) {
        statusBar()->showMessage(tr("Jatketaan ilman kirjautumista."), 5000);
        return;
    }

    if (domainLogin->isChecked())
        m_auth->startDomainSso();
    else
        m_auth->startInteractiveOidc();
}

void MainWindow::showAbout() {
    const auto safeEndpoint = [](const QJsonObject &runtime, const QString &key) {
        return QUrl(runtime.value(key).toString()).toDisplayString(QUrl::RemoveUserInfo);
    };
    const QString platform = QStringLiteral("%1 (%2 %3)")
        .arg(QSysInfo::prettyProductName(), QSysInfo::kernelType(), QSysInfo::kernelVersion());
    const QString secureStorage = m_credentials && m_credentials->isAvailable()
        ? tr("Käytettävissä (QtKeychain)")
        : tr("Ei käytettävissä");
    const QString dataDirectory = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);

    QMessageBox dialog(this);
    dialog.setWindowTitle(tr("Tietoja Amaliasta"));
    dialog.setIcon(QMessageBox::Information);
    dialog.setText(tr("Amalia"));
    dialog.setInformativeText(
        tr("Amalia-versio: %1\n"
           "Qt-versio: %2\n"
           "Käyttöjärjestelmä: %3\n"
           "Työasema: %4\n"
           "Arkkitehtuuri: %5\n"
           "Credential storage: %6")
            .arg(QCoreApplication::applicationVersion(),
                 QString::fromLatin1(qVersion()),
                 platform,
                 QSysInfo::machineHostName(),
                 QSysInfo::currentCpuArchitecture(),
                 secureStorage));
    dialog.setDetailedText(
        tr("API: %1\nWebSocket: %2\nPaikallinen data: %3")
            .arg(safeEndpoint(m_runtime, QStringLiteral("apiBaseUrl")),
                 safeEndpoint(m_runtime, QStringLiteral("webSocketBaseUrl")),
                 dataDirectory));
    dialog.exec();
}

void MainWindow::forgetUser() {
    if (!m_credentials || !m_credentials->isAvailable()) {
        QMessageBox::warning(this, tr("Unohda minut"),
            tr("QtKeychain/OS credential storage ei ole käytettävissä."));
        return;
    }
    const auto answer = QMessageBox::question(
        this,
        tr("Unohda minut"),
        tr("Poistetaanko tälle käyttöjärjestelmäkäyttäjälle tallennetut Ohto-tunnukset?\n\n"
           "Ohto-käyttäjätunnus ja salasana poistetaan secure storagesta. Toimintoa ei voi perua."),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);
    if (answer != QMessageBox::Yes) return;

    const QSet<QString> keys{
        QStringLiteral("external.ohto.username"),
        QStringLiteral("external.ohto.password"),
    };
    auto pending = std::make_shared<QSet<QString>>(keys);
    auto errors = std::make_shared<QStringList>();
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = connect(m_credentials, &CredentialStore::writeFinished, this,
        [this, pending, errors, connection](const QString &key, const QString &error) {
            if (!pending->remove(key)) return;
            if (!error.isEmpty()) errors->append(error);
            if (!pending->isEmpty()) return;
            disconnect(*connection);
            if (errors->isEmpty()) {
                statusBar()->showMessage(tr("Ohto-tunnukset poistettiin secure storagesta."), 5000);
                QMessageBox::information(this, tr("Unohda minut"),
                    tr("Ohto-käyttäjätunnus ja salasana on poistettu."));
            } else {
                QMessageBox::warning(this, tr("Unohda minut"),
                    tr("Kaikkia Ohto-tunnuksia ei voitu poistaa: %1").arg(errors->join(QStringLiteral("; "))));
            }
        });
    for (const auto &key : keys) m_credentials->deleteSecret(key);
}

void MainWindow::configureOhtoCredentials() {
    if (!m_credentials->isAvailable()) {
        QMessageBox::warning(this, tr("Secure storage"),
            tr("QtKeychain/OS credential storage ei ole käytettävissä. Tunnuksia ei tallenneta tiedostoon."));
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Ohto-tunnukset"));
    auto *layout = new QFormLayout(&dialog);
    auto *username = new QLineEdit(&dialog);
    auto *password = new QLineEdit(&dialog);
    password->setEchoMode(QLineEdit::Password);
    password->setPlaceholderText(tr("Jätä tyhjäksi, jos salasanaa ei muuteta"));
    layout->addRow(tr("Käyttäjätunnus:"), username);
    layout->addRow(tr("Salasana:"), password);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel | QDialogButtonBox::Reset, &dialog);
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Reset), &QPushButton::clicked, &dialog, [this, &dialog] {
        m_credentials->deleteSecret(QStringLiteral("external.ohto.username"));
        m_credentials->deleteSecret(QStringLiteral("external.ohto.password"));
        dialog.reject();
    });
    connect(m_credentials, &CredentialStore::secretRead, &dialog,
            [username](const QString &key, const QString &value, const QString &) {
        if (key == QStringLiteral("external.ohto.username")) username->setText(value);
    });
    m_credentials->readSecret(QStringLiteral("external.ohto.username"));
    if (dialog.exec() == QDialog::Accepted) {
        if (!username->text().trimmed().isEmpty())
            m_credentials->writeSecret(QStringLiteral("external.ohto.username"), username->text().trimmed());
        if (!password->text().isEmpty())
            m_credentials->writeSecret(QStringLiteral("external.ohto.password"), password->text());
        password->clear();
    }
}

void MainWindow::buildApplicationsMenu() {
    auto *applicationsMenu = menuBar()->addMenu(tr("Sovellukset"));
    for (const auto &module : m_catalog.modules()) {
        auto *action = applicationsMenu->addAction(QIcon(module.icon), module.name);
        if (module.special.value(QStringLiteral("deploymentStatus")).toString() == QStringLiteral("planned"))
            action->setToolTip(tr("Palvelu odottaa käyttöönottoa; URL on muokattavissa apps.json-tiedostossa."));
        connect(action, &QAction::triggered, this,
                [this, moduleId = module.id] { openModule(moduleId); });
    }
}

QMdiSubWindow *MainWindow::openModule(const QString &moduleId, const QRect &geometry) {
    const auto *module = m_catalog.find(moduleId);
    if (!module) return nullptr;
    if (!module->multipleInstances) {
        for (auto *existing : m_workspace->subWindowList()) {
            if (existing->property("moduleId").toString() == moduleId) {
                m_workspace->setActiveSubWindow(existing);
                existing->showNormal();
                return existing;
            }
        }
    }
    QWidget *content = nullptr;
    if (module->type == QStringLiteral("web"))
        content = new WebAppWindow(*module);
    else if (module->type == QStringLiteral("journal"))
        content = new JournalWidget(m_api, m_offline, m_socket);
    if (!content) return nullptr;
    auto *window = m_workspace->addSubWindow(content,
        Qt::SubWindow | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
        Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);
    window->setProperty("moduleId", moduleId);
    window->setWindowTitle(module->name);
    window->setWindowIcon(QIcon(module->icon));
    if (geometry.isValid()) {
        QRect safe = geometry;
        const QRect available = m_workspace->rect().adjusted(0, 0, -20, -20);
        safe.setSize(safe.size().boundedTo(available.size()).expandedTo(QSize(320, 240)));
        if (!available.intersects(safe)) safe.moveTopLeft(QPoint(20, 20));
        window->setGeometry(safe);
    }
    else window->resize(module->defaultSize.boundedTo(m_workspace->size()));
    window->show();
    return window;
}

void MainWindow::ensureMainWindowOnAvailableScreen(const QString &screenName) {
    QScreen *target = nullptr;
    for (auto *screen : QGuiApplication::screens())
        if (screen->name() == screenName) { target = screen; break; }
    if (!target) target = QGuiApplication::primaryScreen();
    if (target && !target->availableGeometry().intersects(frameGeometry()))
        move(target->availableGeometry().topLeft() + QPoint(40, 40));
}

void MainWindow::restoreWorkspace() {
    QSettings settings;
    restoreGeometry(settings.value(QStringLiteral("workspace/mainGeometry")).toByteArray());
    restoreState(settings.value(QStringLiteral("workspace/mainState")).toByteArray());
    ensureMainWindowOnAvailableScreen(settings.value(QStringLiteral("workspace/screen")).toString());
    const int count = settings.beginReadArray(QStringLiteral("workspace/windows"));
    for (int index = 0; index < count; ++index) {
        settings.setArrayIndex(index);
        auto *window = openModule(settings.value(QStringLiteral("moduleId")).toString(),
                                  settings.value(QStringLiteral("geometry")).toRect());
        if (window) {
            const auto state = Qt::WindowStates::fromInt(settings.value(QStringLiteral("windowState"), 0).toInt());
            if (state.testFlag(Qt::WindowMaximized)) window->showMaximized();
            else if (state.testFlag(Qt::WindowMinimized)) window->showMinimized();
        }
    }
    settings.endArray();
}

void MainWindow::saveWorkspace() {
    QSettings settings;
    settings.setValue(QStringLiteral("workspace/mainGeometry"), saveGeometry());
    settings.setValue(QStringLiteral("workspace/mainState"), saveState());
    settings.setValue(QStringLiteral("workspace/screen"), windowHandle() && windowHandle()->screen()
        ? windowHandle()->screen()->name() : QString());
    settings.beginWriteArray(QStringLiteral("workspace/windows"));
    int index = 0;
    for (auto *window : m_workspace->subWindowList()) {
        if (!window->isVisible()) continue;
        settings.setArrayIndex(index++);
        settings.setValue(QStringLiteral("moduleId"), window->property("moduleId"));
        settings.setValue(QStringLiteral("geometry"), window->geometry());
        settings.setValue(QStringLiteral("windowState"), window->windowState().toInt());
        settings.setValue(QStringLiteral("screen"), windowHandle() && windowHandle()->screen()
            ? windowHandle()->screen()->name() : QString());
    }
    settings.endArray();
    settings.sync();
}

void MainWindow::closeEvent(QCloseEvent *event) {
    saveWorkspace();
    QMainWindow::closeEvent(event);
}
