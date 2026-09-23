#include "MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QSaveFile>
#include <QTimer>

int main(int argc, char *argv[]) {
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Vapepa"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("vapepa.fi"));
    QCoreApplication::setApplicationName(QStringLiteral("Amalia"));
    QCoreApplication::setApplicationVersion(QStringLiteral(AMALIA_VERSION));

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption ackOption(QStringLiteral("startup-ack"), QStringLiteral("Launcher startup acknowledgement file"), QStringLiteral("path"));
    QCommandLineOption tokenOption(QStringLiteral("startup-token"), QStringLiteral("Launcher startup token"), QStringLiteral("token"));
    parser.addOption(ackOption);
    parser.addOption(tokenOption);
    parser.process(application);

    MainWindow window;
    if (!window.initialize()) return 1;
    window.show();
    if (parser.isSet(ackOption)) {
        QTimer::singleShot(0, &window, [&parser, &ackOption, &tokenOption] {
            QSaveFile acknowledgement(parser.value(ackOption));
            if (acknowledgement.open(QIODevice::WriteOnly)) {
                acknowledgement.write(parser.value(tokenOption).toUtf8());
                acknowledgement.commit();
            }
        });
    }
    return application.exec();
}
