#include "appcontroller.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QFileInfo>
#include <QTimer>

namespace {
QString codecLabel(const QString &codec)
{
    if (codec == QStringLiteral("lhdc_v5"))
        return QStringLiteral("LHDC v5");
    return codec.toUpper();
}
}

AppController::AppController(QObject *parent)
    : QObject(parent), m_monitor(this), m_configuredSettings(m_store.load()),
      m_settings(m_configuredSettings)
{
    connect(&m_monitor, &PipeWireMonitor::devicesChanged, this,
            [this](const QVariantList &devices) {
        const QString oldAddress = selectedAddress();
        m_devices = devices;
        m_selectedDevice = -1;
        for (qsizetype i = 0; i < m_devices.size(); ++i) {
            const QVariantMap device = m_devices.at(i).toMap();
            if ((!oldAddress.isEmpty() && device.value(QStringLiteral("address")).toString() == oldAddress)
                || (oldAddress.isEmpty() && device.value(QStringLiteral("connected")).toBool())) {
                m_selectedDevice = static_cast<int>(i);
                break;
            }
        }
        if (m_selectedDevice < 0 && !m_devices.isEmpty())
            m_selectedDevice = 0;
        emit devicesChanged();
        emit selectedDeviceChanged();
        if (m_pendingCodec.isEmpty() && !m_requestedCodec.isEmpty()) {
            for (const QVariant &value : std::as_const(m_devices)) {
                const QVariantMap device = value.toMap();
                if (device.value(QStringLiteral("address")).toString() == m_requestedAddress
                    && device.value(QStringLiteral("codec")).toString() == m_requestedCodec) {
                    clearRequestedCodec();
                    break;
                }
            }
        }
        emit requestedTransportChanged();
        if (m_pendingCodec.isEmpty() && !m_capabilityRefresh)
            setStatusMessage(m_devices.isEmpty() ? QStringLiteral("No Bluetooth audio device")
                                                  : QStringLiteral("Ready"));
    });
    connect(&m_monitor, &PipeWireMonitor::connectionChanged, this,
            [this](bool connected, const QString &error) {
        setStatusMessage(connected ? QStringLiteral("Ready") : error);
    });

    QString error;
    if (!m_monitor.start(&error))
        setStatusMessage(error);
}

QVariantList AppController::devices() const { return m_devices; }
int AppController::selectedDevice() const { return m_selectedDevice; }

void AppController::setSelectedDevice(int value)
{
    if (value == m_selectedDevice || value < -1 || value >= m_devices.size())
        return;
    m_selectedDevice = value;
    emit selectedDeviceChanged();
    emit requestedTransportChanged();
}

int AppController::preferredRate() const { return m_settings.sampleRate; }
void AppController::setPreferredRate(int value)
{
    if (value == m_settings.sampleRate)
        return;
    m_settings.sampleRate = value;
    emit settingsChanged();
}

int AppController::configuredRate() const { return m_configuredSettings.sampleRate; }

QString AppController::ldacQuality() const { return m_settings.ldacQuality; }
void AppController::setLdacQuality(const QString &value)
{
    if (value == m_settings.ldacQuality)
        return;
    m_settings.ldacQuality = value;
    emit settingsChanged();
}

QString AppController::lhdcQuality() const { return m_settings.lhdcQuality; }
void AppController::setLhdcQuality(const QString &value)
{
    if (value == m_settings.lhdcQuality)
        return;
    m_settings.lhdcQuality = value;
    emit settingsChanged();
}

bool AppController::hasPendingSettings() const
{
    return m_settings != m_configuredSettings;
}

QString AppController::requestedCodec() const
{
    return selectedAddress() == m_requestedAddress ? m_requestedCodec : QString{};
}

QString AppController::statusMessage() const { return m_statusMessage; }
bool AppController::busy() const { return m_busy; }

void AppController::refresh()
{
    m_monitor.refresh();
    setStatusMessage(QStringLiteral("Refreshing"));
}

