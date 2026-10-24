import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The samples, and the gate tree with each population's share of its parent
// in the current sample.
Rectangle {
    id: root
    signal renameRequested(int id)
    color: "#121215"
    Material.roundedScale: theme.radius

    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: "#24242b"
    }

    component SectionTitle: RowLayout {
        property alias text: title.text
        property alias detail: count.text
        default property alias extra: tail.data
        Layout.fillWidth: true
        Layout.leftMargin: 16
        Layout.rightMargin: 8
        Layout.topMargin: 12
        Label {
            id: title
            color: theme.accent
            font.pixelSize: 12
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 0.6
            font.bold: true
        }
        Label {
            id: count
            color: "#6c6c76"
            font.pixelSize: 12
        }
        Item { Layout.fillWidth: true }
        Row { id: tail }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.rightMargin: 1
        spacing: 4

        SectionTitle {
            text: qsTr("Samples")
            detail: backend.samples.length.toLocaleString(Qt.locale(), 'f', 0)
            ToolButton {
                text: "+"
                font.pixelSize: 17
                implicitWidth: 32
                implicitHeight: 32
                onClicked: backend.addSamples()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Add FCS files (Ctrl + O)")
            }
        }

        ListView {
            id: samples
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, root.height * 0.4)
            clip: true
            model: backend.samples
            currentIndex: backend.currentSample
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)
            delegate: ItemDelegate {
                id: sampleRow
                required property var modelData
                required property int index
                width: ListView.view.width
                height: 44
                highlighted: index === backend.currentSample
                onClicked: backend.currentSample = index
                contentItem: RowLayout {
                    spacing: 8
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Label {
                            Layout.fillWidth: true
                            text: sampleRow.modelData.name
                            elide: Text.ElideMiddle
                            font.pixelSize: 13
                            color: sampleRow.highlighted ? "#ffffff" : "#d6d6dc"
                        }
                        Label {
                            text: qsTr("%Ln events", "", sampleRow.modelData.events)
                            font.pixelSize: 11
                            color: "#7a7a84"
                        }
                    }
                    ToolButton {
                        visible: sampleRow.hovered
                        text: "✕"
                        implicitWidth: 28
                        implicitHeight: 28
                        font.pixelSize: 11
                        onClicked: backend.removeSample(sampleRow.index)
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Remove from the analysis (Shift + Delete); the file stays on disk")
                    }
                }
            }
        }

        SectionTitle {
            text: qsTr("Populations")
            detail: backend.samples.length > 0 && backend.currentSample >= 0
                    ? backend.samples[backend.currentSample].name : ""
        }

        ListView {
            id: populations
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: backend.populations
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            delegate: ItemDelegate {
                id: row
                required property var modelData
                width: ListView.view.width
                height: 40
                highlighted: modelData.id === backend.currentPopulation
                onClicked: backend.currentPopulation = modelData.id
                onDoubleClicked: if (modelData.id !== 0) root.renameRequested(modelData.id)
                ToolTip.visible: hovered && modelData.axes !== ""
                ToolTip.delay: 700
                ToolTip.text: modelData.axes
                contentItem: RowLayout {
                    spacing: 6
                    Item { Layout.preferredWidth: row.modelData.depth * 14 }
                    Rectangle {
                        Layout.preferredWidth: 8
                        Layout.preferredHeight: 8
                        radius: row.modelData.id === 0 ? 4 : Math.min(2, theme.radius)
                        color: row.modelData.id === backend.selectedGate || row.highlighted ? theme.accent : "#4a4a54"
                    }
                    Label {
                        Layout.fillWidth: true
                        text: row.modelData.name
                        elide: Text.ElideRight
                        font.pixelSize: 13
                        font.bold: row.highlighted
                        color: row.highlighted ? "#ffffff" : "#d6d6dc"
                    }
                    Label {
                        visible: row.modelData.id !== 0
                        text: row.modelData.available ? row.modelData.percent.toLocaleString(Qt.locale(), 'f', 1) + " %" : "—"
                        font.pixelSize: 12
                        color: theme.accent
                    }
                    Label {
                        Layout.preferredWidth: 64
                        horizontalAlignment: Text.AlignRight
                        text: row.modelData.available ? row.modelData.count.toLocaleString(Qt.locale(), 'f', 0) : ""
                        font.pixelSize: 12
                        color: "#7a7a84"
                    }
                }
            }
        }

        Label {
            Layout.fillWidth: true
            Layout.margins: 16
            visible: backend.populations.length <= 1
            wrapMode: Text.Wrap
            color: "#6c6c76"
            font.pixelSize: 12
            text: qsTr("Draw a gate on the plot with R, P, or E. Double-click a gate to look inside it.")
        }
    }
}
