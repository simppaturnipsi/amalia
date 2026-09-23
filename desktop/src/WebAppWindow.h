#pragma once

#include "AppCatalog.h"

#include <QWidget>

class QWebEngineProfile;
class QWebEngineView;

class WebAppWindow final : public QWidget {
    Q_OBJECT
public:
    explicit WebAppWindow(const AppModule &module, QWidget *parent = nullptr);
    QString moduleId() const { return m_moduleId; }

private:
    QString m_moduleId;
    QWebEngineProfile *m_profile{nullptr};
    QWebEngineView *m_view{nullptr};
};