void AppController::refreshCapabilities()
{
    if (m_busy)
        return;

    const QString bluezPath = selectedBluezPath();
    const QString address = selectedAddress();
    const QString helperPath = QString::fromUtf8(CODECDECK_HELPER_PATH);
    if (bluezPath.isEmpty() || address.isEmpty()) {
        setStatusMessage(QStringLiteral("No Bluetooth device is selected"));
        return;
    }
    if (!QFileInfo::exists(helperPath)) {
        setStatusMessage(QStringLiteral("The capability refresh helper is not installed"));
        return;
    }

    m_capabilityRefresh = true;
    m_pendingCodec.clear();
    m_pendingAddress = address;
    m_pendingBluezPath = bluezPath;
    m_retryCount = 0;
    m_profileSelectionIssued = false;
    setBusy(true);
    setStatusMessage(QStringLiteral("Authorizing capability refresh"));

    auto *process = new QProcess(this);
    m_helperProcess = process;
    connect(process, &QProcess::errorOccurred, this,
            [this, process](QProcess::ProcessError processError) {
        if (processError != QProcess::FailedToStart || m_helperProcess != process)
            return;
        m_helperProcess = nullptr;
        process->deleteLater();
        finishCapabilityRefresh(QStringLiteral("Could not start the authorization helper"));
    });
    connect(process, &QProcess::finished, this,
            [this, process](int exitCode, QProcess::ExitStatus exitStatus) {
        if (m_helperProcess != process)
            return;
        m_helperProcess = nullptr;
        const QString helperError = QString::fromUtf8(process->readAllStandardError()).trimmed();
        process->deleteLater();

        if (exitStatus != QProcess::NormalExit || exitCode != 0) {
            const QString message = exitCode == 126 || exitCode == 127
                ? QStringLiteral("Authorization canceled")
                : (helperError.isEmpty() ? QStringLiteral("Capability refresh failed") : helperError);
            finishCapabilityRefresh(message);
            return;
        }

        QString error;
        if (!restartWirePlumber(&error)) {
            finishCapabilityRefresh(error);
            return;
        }
        setStatusMessage(QStringLiteral("Restarting WirePlumber"));
        QTimer::singleShot(500, this, &AppController::requestBluetoothReconnect);
        QTimer::singleShot(1200, this, &AppController::retryCodecSelection);
    });
    process->start(QStringLiteral("pkexec"),
                   {helperPath, QStringLiteral("refresh-capabilities"), bluezPath});
}

void AppController::apply(const QString &codec)
{
    QString error;
    if (!m_store.save(m_settings, &error)) {
        setStatusMessage(error);
        return;
    }
    if (m_configuredSettings != m_settings) {
        m_configuredSettings = m_settings;
        emit configuredSettingsChanged();
        emit settingsChanged();
    }

    m_pendingCodec = codec;
    m_pendingAddress = selectedAddress();
    m_pendingBluezPath = selectedBluezPath();
    setRequestedCodec(codec, m_pendingAddress);
    m_retryCount = 0;
    m_profileSelectionIssued = false;
    setBusy(true);
    setStatusMessage(QStringLiteral("Restarting WirePlumber"));

    if (!restartWirePlumber(&error)) {
        clearRequestedCodec();
        setBusy(false);
        setStatusMessage(error);
        return;
    }
    QTimer::singleShot(500, this, &AppController::requestBluetoothReconnect);
    QTimer::singleShot(1200, this, &AppController::retryCodecSelection);
}

void AppController::switchCodec(const QString &codec)
{
    m_pendingCodec = codec;
    m_pendingAddress = selectedAddress();
    m_pendingBluezPath = selectedBluezPath();
    setRequestedCodec(codec, m_pendingAddress);
    m_retryCount = 0;
    m_profileSelectionIssued = false;

    QString error;
    if (!m_monitor.selectCodec(selectedAddress(), codec, &error)) {
        m_pendingCodec.clear();
        m_pendingAddress.clear();
        m_pendingBluezPath.clear();
        setStatusMessage(error);
        return;
    }
    m_profileSelectionIssued = true;
    setBusy(true);
    setStatusMessage(QStringLiteral("Switching codec"));
    QTimer::singleShot(600, this, &AppController::retryCodecSelection);
}

QString AppController::selectedAddress() const
{
    if (m_selectedDevice < 0 || m_selectedDevice >= m_devices.size())
        return {};
    return m_devices.at(m_selectedDevice).toMap().value(QStringLiteral("address")).toString();
}

QString AppController::selectedBluezPath() const
{
    if (m_selectedDevice < 0 || m_selectedDevice >= m_devices.size())
        return {};
    return m_devices.at(m_selectedDevice).toMap().value(QStringLiteral("bluezPath")).toString();
}

void AppController::setStatusMessage(const QString &message)
{
    if (message == m_statusMessage)
        return;
    m_statusMessage = message;
    emit statusMessageChanged();
}

void AppController::setRequestedCodec(const QString &codec, const QString &address)
{
    if (codec == m_requestedCodec && address == m_requestedAddress)
        return;
    m_requestedCodec = codec;
    m_requestedAddress = address;
    emit requestedTransportChanged();
}

void AppController::clearRequestedCodec()
{
    if (m_requestedCodec.isEmpty() && m_requestedAddress.isEmpty())
        return;
    m_requestedCodec.clear();
    m_requestedAddress.clear();
    emit requestedTransportChanged();
}

void AppController::setBusy(bool value)
{
    if (value == m_busy)
        return;
    m_busy = value;
    emit busyChanged();
}

