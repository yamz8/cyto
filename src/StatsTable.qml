import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Every population in every sample, gated the same way.
Item {
    id: root
    Material.roundedScale: theme.radius

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Label {
                text: qsTr("Statistics")
                font.pixelSize: 18
                font.bold: true
            }
            Item { Layout.preferredWidth: 12 }
            // Which number the cells show, as one segmented control.
            Rectangle {
                implicitWidth: measures.implicitWidth + 4
                implicitHeight: 32
                radius: Math.min(6, theme.radius)
                color: "#1b1b20"
                border.color: "#2c2c34"
                Row {
                    id: measures
                    anchors.centerIn: parent
                    spacing: 2
                    Repeater {
                        model: [qsTr("% of parent"), qsTr("% of total"), qsTr("Events")]
                        delegate: Rectangle {
                            id: segment
                            required property string modelData
                            required property int index
                            readonly property bool current: backend.stats.measure === index
                            width: segmentLabel.implicitWidth + 24
                            height: 28
                            radius: Math.min(4, theme.radius)
                            color: current ? theme.accent : segmentArea.containsMouse ? "#26262d" : "transparent"
                            Label {
                                id: segmentLabel
                                anchors.centerIn: parent
                                text: segment.modelData
                                font.pixelSize: 13
                                color: segment.current ? theme.accentForeground : "#c8c8d0"
                            }
                            MouseArea {
                                id: segmentArea
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: backend.stats.measure = segment.index
                            }
                        }
                    }
                }
            }
            Label {
                text: qsTr("M switches")
                color: "#6c6c76"
                font.pixelSize: 12
                Layout.leftMargin: 6
            }
            Item { Layout.fillWidth: true }
            Button {
                text: qsTr("Export CSV…")
                highlighted: true
                onClicked: backend.exportStats()
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: qsTr("Counts, percentages, and every channel's median (Ctrl + E)")
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            HorizontalHeaderView {
                id: header
                Layout.fillWidth: true
                syncView: table
                clip: true
                delegate: Rectangle {
                    required property var model
                    required property int column
                    implicitHeight: 34
                    implicitWidth: 120
                    color: "#17171b"
                    Label {
                        anchors.fill: parent
                        anchors.margins: 8
                        text: model.display
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignVCenter
                        horizontalAlignment: column === 0 ? Text.AlignLeft : Text.AlignRight
                        font.pixelSize: 12
                        font.bold: true
                        color: column === 0 ? "#b4b4bc" : theme.accent
                    }
                    HoverHandler { id: headerHover }
                    ToolTip.visible: headerHover.hovered && column > 0
                    ToolTip.delay: 500
                    ToolTip.text: backend.stats.headerData(column, Qt.Horizontal, Qt.UserRole + 3) || ""
                }
            }

            TableView {
                id: table
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: backend.stats
                boundsBehavior: Flickable.StopAtBounds
                columnSpacing: 0
                rowSpacing: 0
                columnWidthProvider: (column) => column === 0 ? 230 : 128
                ScrollBar.vertical: ScrollBar {}
                ScrollBar.horizontal: ScrollBar {}
                delegate: Rectangle {
                    required property var model
                    required property int row
                    required property int column
                    implicitHeight: 40
                    color: row === backend.currentSample ? "#1d1d24" : row % 2 ? "#121215" : "#0f0f12"
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 6
                        Label {
                            Layout.fillWidth: true
                            text: model.display
                            elide: Text.ElideMiddle
                            horizontalAlignment: column === 0 ? Text.AlignLeft : Text.AlignRight
                            font.pixelSize: 13
                            color: model.available ? "#e4e4ea" : "#55555e"
                        }
                        Label {
                            visible: text !== "" && column > 0
                            text: model.detail
                            font.pixelSize: 11
                            color: "#6c6c76"
                        }
                    }
                    TapHandler { onTapped: backend.currentSample = row }
                }
            }
        }
    }
}
