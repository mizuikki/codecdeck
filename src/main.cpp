#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>

int main(int argc, char *argv[])
{
    QGuiApplication::setDesktopFileName(QStringLiteral("io.github.mizuikki.CodecDeck"));
    QGuiApplication application(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("CodecDeck"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("CodecDeck"));
    QGuiApplication::setOrganizationName(QStringLiteral("CodecDeck"));
    QGuiApplication::setOrganizationDomain(QStringLiteral("mizuikki.github.io"));
    QGuiApplication::setApplicationVersion(QStringLiteral(CODECDECK_VERSION));
    QGuiApplication::setWindowIcon(
        QIcon(QStringLiteral(":/icons/io.github.mizuikki.CodecDeck.png")));

    QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &application, [] { QCoreApplication::exit(EXIT_FAILURE); },
                     Qt::QueuedConnection);
    engine.loadFromModule(QStringLiteral("io.github.mizuikki.CodecDeck"),
                          QStringLiteral("Main"));
    return application.exec();
}
