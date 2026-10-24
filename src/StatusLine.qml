import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Where you are, how many events that is, and what the pointer is over.
Rectangle {
    id: root
    property string pointer
    property bool drawing
    property int tool

    implicitHeight: 30
    color: "#141417"

    readonly property var current: {
        const list = backend.populations
        for (let i = 0; i < list.length; ++i)
            if (list[i].id === backend.currentPopulation)
                return list[i]
        return null
    }
    readonly property string hint: drawing && tool === 2
        ? qsTr("Click to add corners · click the first corner, double-click, or Enter to close · Esc cancels")
        : tool !== 0 ? qsTr("Drag on the plot to draw the gate · Esc cancels")
        : backend.selectedGate !== 0 ? qsTr("Drag to move · drag a handle to reshape · double-click or Enter to look inside · Delete removes")
        : ""

    Timer {
        id: noticeTimer
        interval: 3500
        onTriggered: backend.dismissNotice()
    }
    Connections {
        target: backend
        function onNoticeChanged() { if (backend.notice !== "") noticeTimer.restart() }
    }

    Rectangle {
        width: parent.width
        height: 1
        color: "#24242b"
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        spacing: 16

        Label {
            visible: backend.samples.length > 0
            text: backend.samples.length > 0 && backend.currentSample >= 0
                  ? qsTr("Sample %1 of %2").arg((backend.currentSample + 1).toLocaleString(Qt.locale(), 'f', 0))
                                         .arg(backend.samples.length.toLocaleString(Qt.locale(), 'f', 0))
                  : ""
            color: "#9a9aa4"
            font.pixelSize: 12
        }
        Label {
            visible: root.current !== null && backend.samples.length > 0
            text: root.current ? qsTr("%1: %Ln events", "", root.current.count).arg(root.current.name) : ""
            color: "#d6d6dc"
            font.pixelSize: 12
        }
        Label {
            visible: backend.compensate
            text: qsTr("Compensated")
            color: theme.accent
            font.pixelSize: 12
        }
        Label {
            Layout.fillWidth: true
            text: backend.notice !== "" ? backend.notice : root.hint
            color: backend.notice !== "" ? theme.accent : "#7a7a84"
            elide: Text.ElideRight
            font.pixelSize: 12
        }
        Label {
            text: root.pointer
            color: "#9a9aa4"
            font.pixelSize: 12
            font.family: "monospace"
        }
        Label {
            text: qsTr("? shortcuts")
            color: "#55555e"
            font.pixelSize: 12
        }
    }
}
