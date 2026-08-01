#include "bluezdevicemanager.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QtDBus/qdbusmetatype.h>

#include <algorithm>

namespace {
constexpr auto bluezService = "org.bluez";
constexpr auto deviceInterface = "org.bluez.Device1";
constexpr auto audioSinkUuid = "0000110b-0000-1000-8000-00805f9b34fb";

QVariant unwrap(const QVariant &value)
{
    if (value.metaType() == QMetaType::fromType<QDBusVariant>())
        return value.value<QDBusVariant>().variant();
    return value;
}

QString stringProperty(const QVariantMap &properties, const QString &name)
{
    return unwrap(properties.value(name)).toString();
}

bool boolProperty(const QVariantMap &properties, const QString &name)
{
    return unwrap(properties.value(name)).toBool();
}

QStringList stringListProperty(const QVariantMap &properties, const QString &name)
{
    return unwrap(properties.value(name)).toStringList();
}

QString objectPathProperty(const QVariantMap &properties, const QString &name)
{
    const QVariant value = unwrap(properties.value(name));
    if (value.metaType() == QMetaType::fromType<QDBusObjectPath>())
        return value.value<QDBusObjectPath>().path();
    return value.toString();
}
}

BluezDeviceManager::BluezDeviceManager(QObject *parent)
    : QObject(parent),
      m_serviceWatcher(new QDBusServiceWatcher(QString::fromLatin1(bluezService),
                                               QDBusConnection::systemBus(),
                                               QDBusServiceWatcher::WatchForRegistration
                                                   | QDBusServiceWatcher::WatchForUnregistration,
                                               this))
{
    connect(m_serviceWatcher, &QDBusServiceWatcher::serviceRegistered,
            this, [this] { refresh(); });
    connect(m_serviceWatcher, &QDBusServiceWatcher::serviceUnregistered,
            this, [this] {
        m_deviceProperties.clear();
        m_audioDevicePaths.clear();
        publish();
    });
}

bool BluezDeviceManager::start(QString *error)
{
    if (m_started)
        return true;

    qDBusRegisterMetaType<BluezInterfaceMap>();
    qDBusRegisterMetaType<BluezManagedObjectMap>();

    QDBusConnection bus = QDBusConnection::systemBus();
    if (!bus.isConnected()) {
        if (error)
            *error = QStringLiteral("Could not connect to the system D-Bus");
        return false;
    }

    const bool added = bus.connect(QString::fromLatin1(bluezService), QStringLiteral("/"),
                                   QStringLiteral("org.freedesktop.DBus.ObjectManager"),
                                   QStringLiteral("InterfacesAdded"), this,
                                   SLOT(onInterfacesAdded(QDBusObjectPath,BluezInterfaceMap)));
    const bool removed = bus.connect(QString::fromLatin1(bluezService), QStringLiteral("/"),
                                     QStringLiteral("org.freedesktop.DBus.ObjectManager"),
                                     QStringLiteral("InterfacesRemoved"), this,
                                     SLOT(onInterfacesRemoved(QDBusObjectPath,QStringList)));
    const bool changed = bus.connect(QString::fromLatin1(bluezService), {},
                                     QStringLiteral("org.freedesktop.DBus.Properties"),
                                     QStringLiteral("PropertiesChanged"), this,
                                     SLOT(onPropertiesChanged(QString,QVariantMap,QStringList,QDBusMessage)));
    if (!added || !removed || !changed) {
        if (error)
            *error = QStringLiteral("Could not subscribe to BlueZ device updates");
        return false;
    }

    m_started = true;
    refresh();
    return true;
}

void BluezDeviceManager::refresh()
{
    QDBusInterface manager(QString::fromLatin1(bluezService), QStringLiteral("/"),
                           QStringLiteral("org.freedesktop.DBus.ObjectManager"),
                           QDBusConnection::systemBus());
    if (!manager.isValid()) {
        m_deviceProperties.clear();
        publish();
        return;
    }

    auto *watcher = new QDBusPendingCallWatcher(
        manager.asyncCall(QStringLiteral("GetManagedObjects")), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, watcher](QDBusPendingCallWatcher *finished) {
        const QDBusPendingReply<BluezManagedObjectMap> reply = *finished;
        watcher->deleteLater();
        if (reply.isError())
            return;

        QMap<QString, QVariantMap> properties;
        const BluezManagedObjectMap objects = reply.value();
        for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
            const auto device = it.value().constFind(QString::fromLatin1(deviceInterface));
            if (device != it.value().cend())
                properties.insert(it.key().path(), device.value());
        }
        m_deviceProperties = std::move(properties);
        for (auto it = m_audioDevicePaths.begin(); it != m_audioDevicePaths.end();) {
            if (!m_deviceProperties.contains(*it))
                it = m_audioDevicePaths.erase(it);
            else
                ++it;
        }
        publish();
    });
}

void BluezDeviceManager::connectDevice(const QString &path)
{
    setConnected(path, true);
}

void BluezDeviceManager::disconnectDevice(const QString &path)
{
    setConnected(path, false);
}

