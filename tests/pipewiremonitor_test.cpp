#include "pipewiremonitor.h"

#include <QEventLoop>
#include <QTimer>
#include <QtTest>

class PipeWireMonitorTest final : public QObject
{
    Q_OBJECT

private slots:
    void discoversActiveCodec();
};

void PipeWireMonitorTest::discoversActiveCodec()
{
    PipeWireMonitor monitor;
    QString error;
    if (!monitor.start(&error))
        QSKIP(qPrintable(error));

    QVariantMap activeDevice;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    connect(&monitor, &PipeWireMonitor::devicesChanged, &loop,
            [&activeDevice, &loop](const QVariantList &devices) {
        for (const QVariant &value : devices) {
            const QVariantMap device = value.toMap();
            if (device.value(QStringLiteral("connected")).toBool()
                && !device.value(QStringLiteral("codec")).toString().isEmpty()
                && !device.value(QStringLiteral("profiles")).toList().isEmpty()
                && device.value(QStringLiteral("rate")).toInt() > 0
                && !device.value(QStringLiteral("format")).toString().isEmpty()) {
                activeDevice = device;
                loop.quit();
                return;
            }
        }
    });
    timeout.start(3000);
    monitor.refresh();
    loop.exec();

    if (activeDevice.isEmpty())
        QSKIP("No active Bluetooth A2DP device is available.");

    const QString address = activeDevice.value(QStringLiteral("address")).toString();
    const QString bluezPath = activeDevice.value(QStringLiteral("bluezPath")).toString();
    const QString codec = activeDevice.value(QStringLiteral("codec")).toString();
    QVERIFY(!address.isEmpty());
    QVERIFY(bluezPath.startsWith(QStringLiteral("/org/bluez/")));
    QVERIFY(!codec.isEmpty());
    QVERIFY(activeDevice.value(QStringLiteral("rate")).toInt() > 0);
    QVERIFY(!activeDevice.value(QStringLiteral("format")).toString().isEmpty());
    QVERIFY(!monitor.selectCodec(address, QStringLiteral("not-a-codec"), &error));
    QVERIFY(!error.isEmpty());
}

QTEST_GUILESS_MAIN(PipeWireMonitorTest)
#include "pipewiremonitor_test.moc"
