#include "AppCatalog.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

bool AppCatalog::load(const QString &externalPath) {
    m_modules.clear();
    m_error.clear();
    QFile file(externalPath.isEmpty() ? QStringLiteral(":/apps.json") : externalPath);
    if (!file.open(QIODevice::ReadOnly)) {
        m_error = QStringLiteral("Sovelluskatalogia ei voitu avata: %1").arg(file.errorString());
        return false;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        m_error = QStringLiteral("Virheellinen sovelluskatalogi: %1").arg(parseError.errorString());
        return false;
    }
    QSet<QString> ids;
    const auto applications = document.object().value(QStringLiteral("applications")).toArray();
    for (const auto &value : applications) {
        const auto object = value.toObject();
        AppModule module;
        module.id = object.value(QStringLiteral("id")).toString().trimmed();
        module.name = object.value(QStringLiteral("name")).toString().trimmed();
        module.type = object.value(QStringLiteral("type")).toString(QStringLiteral("web"));
        module.url = QUrl(object.value(QStringLiteral("url")).toString());
        module.icon = object.value(QStringLiteral("icon")).toString();
        const auto size = object.value(QStringLiteral("defaultSize")).toArray();
        if (size.size() == 2)
            module.defaultSize = QSize(size.at(0).toInt(900), size.at(1).toInt(700));
        module.multipleInstances = object.value(QStringLiteral("multipleInstances")).toBool(false);
        module.separateProfile = object.value(QStringLiteral("separateProfile")).toBool(false);
        module.special = object.value(QStringLiteral("special")).toObject();
        const bool validType = module.type == QStringLiteral("web") || module.type == QStringLiteral("journal");
        if (module.id.isEmpty() || module.name.isEmpty() || ids.contains(module.id) || !validType ||
            (module.type == QStringLiteral("web") && (!module.url.isValid() || module.url.scheme() != QStringLiteral("https")))) {
            m_error = QStringLiteral("Virheellinen tai päällekkäinen moduuli: %1").arg(module.id);
            m_modules.clear();
            return false;
        }
        ids.insert(module.id);
        m_modules.append(module);
    }
    return !m_modules.isEmpty();
}

const AppModule *AppCatalog::find(const QString &id) const {
    for (const auto &module : m_modules) {
        if (module.id == id)
            return &module;
    }
    return nullptr;
}
