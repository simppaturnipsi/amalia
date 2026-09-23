#pragma once

#include "AppCatalog.h"

#include <QJsonObject>
#include <QMainWindow>

class ApiClient;
class AuthManager;
class CredentialStore;
class JournalSocket;
class OfflineQueue;
class QMdiArea;
class QMdiSubWindow;

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;
    bool initialize();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    QJsonObject loadRuntimeConfig() const;
    void buildApplicationsMenu();
    QMdiSubWindow *openModule(const QString &moduleId, const QRect &geometry = {});
    void restoreWorkspace();
    void saveWorkspace();
    void ensureMainWindowOnAvailableScreen(const QString &screenName);
    void showLoginDialog();
    void configureOhtoCredentials();
    void forgetUser();
    void showAbout();

    AppCatalog m_catalog;
    QJsonObject m_runtime;
    QMdiArea *m_workspace{nullptr};
    CredentialStore *m_credentials{nullptr};
    AuthManager *m_auth{nullptr};
    ApiClient *m_api{nullptr};
    OfflineQueue *m_offline{nullptr};
    JournalSocket *m_socket{nullptr};
};
