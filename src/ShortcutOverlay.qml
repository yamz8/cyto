import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Every shortcut, shown with ?.
Popup {
    id: root
    modal: true
    anchors.centerIn: Overlay.overlay
    padding: 24
    // Three columns when the window is wide enough; otherwise two, scrolling when short.
    width: Math.min(1280, parent ? parent.width - 40 : 820)
    height: Math.min(implicitHeight, parent ? parent.height - 40 : implicitHeight)
    Material.roundedScale: theme.radius
    focus: true

    Overlay.modal: Rectangle { color: "#a0000000" }

    background: Rectangle {
        color: "#17171b"
        border.color: "#2a2a31"
        radius: theme.radius
    }

    readonly property var groups: [
        { title: qsTr("Gates"), keys: [
            ["V", qsTr("Select, move, and reshape")],
            ["R", qsTr("Draw a rectangle (a range on a histogram)")],
            ["P", qsTr("Draw a polygon")],
            ["E", qsTr("Draw an ellipse")],
            ["Enter", qsTr("Close the polygon, or look inside the gate")],
            [qsTr("Double-click"), qsTr("Look inside a gate")],
            ["F2", qsTr("Rename the population")],
            ["Delete", qsTr("Delete the gate and the gates inside it")],
            ["Esc", qsTr("Cancel, or deselect")],
            ["Ctrl + Z", qsTr("Undo")],
            ["Ctrl + Shift + Z", qsTr("Redo (or Ctrl + Y)")]
        ]},
        { title: qsTr("Plot"), keys: [
            ["X  /  Shift + X", qsTr("Next or previous X channel")],
            ["Y  /  Shift + Y", qsTr("Next or previous Y channel")],
            ["S", qsTr("Swap the axes")],
            ["L  /  Shift + L", qsTr("X or Y scale: linear, log, arcsinh")],
            ["H", qsTr("Dot plot or histogram")],
            ["↑ ↓", qsTr("Previous or next population")],
            ["←", qsTr("Up to the parent population")],
            ["Space", qsTr("Next sample")],
            ["Shift + Space", qsTr("Previous sample")]
        ]},
        { title: qsTr("Analysis"), keys: [
            ["C", qsTr("Compensation on or off")],
            ["Shift + C", qsTr("Edit the compensation matrix")],
            ["T", qsTr("Statistics table")],
            ["M", qsTr("% of parent, % of total, or events")],
            ["Ctrl + O", qsTr("Add FCS files")],
            ["Ctrl + Shift + O", qsTr("Open a workspace")],
            ["Ctrl + S", qsTr("Save the workspace")],
            ["Ctrl + Shift + S", qsTr("Save as")],
            ["Ctrl + E", qsTr("Export statistics as CSV")],
            ["Shift + Delete", qsTr("Remove the sample")],
            ["?", qsTr("Show or hide shortcuts")],
            ["Q", qsTr("Quit")]
        ]}
    ]

    contentItem: Flickable {
        id: scroller
        implicitHeight: content.implicitHeight
        contentHeight: content.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        focus: true
        ScrollBar.vertical: ScrollBar { policy: scroller.contentHeight > scroller.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff }
        Keys.onPressed: (event) => {
            if (event.text === "?") {
                root.close()
                event.accepted = true
                return
            }
            // When the window is too short for every shortcut, the arrows scroll.
            const step = event.key === Qt.Key_Down ? 40 : event.key === Qt.Key_Up ? -40
                       : event.key === Qt.Key_PageDown ? height : event.key === Qt.Key_PageUp ? -height : 0
            if (step !== 0) {
                contentY = Math.max(0, Math.min(contentHeight - height, contentY + step))
                event.accepted = true
            }
        }

        ColumnLayout {
            id: content
            width: scroller.width - (scroller.contentHeight > scroller.height ? 14 : 0)
            spacing: 16
            Label {
                text: qsTr("Keyboard shortcuts")
                font.pixelSize: 18
                font.bold: true
            }
            GridLayout {
                columns: root.width >= 1200 ? 3 : 2
                columnSpacing: 32
                rowSpacing: 18
                Repeater {
                    model: root.groups
                    delegate: ColumnLayout {
                        required property var modelData
                        Layout.alignment: Qt.AlignTop
                        Layout.fillWidth: true
                        spacing: 6
                        Label {
                            text: modelData.title
                            color: theme.accent
                            font.pixelSize: 12
                            font.capitalization: Font.AllUppercase
                            font.letterSpacing: 0.6
                        }
                        Repeater {
                            model: modelData.keys
                            delegate: RowLayout {
                                required property var modelData
                                spacing: 12
                                Rectangle {
                                    Layout.preferredWidth: 136
                                    Layout.preferredHeight: 22
                                    radius: Math.min(4, theme.radius)
                                    color: "#232329"
                                    Label {
                                        anchors.centerIn: parent
                                        text: modelData[0]
                                        font.pixelSize: 12
                                        color: "#d6d6dc"
                                    }
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: modelData[1]
                                    font.pixelSize: 13
                                    color: "#b4b4bc"
                                    wrapMode: Text.Wrap
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
