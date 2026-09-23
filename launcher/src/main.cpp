#include "UpdateEngine.h"

#include <QApplication>
#include <QFile>
#include <QJsonDocument>
#include <QMessageBox>

int main(int argc, char *argv[]) {
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Vapepa"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("vapepa.fi"));
    QCoreApplication::setApplicationName(QStringLiteral("AmaliaLauncher"));
    QCoreApplication::setApplicationVersion(QStringLiteral(AMALIA_LAUNCHER_VERSION));
    QString configPath = QCoreApplication::applicationDirPath() + QStringLiteral("/config/launcher.json");
    if (!QFile::exists(configPath))
        configPath = QCoreApplication::applicationDirPath() + QStringLiteral("/../share/amalia/config/launcher.json");
    QFile config(configPath);
    if (!config.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(nullptr, QObject::tr("Amalia Launcher"), QObject::tr("launcher.json-asetusta ei löytynyt."));
        return 1;
    }
    const auto object = QJsonDocument::fromJson(config.readAll()).object();
    UpdateEngine engine(object);
    if (!engine.run()) {
        QMessageBox::critical(nullptr, QObject::tr("Amalia Launcher"), engine.errorString());
        return 1;
    }
    return 0;
}
