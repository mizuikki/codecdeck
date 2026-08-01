#pragma once

#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QVariantList>
#include <QVariantMap>

class QDBusServiceWatcher;

using BluezInterfaceMap = QMap<QString, QVariantMap>;
using BluezManagedObjectMap = QMap<QDBusObjectPath, BluezInterfaceMap>;

Q_DECLARE_METATYPE(BluezInterfaceMap)
Q_DECLARE_METATYPE(BluezManagedObjectMap)

class BluezDeviceManager final : public QObject
{
    Q_OBJECT

public:
    explicit BluezDeviceManager(QObject *parent = nullptr);

    bool start(QString *error = nullptr);
    void refresh();
    void connectDevice(const QString &path);
    void disconnectDevice(const QString &path);

    [[nodiscard]] static bool isA2dpAudioDevice(const QVariantMap &properties);
    [[nodiscard]] static QVariantMap deviceSnapshot(const QString &path,
                                                    const QVariantMap &properties);

signals:
    void devicesChanged(const QVariantList &devices);
    void connectionRequestFinished(const QString &path, bool connected,
                                   const QString &errorName, const QString &errorMessage);

private slots:
    void onInterfacesAdded(const QDBusObjectPath &path, const BluezInterfaceMap &interfaces);
    void onInterfacesRemoved(const QDBusObjectPath &path, const QStringList &interfaces);
    void onPropertiesChanged(const QString &interface, const QVariantMap &changed,
                             const QStringList &invalidated, const QDBusMessage &message);

private:
    void setConnected(const QString &path, bool connected);
    void publish();

    QMap<QString, QVariantMap> m_deviceProperties;
    QSet<QString> m_audioDevicePaths;
    QDBusServiceWatcher *m_serviceWatcher = nullptr;
    bool m_started = false;
};