bool BluezDeviceManager::isA2dpAudioDevice(const QVariantMap &properties)
{
    if (!boolProperty(properties, QStringLiteral("Paired")))
        return false;

    const QStringList uuids = stringListProperty(properties, QStringLiteral("UUIDs"));
    return std::any_of(uuids.cbegin(), uuids.cend(), [](const QString &uuid) {
        return uuid.compare(QString::fromLatin1(audioSinkUuid), Qt::CaseInsensitive) == 0;
    });
}

QVariantMap BluezDeviceManager::deviceSnapshot(const QString &path,
                                               const QVariantMap &properties)
{
    QVariantMap result;
    const QString address = stringProperty(properties, QStringLiteral("Address"));
    QString name = stringProperty(properties, QStringLiteral("Alias"));
    if (name.isEmpty())
        name = stringProperty(properties, QStringLiteral("Name"));
    if (name.isEmpty())
        name = address;

    result.insert(QStringLiteral("bluezPath"), path);
    result.insert(QStringLiteral("adapterPath"),
                  objectPathProperty(properties, QStringLiteral("Adapter")));
    result.insert(QStringLiteral("address"), address);
    result.insert(QStringLiteral("name"), name);
    result.insert(QStringLiteral("paired"), boolProperty(properties, QStringLiteral("Paired")));
    result.insert(QStringLiteral("connected"), boolProperty(properties, QStringLiteral("Connected")));
    result.insert(QStringLiteral("servicesResolved"),
                  boolProperty(properties, QStringLiteral("ServicesResolved")));
    result.insert(QStringLiteral("uuids"), stringListProperty(properties, QStringLiteral("UUIDs")));
    return result;
}

void BluezDeviceManager::onInterfacesAdded(const QDBusObjectPath &path,
                                           const BluezInterfaceMap &interfaces)
{
    const auto device = interfaces.constFind(QString::fromLatin1(deviceInterface));
    if (device == interfaces.cend())
        return;
    m_deviceProperties.insert(path.path(), device.value());
    publish();
}

void BluezDeviceManager::onInterfacesRemoved(const QDBusObjectPath &path,
                                             const QStringList &interfaces)
{
    if (!interfaces.contains(QString::fromLatin1(deviceInterface)))
        return;
    m_deviceProperties.remove(path.path());
    m_audioDevicePaths.remove(path.path());
    publish();
}

void BluezDeviceManager::onPropertiesChanged(const QString &interface,
                                             const QVariantMap &changed,
                                             const QStringList &invalidated,
                                             const QDBusMessage &message)
{
    if (interface != QString::fromLatin1(deviceInterface))
        return;
    auto device = m_deviceProperties.find(message.path());
    if (device == m_deviceProperties.end())
        return;
    for (auto it = changed.cbegin(); it != changed.cend(); ++it)
        device->insert(it.key(), it.value());
    for (const QString &name : invalidated)
        device->remove(name);
    publish();
}

void BluezDeviceManager::setConnected(const QString &path, bool connected)
{
    if (!path.startsWith(QStringLiteral("/org/bluez/"))) {
        emit connectionRequestFinished(path, connected,
                                       QStringLiteral("org.bluez.Error.InvalidArguments"),
                                       QStringLiteral("Invalid Bluetooth device path"));
        return;
    }

    QDBusInterface device(QString::fromLatin1(bluezService), path,
                          QString::fromLatin1(deviceInterface), QDBusConnection::systemBus());
    if (!device.isValid()) {
        emit connectionRequestFinished(path, connected,
                                       QStringLiteral("org.bluez.Error.DoesNotExist"),
                                       QStringLiteral("Bluetooth device is unavailable"));
        return;
    }

    auto *watcher = new QDBusPendingCallWatcher(
        device.asyncCall(connected ? QStringLiteral("Connect") : QStringLiteral("Disconnect")),
        this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, watcher, path, connected](QDBusPendingCallWatcher *finished) {
        const QDBusPendingReply<> reply = *finished;
        watcher->deleteLater();
        emit connectionRequestFinished(path, connected,
                                       reply.isError() ? reply.error().name() : QString{},
                                       reply.isError() ? reply.error().message() : QString{});
    });
}

void BluezDeviceManager::publish()
{
    QVariantList devices;
    for (auto it = m_deviceProperties.cbegin(); it != m_deviceProperties.cend(); ++it) {
        if (isA2dpAudioDevice(it.value()))
            m_audioDevicePaths.insert(it.key());
        if (boolProperty(it.value(), QStringLiteral("Paired"))
            && m_audioDevicePaths.contains(it.key())) {
            devices.append(deviceSnapshot(it.key(), it.value()));
        }
    }
    std::sort(devices.begin(), devices.end(), [](const QVariant &left, const QVariant &right) {
        const QVariantMap a = left.toMap();
        const QVariantMap b = right.toMap();
        if (a.value(QStringLiteral("connected")).toBool()
            != b.value(QStringLiteral("connected")).toBool()) {
            return a.value(QStringLiteral("connected")).toBool();
        }
        const int nameOrder = QString::localeAwareCompare(
            a.value(QStringLiteral("name")).toString(),
            b.value(QStringLiteral("name")).toString());
        if (nameOrder != 0)
            return nameOrder < 0;
        return a.value(QStringLiteral("address")).toString()
            < b.value(QStringLiteral("address")).toString();
    });
    emit devicesChanged(devices);
}
