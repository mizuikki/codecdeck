#pragma once

#include "configstore.h"
#include "pipewiremonitor.h"

#include <QObject>
#include <QProcess>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

class AppController final : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Backend)
    QML_SINGLETON
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(int selectedDevice READ selectedDevice WRITE setSelectedDevice NOTIFY selectedDeviceChanged)
    Q_PROPERTY(int preferredRate READ preferredRate WRITE setPreferredRate NOTIFY settingsChanged)
    Q_PROPERTY(int configuredRate READ configuredRate NOTIFY configuredSettingsChanged)
    Q_PROPERTY(QString ldacQuality READ ldacQuality WRITE setLdacQuality NOTIFY settingsChanged)
    Q_PROPERTY(QString lhdcQuality READ lhdcQuality WRITE setLhdcQuality NOTIFY settingsChanged)
    Q_PROPERTY(bool hasPendingSettings READ hasPendingSettings NOTIFY settingsChanged)
    Q_PROPERTY(QString requestedCodec READ requestedCodec NOTIFY requestedTransportChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    explicit AppController(QObject *parent = nullptr);

    [[nodiscard]] QVariantList devices() const;
    [[nodiscard]] int selectedDevice() const;
    void setSelectedDevice(int value);

    [[nodiscard]] int preferredRate() const;
    void setPreferredRate(int value);
    [[nodiscard]] int configuredRate() const;
    [[nodiscard]] QString ldacQuality() const;
    void setLdacQuality(const QString &value);
    [[nodiscard]] QString lhdcQuality() const;
    void setLhdcQuality(const QString &value);
    [[nodiscard]] bool hasPendingSettings() const;
    [[nodiscard]] QString requestedCodec() const;

    [[nodiscard]] QString statusMessage() const;
    [[nodiscard]] bool busy() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void refreshCapabilities();
    Q_INVOKABLE void apply(const QString &codec);
    Q_INVOKABLE void switchCodec(const QString &codec);

signals:
    void devicesChanged();
    void selectedDeviceChanged();
    void settingsChanged();
    void configuredSettingsChanged();
    void requestedTransportChanged();
    void statusMessageChanged();
    void busyChanged();

private:
    QString selectedAddress() const;
    QString selectedBluezPath() const;
    void setStatusMessage(const QString &message);
    void setRequestedCodec(const QString &codec, const QString &address);
    void clearRequestedCodec();
    void setBusy(bool value);
    bool restartWirePlumber(QString *error);
    void finishCapabilityRefresh(const QString &message);
    void requestBluetoothReconnect();
    void retryCodecSelection();

    ConfigStore m_store;
    PipeWireMonitor m_monitor;
    QVariantList m_devices;
    int m_selectedDevice = -1;
    CodecSettings m_configuredSettings;
    CodecSettings m_settings;
    QString m_statusMessage = QStringLiteral("Connecting to PipeWire");
    bool m_busy = false;
    bool m_capabilityRefresh = false;
    QString m_pendingCodec;
    QString m_pendingAddress;
    QString m_pendingBluezPath;
    QString m_requestedCodec;
    QString m_requestedAddress;
    int m_retryCount = 0;
    bool m_profileSelectionIssued = false;
    QProcess *m_helperProcess = nullptr;
};
