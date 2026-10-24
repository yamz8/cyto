import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Axes, gate tools, and the switches for compensation and statistics.
Rectangle {
    id: root
    property bool statsVisible: false
    signal toggleStats()
    signal editCompensation()
    signal showHelp()

    implicitHeight: 52
    color: "#141417"
    Material.roundedScale: theme.radius

    readonly property var scaleNames: [qsTr("Linear"), qsTr("Log"), qsTr("Arcsinh")]
    readonly property bool enabledAxes: backend.samples.length > 0

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: "#24242b"
    }

    component AxisLabel: Label {
        color: "#8a8a94"
        font.pixelSize: 12
        font.bold: true
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 8
        spacing: 6

        AxisLabel { text: "X" }
        ComboBox {
            id: xBox
            Layout.fillWidth: true
            Layout.preferredWidth: 190
            Layout.maximumWidth: 190
            Layout.minimumWidth: 104
            enabled: root.enabledAxes
            model: backend.channelNames
            currentIndex: backend.channels.indexOf(backend.xChannel)
            onActivated: (index) => backend.xChannel = backend.channels[index]
            ToolTip.visible: hovered && !popup.visible
            ToolTip.delay: 600
            ToolTip.text: qsTr("X axis channel (X, Shift + X)")
        }
        ComboBox {
            Layout.fillWidth: true
            Layout.preferredWidth: 112
            Layout.maximumWidth: 112
            Layout.minimumWidth: 88
            enabled: root.enabledAxes
            model: root.scaleNames
            currentIndex: backend.xScale
            onActivated: (index) => backend.xScale = index
            ToolTip.visible: hovered && !popup.visible
            ToolTip.delay: 600
            ToolTip.text: qsTr("X axis scale (L)")
        }

        Item { Layout.preferredWidth: 6 }
        AxisLabel { text: "Y"; opacity: backend.histogram ? 0.35 : 1 }
        ComboBox {
            Layout.fillWidth: true
            Layout.preferredWidth: 190
            Layout.maximumWidth: 190
            Layout.minimumWidth: 104
            enabled: root.enabledAxes && !backend.histogram
            model: backend.channelNames
            currentIndex: backend.channels.indexOf(backend.yChannel)
            onActivated: (index) => backend.yChannel = backend.channels[index]
            ToolTip.visible: hovered && !popup.visible
            ToolTip.delay: 600
            ToolTip.text: qsTr("Y axis channel (Y, Shift + Y)")
        }
        ComboBox {
            Layout.fillWidth: true
            Layout.preferredWidth: 112
            Layout.maximumWidth: 112
            Layout.minimumWidth: 88
            enabled: root.enabledAxes && !backend.histogram
            model: root.scaleNames
            currentIndex: backend.yScale
            onActivated: (index) => backend.yScale = index
            ToolTip.visible: hovered && !popup.visible
            ToolTip.delay: 600
            ToolTip.text: qsTr("Y axis scale (Shift + L)")
        }
        IconButton {
            kind: "swap"
            checkable: false
            enabled: root.enabledAxes && !backend.histogram
            tip: qsTr("Swap the axes (S)")
            onClicked: backend.swapAxes()
        }

        Item { Layout.fillWidth: true }

        Switch {
            text: qsTr("Compensate")
            enabled: backend.compensationChannels.length > 0
            checked: backend.compensate
            onToggled: backend.compensate = checked
            ToolTip.visible: hovered
            ToolTip.delay: 600
            ToolTip.text: backend.compensationChannels.length > 0
                          ? qsTr("Apply the spillover matrix (C)")
                          : qsTr("None of the files carries a spillover matrix; add one with Matrix…")
        }
        Button {
            text: qsTr("Matrix…")
            flat: true
            enabled: root.enabledAxes
            onClicked: root.editCompensation()
            ToolTip.visible: hovered
            ToolTip.delay: 600
            ToolTip.text: qsTr("Edit the compensation matrix (Shift + C)")
        }
        Button {
            text: qsTr("Statistics")
            flat: true
            Material.foreground: root.statsVisible ? theme.accent : "#d6d6dc"
            enabled: root.enabledAxes
            onClicked: root.toggleStats()
            ToolTip.visible: hovered
            ToolTip.delay: 600
            ToolTip.text: qsTr("Every population in every sample (T)")
        }
        ToolButton {
            text: "?"
            font.pixelSize: 15
            onClicked: root.showHelp()
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Keyboard shortcuts (?)")
        }
    }
}
