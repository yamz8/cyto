import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// What the window shows before there's anything to gate.
ColumnLayout {
    id: root
    signal addFiles()
    signal openWorkspace()
    spacing: 14
    width: 460
    Material.roundedScale: theme.radius

    ToolIcon {
        Layout.alignment: Qt.AlignHCenter
        Layout.preferredWidth: 54
        Layout.preferredHeight: 54
        kind: "dots"
        color: theme.accent
        scale: 3
    }
    Label {
        Layout.fillWidth: true
        Layout.topMargin: 8
        horizontalAlignment: Text.AlignHCenter
        text: qsTr("Drop FCS files here")
        font.pixelSize: 22
        font.bold: true
    }
    Label {
        Layout.fillWidth: true
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        color: "#9a9aa4"
        font.pixelSize: 14
        text: qsTr("Gate one sample, and the same gates apply to every sample you add. Your files are only read, never changed.")
    }
    RowLayout {
        Layout.alignment: Qt.AlignHCenter
        Layout.topMargin: 6
        spacing: 10
        Button {
            text: qsTr("Add FCS files…")
            highlighted: true
            onClicked: root.addFiles()
        }
        Button {
            text: qsTr("Open workspace…")
            flat: true
            onClicked: root.openWorkspace()
        }
    }
    Label {
        Layout.fillWidth: true
        horizontalAlignment: Text.AlignHCenter
        color: "#6c6c76"
        font.pixelSize: 12
        text: qsTr("Ctrl + O adds files · Ctrl + Shift + O opens a workspace · ? lists every shortcut")
    }
}
