#include <QCoreApplication>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusVariant>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>

#include <unistd.h>

namespace {
struct DeviceLocation {
    QString adapterName;
    QString deviceAddress;
};

bool parseDevicePath(const QString &path, DeviceLocation *location)
{
    static const QRegularExpression expression(
        QStringLiteral("^/org/bluez/(hci[0-9]+)/dev_"
                       "([0-9A-Fa-f]{2}(?:_[0-9A-Fa-f]{2}){5})$"));
    const QRegularExpressionMatch match = expression.match(path);
    if (!match.hasMatch())
        return false;

    location->adapterName = match.captured(1);
    location->deviceAddress = match.captured(2).replace(QLatin1Char('_'), QLatin1Char(':')).toUpper();
    return true;
}

QString adapterAddress(const QString &adapterName)
{
    QDBusInterface properties(QStringLiteral("org.bluez"),
                              QStringLiteral("/org/bluez/%1").arg(adapterName),
                              QStringLiteral("org.freedesktop.DBus.Properties"),
                              QDBusConnection::systemBus());
    const QDBusReply<QDBusVariant> reply = properties.call(
        QStringLiteral("Get"), QStringLiteral("org.bluez.Adapter1"),
        QStringLiteral("Address"));
    if (reply.isValid()) {
        const QString address = reply.value().variant().toString().trimmed().toUpper();
        if (!address.isEmpty())
            return address;
    }

    QFile file(QStringLiteral("/sys/class/bluetooth/%1/address").arg(adapterName));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(file.readAll()).trimmed().toUpper();
}

bool validAddress(const QString &address)
{
    static const QRegularExpression expression(
        QStringLiteral("^[0-9A-F]{2}(?::[0-9A-F]{2}){5}$"));
    return expression.match(address).hasMatch();
}

int fail(const QString &message)
{
    QTextStream(stderr) << "codecdeck-helper: " << message << Qt::endl;
    return 1;
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    const QStringList arguments = application.arguments();

    if (arguments.size() != 3 || arguments.at(1) != QStringLiteral("refresh-capabilities"))
        return fail(QStringLiteral("usage: codecdeck-helper refresh-capabilities <bluez-device-path>"));
    if (geteuid() != 0)
        return fail(QStringLiteral("this helper must be run through Polkit"));

    DeviceLocation location;
    if (!parseDevicePath(arguments.at(2), &location))
        return fail(QStringLiteral("invalid BlueZ device path"));

    const QString adapter = adapterAddress(location.adapterName);
    if (!validAddress(adapter) || !validAddress(location.deviceAddress))
        return fail(QStringLiteral("could not resolve a valid Bluetooth adapter or device address"));

    QDBusInterface device(QStringLiteral("org.bluez"), arguments.at(2),
                          QStringLiteral("org.bluez.Device1"), QDBusConnection::systemBus());
    if (!device.isValid())
        return fail(QStringLiteral("could not access the BlueZ device"));

    const QDBusMessage disconnectReply = device.call(QStringLiteral("Disconnect"));
    if (disconnectReply.type() == QDBusMessage::ErrorMessage
        && !disconnectReply.errorName().endsWith(QStringLiteral("NotConnected"))) {
        return fail(QStringLiteral("BlueZ refused to disconnect the device: %1")
                        .arg(disconnectReply.errorMessage()));
    }

    const QString cachePath = QStringLiteral("/var/lib/bluetooth/%1/cache/%2")
                                  .arg(adapter, location.deviceAddress);
    const QFileInfo cacheInfo(cachePath);
    if (!cacheInfo.exists()) {
        QTextStream(stdout) << "No cache file exists for " << location.deviceAddress << Qt::endl;
        return 0;
    }
    if (!cacheInfo.isFile() || cacheInfo.isSymLink())
        return fail(QStringLiteral("refusing to operate on an unexpected cache file"));

    const QString backupDirectory = QStringLiteral("/var/backups/codecdeck");
    if (!QDir().mkpath(backupDirectory))
        return fail(QStringLiteral("could not create the cache backup directory"));
    QFile::setPermissions(backupDirectory, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                          | QFileDevice::ExeOwner);

    const QString timestamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString backupPath = QStringLiteral("%1/cache-%2-%3-%4")
                                   .arg(backupDirectory, adapter, location.deviceAddress, timestamp);
    if (!QFile::copy(cachePath, backupPath))
        return fail(QStringLiteral("could not back up the BlueZ cache"));
    QFile::setPermissions(backupPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);

    if (!QFile::remove(cachePath))
        return fail(QStringLiteral("could not remove the BlueZ cache"));

    QTextStream(stdout) << "Bluetooth capability cache cleared for "
                        << location.deviceAddress << Qt::endl;
    return 0;
}
