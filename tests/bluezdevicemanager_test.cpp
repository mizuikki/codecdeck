#include "bluezdevicemanager.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusObjectPath>
#include <QSignalSpy>
#include <QtTest>

class BluezDeviceManagerTest final : public QObject
{
    Q_OBJECT

private slots:
    void recognizesPairedA2dpSink();
    void rejectsUnpairedAndNonAudioDevices();
    void buildsStableSnapshot();
    void enumeratesLiveDevices();
};

namespace {
QVariantMap audioProperties()
{
    return {
        {QStringLiteral("Adapter"),
         QVariant::fromValue(QDBusObjectPath(QStringLiteral("/org/bluez/hci0")))},
        {QStringLiteral("Address"), QStringLiteral("00:11:22:33:44:55")},
        {QStringLiteral("Alias"), QStringLiteral("Test Headphones")},
        {QStringLiteral("Paired"), true},
        {QStringLiteral("Connected"), false},
        {QStringLiteral("ServicesResolved"), true},
        {QStringLiteral("UUIDs"),
         QStringList{QStringLiteral("0000110B-0000-1000-8000-00805F9B34FB")}},
    };
}
}

void BluezDeviceManagerTest::recognizesPairedA2dpSink()
{
    QVERIFY(BluezDeviceManager::isA2dpAudioDevice(audioProperties()));
}

void BluezDeviceManagerTest::rejectsUnpairedAndNonAudioDevices()
{
    QVariantMap unpaired = audioProperties();
    unpaired.insert(QStringLiteral("Paired"), false);
    QVERIFY(!BluezDeviceManager::isA2dpAudioDevice(unpaired));

    QVariantMap keyboard = audioProperties();
    keyboard.insert(QStringLiteral("UUIDs"),
                    QStringList{QStringLiteral("00001124-0000-1000-8000-00805f9b34fb")});
    QVERIFY(!BluezDeviceManager::isA2dpAudioDevice(keyboard));
}

void BluezDeviceManagerTest::buildsStableSnapshot()
{
    QVariantMap properties = audioProperties();
    properties.insert(QStringLiteral("Connected"), true);
    const QVariantMap device = BluezDeviceManager::deviceSnapshot(
        QStringLiteral("/org/bluez/hci0/dev_00_11_22_33_44_55"), properties);

    QCOMPARE(device.value(QStringLiteral("name")).toString(),
             QStringLiteral("Test Headphones"));
    QCOMPARE(device.value(QStringLiteral("address")).toString(),
             QStringLiteral("00:11:22:33:44:55"));
    QCOMPARE(device.value(QStringLiteral("adapterPath")).toString(),
             QStringLiteral("/org/bluez/hci0"));
    QVERIFY(device.value(QStringLiteral("connected")).toBool());
    QVERIFY(device.value(QStringLiteral("servicesResolved")).toBool());
}

void BluezDeviceManagerTest::enumeratesLiveDevices()
{
    QDBusConnection bus = QDBusConnection::systemBus();
    if (!bus.isConnected() || !bus.interface()
        || !bus.interface()->isServiceRegistered(QStringLiteral("org.bluez"))) {
        QSKIP("BlueZ is not available on the system bus.");
    }

    BluezDeviceManager manager;
    QSignalSpy devicesChanged(&manager, &BluezDeviceManager::devicesChanged);
    QString error;
    QVERIFY2(manager.start(&error), qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(!devicesChanged.isEmpty(), 3000);

    const QVariantList devices = devicesChanged.last().at(0).toList();
    for (const QVariant &value : devices) {
        const QVariantMap device = value.toMap();
        QVERIFY(device.value(QStringLiteral("bluezPath")).toString()
                    .startsWith(QStringLiteral("/org/bluez/")));
        QVERIFY(!device.value(QStringLiteral("address")).toString().isEmpty());
        QVERIFY(!device.value(QStringLiteral("name")).toString().isEmpty());
        QVERIFY(device.value(QStringLiteral("paired")).toBool());
    }
}

QTEST_GUILESS_MAIN(BluezDeviceManagerTest)
#include "bluezdevicemanager_test.moc"
