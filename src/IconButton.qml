import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// A square tool button with a drawn icon, lit in the accent when it's on.
ToolButton {
    id: button
    property string kind
    property string tip
    checkable: true
    implicitWidth: 36
    implicitHeight: 36
    contentItem: Item {
        ToolIcon {
            anchors.centerIn: parent
            kind: button.kind
            color: button.checked ? theme.accent : button.enabled ? "#d6d6dc" : "#55555e"
        }
    }
    ToolTip.visible: hovered
    ToolTip.delay: 400
    ToolTip.text: tip
}
