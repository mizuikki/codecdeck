#include "appcontroller.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QFileInfo>
#include <QSet>
#include <QTimer>

#include <algorithm>

namespace {
QString codecLabel(const QString &codec)
{
    if (codec == QStringLiteral("lhdc_v5"))
        return QStringLiteral("LHDC v5");
    return codec.toUpper();
}
}

AppController::AppController(QObject *parent)
    : QObject(parent), m_bluez(this), m_monitor(this), m_configuredSettings(m_store.load()),
      m_settings(m_configuredSettings)
{
    m_connectionTimer.setSingleShot(true);
    connect(&m_connectionTimer, &QTimer::timeout, this, [this] {
        const QVariantMap device = deviceByPath(m_connectionPath);
        if (m_connectionTargetConnected
            && device.value(QStringLiteral("connected")).toBool()) {
            finishConnectionOperation(
                QStringLiteral("Connected, but no audio transport is available"));
        } else {
            finishConnectionOperation(m_connectionTargetConnected
                                          ? QStringLiteral("Bluetooth connection timed out")
                                          : QStringLiteral("Bluetooth disconnection timed out"));
        }
    });

    connect(&m_bluez, &BluezDeviceManager::devicesChanged, this,
            [this](const QVariantList &devices) {
        m_bluezDevices = devices;
        rebuildDevices();
        if (m_pendingCodec.isEmpty() && !m_capabilityRefresh
            && m_connectionOperation == QStringLiteral("idle")) {
            updateIdleStatus();
        }
    });
    connect(&m_bluez, &BluezDeviceManager::connectionRequestFinished, this,
            [this](const QString &path, bool connected, const QString &errorName,
                   const QString &errorMessage) {
        if (path != m_connectionPath || connected != m_connectionTargetConnected)
            return;
        if (errorName.isEmpty()) {
            m_bluez.refresh();
            evaluateConnectionOperation();
            return;
        }

        const bool idempotent = (connected && errorName.endsWith(QStringLiteral("AlreadyConnected")))
            || (!connected && errorName.endsWith(QStringLiteral("NotConnected")));
        if (idempotent || errorName.endsWith(QStringLiteral("InProgress"))) {
            m_bluez.refresh();
            return;
        }
        finishConnectionOperation(errorMessage.isEmpty()
                                      ? QStringLiteral("Bluetooth connection request failed")
                                      : errorMessage);
    });
    connect(&m_monitor, &PipeWireMonitor::devicesChanged, this,
            [this](const QVariantList &devices) {
        m_pipewireDevices = devices;
        rebuildDevices();
        if (m_pendingCodec.isEmpty() && !m_capabilityRefresh
            && m_connectionOperation == QStringLiteral("idle")) {
            updateIdleStatus();
        }
    });
    connect(&m_monitor, &PipeWireMonitor::connectionChanged, this,
            [this](bool connected, const QString &error) {
        if (!connected)
            setStatusMessage(error);
        else if (!m_busy)
            updateIdleStatus();
    });

    QString error;
    if (!m_bluez.start(&error))
        setStatusMessage(error);
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
    if (!m_busy)
        updateIdleStatus();
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

QString AppController::connectionOperation() const { return m_connectionOperation; }

QString AppController::statusMessage() const { return m_statusMessage; }
bool AppController::busy() const { return m_busy; }

void AppController::refresh()
{
    m_bluez.refresh();
    m_monitor.refresh();
    setStatusMessage(QStringLiteral("Refreshing"));
}

void AppController::connectSelectedDevice()
{
    if (m_busy)
        return;
    const QString path = selectedBluezPath();
    if (path.isEmpty()) {
        setStatusMessage(QStringLiteral("No Bluetooth device is selected"));
        return;
    }
    if (deviceByPath(path).value(QStringLiteral("connected")).toBool()) {
        setStatusMessage(QStringLiteral("Bluetooth device is already connected"));
        return;
    }

    m_connectionPath = path;
    m_connectionTargetConnected = true;
    m_connectionOperation = QStringLiteral("connecting");
    emit connectionOperationChanged();
    setBusy(true);
    rebuildDevices();
    setStatusMessage(QStringLiteral("Connecting Bluetooth device"));
    m_connectionTimer.start(20000);
    m_bluez.connectDevice(path);
}

void AppController::disconnectSelectedDevice()
{
    if (m_busy)
        return;
    const QString path = selectedBluezPath();
    if (path.isEmpty()) {
        setStatusMessage(QStringLiteral("No Bluetooth device is selected"));
        return;
    }
    if (!deviceByPath(path).value(QStringLiteral("connected")).toBool()) {
        setStatusMessage(QStringLiteral("Bluetooth device is already disconnected"));
        return;
    }

    m_connectionPath = path;
    m_connectionTargetConnected = false;
    m_connectionOperation = QStringLiteral("disconnecting");
    emit connectionOperationChanged();
    setBusy(true);
    rebuildDevices();
    setStatusMessage(QStringLiteral("Disconnecting Bluetooth device"));
    m_connectionTimer.start(10000);
    m_bluez.disconnectDevice(path);
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
    if (m_busy)
        return;
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
    if (m_busy)
        return;
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

QVariantMap AppController::deviceByPath(const QString &path) const
{
    for (const QVariant &value : m_devices) {
        const QVariantMap device = value.toMap();
        if (device.value(QStringLiteral("bluezPath")).toString() == path)
            return device;
    }
    return {};
}

void AppController::rebuildDevices()
{
    const QString oldPath = selectedBluezPath();
    const QString oldAddress = selectedAddress();
    QVariantList merged;
    QSet<qsizetype> usedPipewireDevices;

    for (const QVariant &bluezValue : std::as_const(m_bluezDevices)) {
        QVariantMap entry = bluezValue.toMap();
        const QString path = entry.value(QStringLiteral("bluezPath")).toString();
        const QString address = entry.value(QStringLiteral("address")).toString();

        qsizetype match = -1;
        for (qsizetype i = 0; i < m_pipewireDevices.size(); ++i) {
            const QVariantMap pipewire = m_pipewireDevices.at(i).toMap();
            const bool samePath = !path.isEmpty()
                && pipewire.value(QStringLiteral("bluezPath")).toString() == path;
            const bool sameAddress = !address.isEmpty()
                && pipewire.value(QStringLiteral("address")).toString()
                    .compare(address, Qt::CaseInsensitive) == 0;
            if (samePath || sameAddress) {
                match = i;
                break;
            }
        }

        if (match >= 0)
            usedPipewireDevices.insert(match);

        if (match >= 0 && entry.value(QStringLiteral("connected")).toBool()) {
            const QVariantMap pipewire = m_pipewireDevices.at(match).toMap();
            for (const QString &name : {QStringLiteral("id"), QStringLiteral("profiles"),
                                        QStringLiteral("codec"), QStringLiteral("profile"),
                                        QStringLiteral("format"), QStringLiteral("rate"),
                                        QStringLiteral("channels")}) {
                entry.insert(name, pipewire.value(name));
            }
        } else {
            entry.insert(QStringLiteral("profiles"), QVariantList{});
            entry.insert(QStringLiteral("codec"), QString{});
            entry.insert(QStringLiteral("profile"), QString{});
            entry.insert(QStringLiteral("format"), QString{});
            entry.insert(QStringLiteral("rate"), 0);
            entry.insert(QStringLiteral("channels"), 0);
        }
        entry.insert(QStringLiteral("connectionOperation"),
                     path == m_connectionPath ? m_connectionOperation : QStringLiteral("idle"));
        entry.insert(QStringLiteral("displayName"),
                     QStringLiteral("%1 (%2)")
                         .arg(entry.value(QStringLiteral("name")).toString(),
                              entry.value(QStringLiteral("connected")).toBool()
                                  ? QStringLiteral("Connected") : QStringLiteral("Disconnected")));
        merged.append(entry);
    }

    // Keep a live PipeWire transport visible during BlueZ startup or daemon re-enumeration.
    for (qsizetype i = 0; i < m_pipewireDevices.size(); ++i) {
        if (usedPipewireDevices.contains(i))
            continue;
        QVariantMap entry = m_pipewireDevices.at(i).toMap();
        entry.insert(QStringLiteral("paired"), true);
        entry.insert(QStringLiteral("servicesResolved"), true);
        entry.insert(QStringLiteral("connectionOperation"),
                     entry.value(QStringLiteral("bluezPath")).toString() == m_connectionPath
                         ? m_connectionOperation : QStringLiteral("idle"));
        entry.insert(QStringLiteral("displayName"),
                     QStringLiteral("%1 (%2)")
                         .arg(entry.value(QStringLiteral("name")).toString(),
                              entry.value(QStringLiteral("connected")).toBool()
                                  ? QStringLiteral("Connected") : QStringLiteral("Disconnected")));
        merged.append(entry);
    }

    std::sort(merged.begin(), merged.end(), [](const QVariant &left, const QVariant &right) {
        const QVariantMap a = left.toMap();
        const QVariantMap b = right.toMap();
        if (a.value(QStringLiteral("connected")).toBool()
            != b.value(QStringLiteral("connected")).toBool()) {
            return a.value(QStringLiteral("connected")).toBool();
        }
        return QString::localeAwareCompare(a.value(QStringLiteral("name")).toString(),
                                           b.value(QStringLiteral("name")).toString()) < 0;
    });

    m_devices = std::move(merged);
    m_selectedDevice = -1;
    for (qsizetype i = 0; i < m_devices.size(); ++i) {
        const QVariantMap device = m_devices.at(i).toMap();
        if ((!oldPath.isEmpty()
             && device.value(QStringLiteral("bluezPath")).toString() == oldPath)
            || (oldPath.isEmpty() && !oldAddress.isEmpty()
                && device.value(QStringLiteral("address")).toString()
                       .compare(oldAddress, Qt::CaseInsensitive) == 0)) {
            m_selectedDevice = static_cast<int>(i);
            break;
        }
    }
    if (m_selectedDevice < 0 && !m_connectionPath.isEmpty()) {
        for (qsizetype i = 0; i < m_devices.size(); ++i) {
            if (m_devices.at(i).toMap().value(QStringLiteral("bluezPath")).toString()
                == m_connectionPath) {
                m_selectedDevice = static_cast<int>(i);
                break;
            }
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
    evaluateConnectionOperation();
}

void AppController::evaluateConnectionOperation()
{
    if (m_connectionOperation == QStringLiteral("idle") || m_connectionPath.isEmpty())
        return;

    const QVariantMap device = deviceByPath(m_connectionPath);
    if (!m_connectionTargetConnected) {
        if (device.isEmpty() || !device.value(QStringLiteral("connected")).toBool())
            finishConnectionOperation(QStringLiteral("Bluetooth device disconnected"));
        return;
    }

    const bool transportReady = device.value(QStringLiteral("connected")).toBool()
        && !device.value(QStringLiteral("profiles")).toList().isEmpty();
    if (transportReady)
        finishConnectionOperation(QStringLiteral("Bluetooth device connected"));
}

void AppController::finishConnectionOperation(const QString &message)
{
    if (m_connectionOperation == QStringLiteral("idle"))
        return;
    m_connectionTimer.stop();
    m_connectionPath.clear();
    m_connectionTargetConnected = false;
    m_connectionOperation = QStringLiteral("idle");
    emit connectionOperationChanged();
    setBusy(false);
    rebuildDevices();
    setStatusMessage(message);
}

void AppController::updateIdleStatus()
{
    if (m_devices.isEmpty()) {
        setStatusMessage(QStringLiteral("No paired Bluetooth audio device"));
        return;
    }
    const QVariantMap selected = m_selectedDevice >= 0 && m_selectedDevice < m_devices.size()
        ? m_devices.at(m_selectedDevice).toMap() : QVariantMap{};
    if (!selected.value(QStringLiteral("connected")).toBool()) {
        setStatusMessage(QStringLiteral("Disconnected"));
    } else if (selected.value(QStringLiteral("profiles")).toList().isEmpty()) {
        setStatusMessage(QStringLiteral("Connected, waiting for audio transport"));
    } else {
        setStatusMessage(QStringLiteral("Ready"));
    }
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
    setStatusMessage(QStringLiteral("Reconnecting Bluetooth device"));
    m_bluez.connectDevice(m_pendingBluezPath);
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

    const bool connected = !device.isEmpty()
        && device.value(QStringLiteral("connected")).toBool();
    const bool transportReady = connected
        && !device.value(QStringLiteral("profiles")).toList().isEmpty();
    QString error;
    if (!m_profileSelectionIssued && transportReady
        && m_monitor.selectCodec(m_pendingAddress, m_pendingCodec, &error)) {
        m_profileSelectionIssued = true;
        setStatusMessage(QStringLiteral("Switching codec"));
    } else if (!connected && m_retryCount % 4 == 0) {
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
