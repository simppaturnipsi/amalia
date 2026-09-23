#pragma once

#include <QJsonObject>
#include <QList>
#include <QSize>
#include <QString>
#include <QUrl>

struct AppModule {
    QString id;
    QString name;
    QString type;
    QUrl url;
    QString icon;
    QSize defaultSize{900, 700};
    bool multipleInstances{false};
    bool separateProfile{false};
    QJsonObject special;
};

class AppCatalog final {
public:
    bool load(const QString &externalPath = {});
    const QList<AppModule> &modules() const { return m_modules; }
    const AppModule *find(const QString &id) const;
    QString errorString() const { return m_error; }

private:
    QList<AppModule> m_modules;
    QString m_error;
};
