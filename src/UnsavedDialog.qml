import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Asked before unsaved work would be lost: on quit, or opening another workspace.
Dialog {
    id: root
    modal: true
    anchors.centerIn: Overlay.overlay
    width: 440
    title: qsTr("Save changes to %1?").arg(backend.fileName)
    Material.roundedScale: theme.radius

    background: Rectangle {
        color: "#1b1b20"
        border.color: "#2c2c34"
        radius: theme.radius
    }
    Overlay.modal: Rectangle { color: "#a0000000" }
    header: Label {
        text: root.title
        padding: 24
        bottomPadding: 4
        font.pixelSize: 17
        font.bold: true
        elide: Text.ElideRight
    }

    signal saveChosen()
    signal discardChosen()
    signal cancelChosen()

    Label {
        width: parent.width
        wrapMode: Text.Wrap
        color: "#b4b4bc"
        text: qsTr("Your gates, samples, or compensation have changes that aren't saved in a workspace yet.")
    }

    footer: DialogButtonBox {
        background: Item {}
        Button {
            text: qsTr("Don't save")
            flat: true
            onClicked: { root.close(); root.discardChosen() }
        }
        Button {
            text: qsTr("Cancel")
            flat: true
            onClicked: { root.close(); root.cancelChosen() }
        }
        Button {
            id: saveButton
            text: qsTr("Save")
            highlighted: true
            onClicked: { root.close(); root.saveChosen() }
            Keys.onReturnPressed: clicked()
        }
    }
    onOpened: saveButton.forceActiveFocus()
    onRejected: cancelChosen()
}