bool AppController::restartWirePlumber(QString *error)
{
    QDBusInterface manager(QStringLiteral("org.freedesktop.systemd1"),
                           QStringLiteral("/org/freedesktop/systemd1"),
                           QStringLiteral("org.freedesktop.systemd1.Manager"),
                           QDBusConnection::sessionBus());
    if (!manager.isValid()) {
        if (error)
            *error = QStringLiteral("Could not connect to the user service manager");
        return false;
    }

    const QDBusMessage reply = manager.call(QStringLiteral("RestartUnit"),
                                             QStringLiteral("wireplumber.service"),
                                             QStringLiteral("replace"));
    if (reply.type() == QDBusMessage::ErrorMessage) {
        if (error)
            *error = reply.errorMessage();
        return false;
    }
    return true;
}

void AppController::finishCapabilityRefresh(const QString &message)
{
    m_capabilityRefresh = false;
    m_pendingCodec.clear();
    m_pendingAddress.clear();
    m_pendingBluezPath.clear();
    m_profileSelectionIssued = false;
    setBusy(false);
    setStatusMessage(message);
}

void AppController::requestBluetoothReconnect()
{
    if (m_pendingBluezPath.isEmpty())
        return;

    QDBusInterface device(QStringLiteral("org.bluez"), m_pendingBluezPath,
                          QStringLiteral("org.bluez.Device1"),
                          QDBusConnection::systemBus());
    if (!device.isValid())
        return;

    setStatusMessage(QStringLiteral("Reconnecting Bluetooth device"));
    auto *watcher = new QDBusPendingCallWatcher(
        device.asyncCall(QStringLiteral("Connect")), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, watcher](QDBusPendingCallWatcher *finished) {
        const QDBusPendingReply<> reply = *finished;
        if (reply.isError()) {
            const QString name = reply.error().name();
            if (!name.endsWith(QStringLiteral("AlreadyConnected"))
                && !name.endsWith(QStringLiteral("InProgress"))) {
                setStatusMessage(QStringLiteral("Bluetooth reconnect pending"));
            }
        }
        watcher->deleteLater();
    });
}

void AppController::retryCodecSelection()
{
    m_monitor.refresh();

    QVariantMap device;
    for (const QVariant &value : std::as_const(m_devices)) {
        const QVariantMap candidate = value.toMap();
        if (candidate.value(QStringLiteral("address")).toString() == m_pendingAddress) {
            device = candidate;
            break;
        }
    }

    if (m_capabilityRefresh) {
        const bool active = !device.isEmpty()
            && device.value(QStringLiteral("connected")).toBool()
            && !device.value(QStringLiteral("codec")).toString().isEmpty()
            && !device.value(QStringLiteral("profiles")).toList().isEmpty();
        if (active) {
            finishCapabilityRefresh(QStringLiteral("Bluetooth capabilities refreshed"));
            QTimer::singleShot(900, &m_monitor, &PipeWireMonitor::refresh);
            return;
        }
        if ((device.isEmpty() || !device.value(QStringLiteral("connected")).toBool())
            && m_retryCount % 4 == 0) {
            requestBluetoothReconnect();
        }
        if (++m_retryCount >= 40) {
            finishCapabilityRefresh(QStringLiteral("Bluetooth device did not reconnect"));
            return;
        }
        QTimer::singleShot(750, this, &AppController::retryCodecSelection);
        return;
    }

    if (!device.isEmpty()
        && device.value(QStringLiteral("codec")).toString() == m_pendingCodec) {
        clearRequestedCodec();
        m_pendingCodec.clear();
        m_pendingAddress.clear();
        m_pendingBluezPath.clear();
        m_profileSelectionIssued = false;
        setBusy(false);
        setStatusMessage(QStringLiteral("Settings applied"));
        QTimer::singleShot(900, &m_monitor, &PipeWireMonitor::refresh);
        return;
    }

    QString error;
    if (!m_profileSelectionIssued && !device.isEmpty()
        && m_monitor.selectCodec(m_pendingAddress, m_pendingCodec, &error)) {
        m_profileSelectionIssued = true;
        setStatusMessage(QStringLiteral("Switching codec"));
    } else if (device.isEmpty() && m_retryCount % 4 == 0) {
        requestBluetoothReconnect();
    }

    if (++m_retryCount >= 24) {
        const QString requestedCodec = m_pendingCodec;
        const QString activeCodec = device.value(QStringLiteral("codec")).toString();
        m_pendingCodec.clear();
        m_pendingAddress.clear();
        m_pendingBluezPath.clear();
        m_profileSelectionIssued = false;
        setBusy(false);
        if (!activeCodec.isEmpty() && activeCodec != requestedCodec) {
            setStatusMessage(QStringLiteral("Could not switch to %1; using %2")
                                 .arg(codecLabel(requestedCodec), codecLabel(activeCodec)));
        } else {
            setStatusMessage(error.isEmpty() ? QStringLiteral("Bluetooth device did not reconnect") : error);
        }
        return;
    }
    QTimer::singleShot(750, this, &AppController::retryCodecSelection);
}
