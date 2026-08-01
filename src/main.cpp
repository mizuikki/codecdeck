#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>

int main(int argc, char *argv[])
{
    QGuiApplication application(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("CodecDeck"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("CodecDeck"));
    QGuiApplication::setOrganizationName(QStringLiteral("CodecDeck"));
    QGuiApplication::setOrganizationDomain(QStringLiteral("mizuikki.github.io"));
    QGuiApplication::setApplicationVersion(QStringLiteral(CODECDECK_VERSION));

    QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &application, [] { QCoreApplication::exit(EXIT_FAILURE); },
                     Qt::QueuedConnection);
    engine.loadFromModule(QStringLiteral("io.github.mizuikki.CodecDeck"),
                          QStringLiteral("Main"));
    return application.exec();
}
