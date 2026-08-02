import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.Dialog {
    title: "About CodecDeck"
    preferredWidth: Kirigami.Units.gridUnit * 24
    standardButtons: Kirigami.Dialog.Close

    Item {
        implicitWidth: Kirigami.Units.gridUnit * 22
        implicitHeight: aboutLayout.implicitHeight + Kirigami.Units.largeSpacing * 2

        ColumnLayout {
            id: aboutLayout
            anchors.fill: parent
            anchors.margins: Kirigami.Units.largeSpacing
            spacing: Kirigami.Units.smallSpacing

            Kirigami.Icon {
                source: "qrc:/icons/io.github.mizuikki.CodecDeck.png"
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: Kirigami.Units.iconSizes.huge
                Layout.preferredHeight: Kirigami.Units.iconSizes.huge
            }

            Kirigami.Heading {
                text: "CodecDeck"
                level: 2
                Layout.alignment: Qt.AlignHCenter
            }

            Label {
                text: "Version " + Qt.application.version
                color: Kirigami.Theme.disabledTextColor
                Layout.alignment: Qt.AlignHCenter
            }

            Label {
                text: "Control Bluetooth audio codecs on PipeWire and WirePlumber."
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                Layout.topMargin: Kirigami.Units.smallSpacing
            }

            Label {
                text: '<a href="https://github.com/mizuikki/codecdeck">github.com/mizuikki/codecdeck</a>'
                textFormat: Text.RichText
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: Kirigami.Units.smallSpacing
                onLinkActivated: function(link) { Qt.openUrlExternally(link) }
            }

            Label {
                text: '<a href="https://github.com/mizuikki/codecdeck/blob/main/LICENSE">MIT License</a>'
                textFormat: Text.RichText
                Layout.alignment: Qt.AlignHCenter
                onLinkActivated: function(link) { Qt.openUrlExternally(link) }
            }
        }
    }
}
