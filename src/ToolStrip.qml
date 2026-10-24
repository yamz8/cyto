import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The gate tools and the plot type, down the side of the plot.
Rectangle {
    id: root
    property var plot
    property bool statsVisible: false
    signal pickTool(int tool)

    implicitWidth: 48
    color: "#121215"

    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: "#24242b"
    }

    ColumnLayout {
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: 10
        spacing: 4

        Repeater {
            model: [
                { tool: 0, kind: "select", tip: qsTr("Select, move, and reshape gates (V)") },
                { tool: 1, kind: "rectangle", tip: qsTr("Draw a rectangle gate (R)") },
                { tool: 2, kind: "polygon", tip: qsTr("Draw a polygon gate (P): click the corners, Enter to close") },
                { tool: 3, kind: "ellipse", tip: qsTr("Draw an ellipse gate (E)") }
            ]
            delegate: IconButton {
                required property var modelData
                kind: modelData.kind
                tip: modelData.tip
                checked: root.plot && root.plot.tool === modelData.tool && !root.statsVisible
                onClicked: root.pickTool(modelData.tool)
            }
        }

        Rectangle { Layout.preferredWidth: 26; Layout.preferredHeight: 1; Layout.alignment: Qt.AlignHCenter; Layout.topMargin: 6; Layout.bottomMargin: 6; color: "#2c2c34" }

        IconButton {
            kind: "dots"
            checked: !backend.histogram
            tip: qsTr("Dot plot (H toggles)")
            onClicked: backend.histogram = false
        }
        IconButton {
            kind: "histogram"
            checked: backend.histogram
            tip: qsTr("Histogram, where gates are ranges (H toggles)")
            onClicked: backend.histogram = true
        }
    }
}
