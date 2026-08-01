#include "configstore.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class ConfigStoreTest final : public QObject
{
    Q_OBJECT

private slots:
    void roundTrip();
    void rejectsInvalidValues();
    void loadsDefaultsWhenMissing();
};

void ConfigStoreTest::roundTrip()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("nested/51-codecdeck.conf"));
    ConfigStore store(path);
    CodecSettings expected;
    expected.sampleRate = 88200;
    expected.ldacQuality = QStringLiteral("hq");
    expected.lhdcQuality = QStringLiteral("900k");

    QString error;
    QVERIFY2(store.save(expected, &error), qPrintable(error));
    const CodecSettings actual = store.load();
    QCOMPARE(actual.sampleRate, expected.sampleRate);
    QCOMPARE(actual.ldacQuality, expected.ldacQuality);
    QCOMPARE(actual.lhdcQuality, expected.lhdcQuality);

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray content = file.readAll();
    QVERIFY(content.contains("bluez5.default.rate = 88200"));
    QVERIFY(content.contains("bluez5.a2dp.ldac.quality = \"hq\""));
}

void ConfigStoreTest::rejectsInvalidValues()
{
    QTemporaryDir directory;
    ConfigStore store(directory.filePath(QStringLiteral("settings.conf")));
    CodecSettings settings;
    settings.sampleRate = 12345;
    QString error;
    QVERIFY(!store.save(settings, &error));
    QVERIFY(!error.isEmpty());
}

void ConfigStoreTest::loadsDefaultsWhenMissing()
{
    QTemporaryDir directory;
    ConfigStore store(directory.filePath(QStringLiteral("missing.conf")));
    const CodecSettings settings = store.load();
    QCOMPARE(settings.sampleRate, 96000);
    QCOMPARE(settings.ldacQuality, QStringLiteral("auto"));
    QCOMPARE(settings.lhdcQuality, QStringLiteral("auto"));
}

QTEST_MAIN(ConfigStoreTest)
#include "configstore_test.moc"
