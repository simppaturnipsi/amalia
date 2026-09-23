#include "WebAppWindow.h"

#include <QMessageBox>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <QWebEnginePage>
#include <QWebEngineCertificateError>
#include <QWebEngineProfile>
#include <QWebEngineView>

WebAppWindow::WebAppWindow(const AppModule &module, QWidget *parent)
    : QWidget(parent), m_moduleId(module.id) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    if (module.separateProfile) {
        m_profile = new QWebEngineProfile(QStringLiteral("amalia-%1").arg(module.id), this);
        const auto root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        m_profile->setPersistentStoragePath(root + QStringLiteral("/profiles/") + module.id);
        m_profile->setCachePath(root + QStringLiteral("/cache/") + module.id);
        m_profile->setPersistentCookiesPolicy(QWebEngineProfile::AllowPersistentCookies);
        m_view = new QWebEngineView(m_profile, this);
    } else {
        m_view = new QWebEngineView(this);
    }
    // TLS errors are intentionally not ignored. Qt WebEngine shows its normal error page.
    connect(m_view->page(), &QWebEnginePage::certificateError, this, [this](const QWebEngineCertificateError &) {
        setToolTip(tr("TLS-varmenteen tarkistus epäonnistui. Yhteyttä ei hyväksytty."));
    });
    layout->addWidget(m_view);
    m_view->load(module.url);
}
