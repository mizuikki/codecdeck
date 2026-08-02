import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.ApplicationWindow {
    id: root
    width: 860
    height: 540
    minimumWidth: 420
    minimumHeight: 500
    visible: true
    title: "CodecDeck"

    property var currentDevice: Backend.selectedDevice >= 0 && Backend.selectedDevice < Backend.devices.length
                                ? Backend.devices[Backend.selectedDevice] : ({})
    property var profiles: currentDevice.profiles || []
    property string selectedCodec: codecBox.currentIndex >= 0 && codecBox.currentIndex < profiles.length
                                   ? profiles[codecBox.currentIndex].codec : ""
    property string configurationCodec: Backend.busy && Backend.requestedCodec !== ""
                                        ? Backend.requestedCodec : selectedCodec
    property bool hasQualityControl: configurationCodec === "ldac" || configurationCodec === "lhdc_v5"
    property bool codecMismatch: Backend.requestedCodec !== ""
                                 && !!currentDevice.codec
                                 && Backend.requestedCodec !== currentDevice.codec
    property bool rateMismatch: Backend.configuredRate > 0
                                && currentDevice.rate > 0
                                && Backend.configuredRate !== currentDevice.rate

    function codecName(codec) {
        if (!codec)
            return "Not active"
        return codec === "lhdc_v5" ? "LHDC v5" : codec.toUpperCase()
    }

    function rateName(rate) {
        if (!rate)
            return "-"
        return (rate / 1000) + " kHz"
    }

    function profileIndex(codec) {
        for (let i = 0; i < profiles.length; ++i) {
            if (profiles[i].codec === codec)
                return i
        }
        return profiles.length > 0 ? 0 : -1
    }

    pageStack.initialPage: Kirigami.Page {
        title: "CodecDeck"
        padding: 0
        actions: [
            Kirigami.Action {
                text: "Refresh"
                icon.name: "view-refresh"
                enabled: !Backend.busy
                onTriggered: Backend.refresh()
            },
            Kirigami.Action {
                text: "Refresh Bluetooth capabilities"
                icon.name: "network-bluetooth"
                enabled: !!root.currentDevice.bluezPath && !Backend.busy
                onTriggered: Backend.refreshCapabilities()
            }
        ]

        ScrollView {
            anchors.fill: parent
            contentWidth: availableWidth

            ColumnLayout {
                width: parent.width
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: statusRow.implicitHeight + Kirigami.Units.largeSpacing * 2
                    color: Kirigami.Theme.alternateBackgroundColor
                    RowLayout {
                        id: statusRow
                        anchors.fill: parent
                        anchors.margins: Kirigami.Units.largeSpacing
                        spacing: Kirigami.Units.smallSpacing
                        Kirigami.Icon {
                            source: Backend.connectionOperation !== "idle"
                                    ? "network-transmit-receive"
                                    : root.currentDevice.connected ? "network-connect" : "network-disconnect"
                            implicitWidth: Kirigami.Units.iconSizes.smallMedium
                            implicitHeight: implicitWidth
                        }
                        Label {
                            text: Backend.statusMessage
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Label {
                            visible: !!root.currentDevice.address
                            text: root.currentDevice.address || ""
                            color: Kirigami.Theme.disabledTextColor
                            font.family: "monospace"
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: Kirigami.Units.gridUnit
                    spacing: Kirigami.Units.largeSpacing

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing
                        Kirigami.Heading { text: "Device"; level: 2 }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing

                            ComboBox {
                                id: deviceBox
                                Layout.fillWidth: true
                                model: Backend.devices
                                textRole: "displayName"
                                currentIndex: Backend.selectedDevice
                                enabled: count > 0 && !Backend.busy
                                onActivated: Backend.selectedDevice = currentIndex
                            }

                            Button {
                                Layout.minimumWidth: 148
                                text: Backend.connectionOperation === "connecting"
                                      ? "Connecting..."
                                      : Backend.connectionOperation === "disconnecting"
                                        ? "Disconnecting..."
                                        : root.currentDevice.connected ? "Disconnect" : "Connect"
                                icon.name: root.currentDevice.connected
                                           ? "network-disconnect" : "network-connect"
                                enabled: !!root.currentDevice.bluezPath && !Backend.busy
                                onClicked: {
                                    if (root.currentDevice.connected)
                                        Backend.disconnectSelectedDevice()
                                    else
                                        Backend.connectSelectedDevice()
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 1
                        color: Qt.alpha(Kirigami.Theme.textColor, 0.18)
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: width >= 700 ? 2 : 1
                        columnSpacing: Kirigami.Units.gridUnit * 2
                        rowSpacing: Kirigami.Units.largeSpacing

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignTop
                            spacing: Kirigami.Units.largeSpacing

                            Kirigami.Heading { text: "Active transport"; level: 2 }

                            GridLayout {
                                Layout.fillWidth: true
                                columns: 2
                                columnSpacing: Kirigami.Units.largeSpacing
                                rowSpacing: Kirigami.Units.smallSpacing

                                Label { text: "Codec"; color: Kirigami.Theme.disabledTextColor }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: Kirigami.Units.smallSpacing
                                    Kirigami.Icon {
                                        visible: root.codecMismatch
                                        source: "dialog-warning"
                                        color: Kirigami.Theme.neutralTextColor
                                        implicitWidth: Kirigami.Units.iconSizes.small
                                        implicitHeight: implicitWidth
                                        ToolTip.visible: codecWarningHover.hovered
                                        ToolTip.text: "The requested codec is not active."
                                        HoverHandler { id: codecWarningHover }
                                    }
                                    Label {
                                        text: root.codecName(root.currentDevice.codec)
                                        font.weight: Font.DemiBold
                                    }
                                    Label {
                                        visible: root.codecMismatch
                                        text: "Requested " + root.codecName(Backend.requestedCodec)
                                        color: Kirigami.Theme.neutralTextColor
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                }
                                Label { text: "Sample rate"; color: Kirigami.Theme.disabledTextColor }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: Kirigami.Units.smallSpacing
                                    Kirigami.Icon {
                                        visible: root.rateMismatch
                                        source: "dialog-warning"
                                        color: Kirigami.Theme.neutralTextColor
                                        implicitWidth: Kirigami.Units.iconSizes.small
                                        implicitHeight: implicitWidth
                                        ToolTip.visible: rateWarningHover.hovered
                                        ToolTip.text: "The negotiated sample rate differs from the configured rate."
                                        HoverHandler { id: rateWarningHover }
                                    }
                                    Label { text: root.rateName(root.currentDevice.rate) }
                                    Label {
                                        visible: root.rateMismatch
                                        text: "Configured " + root.rateName(Backend.configuredRate)
                                        color: Kirigami.Theme.neutralTextColor
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                }
                                Label { text: "PCM format"; color: Kirigami.Theme.disabledTextColor }
                                Label { text: root.currentDevice.format || "-" }
                                Label { text: "Channels"; color: Kirigami.Theme.disabledTextColor }
                                Label { text: root.currentDevice.channels || "-" }
                                Label { text: "Profile"; color: Kirigami.Theme.disabledTextColor }
                                Label {
                                    text: root.currentDevice.profile || "-"
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignTop
                            spacing: Kirigami.Units.largeSpacing

                            Kirigami.Heading { text: "Codec"; level: 2 }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: Kirigami.Units.smallSpacing
                                Label { text: "A2DP codec" }
                                ComboBox {
                                    id: codecBox
                                    Layout.fillWidth: true
                                    model: root.profiles
                                    textRole: "label"
                                    enabled: root.currentDevice.connected && count > 0 && !Backend.busy
                                    Component.onCompleted: currentIndex = root.profileIndex(root.currentDevice.codec || "")
                                    Connections {
                                        target: Backend
                                        function onDevicesChanged() {
                                            codecBox.currentIndex = root.profileIndex(root.currentDevice.codec || "")
                                        }
                                    }
                                }
                            }

                            Button {
                                text: "Switch codec"
                                icon.name: "media-playback-start"
                                enabled: root.currentDevice.connected
                                         && root.selectedCodec !== ""
                                         && root.selectedCodec !== root.currentDevice.codec
                                         && !Backend.busy
                                onClicked: Backend.switchCodec(root.selectedCodec)
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 1
                        color: Qt.alpha(Kirigami.Theme.textColor, 0.18)
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: width >= 700 ? 2 : 1
                        columnSpacing: Kirigami.Units.gridUnit * 2
                        rowSpacing: Kirigami.Units.largeSpacing

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing
                            Kirigami.Heading { text: "Preferred sample rate"; level: 2 }
                            ComboBox {
                                id: rateBox
                                Layout.fillWidth: true
                                enabled: root.currentDevice.connected
                                         && root.configurationCodec !== "" && !Backend.busy
                                property var supportedValues: root.configurationCodec === ""
                                                              ? [Backend.preferredRate]
                                                              : root.configurationCodec === "ldac"
                                                              ? [44100, 48000, 88200, 96000]
                                                              : root.configurationCodec === "lhdc_v5"
                                                                ? [44100, 48000, 96000, 192000]
                                                                : [44100, 48000]
                                property var values: supportedValues.indexOf(Backend.preferredRate) >= 0
                                                     ? supportedValues
                                                     : [Backend.preferredRate].concat(supportedValues)
                                model: values.map(function(rate) {
                                    const unavailable = supportedValues.indexOf(rate) < 0
                                    return root.rateName(rate)
                                           + (unavailable
                                              ? " (not available for " + root.codecName(root.configurationCodec) + ")"
                                              : "")
                                })
                                currentIndex: values.indexOf(Backend.preferredRate)
                                onActivated: function(index) {
                                    Backend.preferredRate = values[index]
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing
                            Kirigami.Heading { text: "Quality"; level: 2 }
                            ComboBox {
                                id: qualityBox
                                Layout.fillWidth: true
                                model: root.configurationCodec === "ldac"
                                       ? ["Auto", "High (990/909 kbit/s)", "Standard (660/606 kbit/s)", "Mobile (330/303 kbit/s)"]
                                       : root.configurationCodec === "lhdc_v5"
                                         ? ["Auto", "400 kbit/s", "500 kbit/s", "900 kbit/s", "1000 kbit/s"]
                                         : ["System default"]
                                property var values: root.configurationCodec === "ldac"
                                                     ? ["auto", "hq", "sq", "mq"]
                                                     : root.configurationCodec === "lhdc_v5"
                                                       ? ["auto", "400k", "500k", "900k", "1000k"]
                                                       : ["system"]
                                enabled: root.currentDevice.connected
                                         && root.hasQualityControl && !Backend.busy
                                Component.onCompleted: {
                                    const value = root.configurationCodec === "ldac" ? Backend.ldacQuality : Backend.lhdcQuality
                                    currentIndex = Math.max(0, values.indexOf(value))
                                }
                                onValuesChanged: {
                                    const value = root.configurationCodec === "ldac" ? Backend.ldacQuality : Backend.lhdcQuality
                                    currentIndex = Math.max(0, values.indexOf(value))
                                }
                                onActivated: {
                                    if (root.configurationCodec === "ldac")
                                        Backend.ldacQuality = values[currentIndex]
                                    else
                                        Backend.lhdcQuality = values[currentIndex]
                                }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: Kirigami.Units.smallSpacing
                        Item { Layout.fillWidth: true }
                        Button {
                            text: "Apply and reconnect"
                            icon.name: "dialog-ok-apply"
                            highlighted: true
                            enabled: root.currentDevice.connected
                                     && root.selectedCodec !== "" && !Backend.busy
                                     && (Backend.hasPendingSettings
                                         || root.selectedCodec !== root.currentDevice.codec)
                            onClicked: Backend.apply(root.selectedCodec)
                        }
                    }
                }
            }
        }
    }
}
