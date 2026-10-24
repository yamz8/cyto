import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Names a gate's population, as it appears in the tree and the statistics.
Dialog {
    id: root
    modal: true
    anchors.centerIn: Overlay.overlay
    width: 400
    title: qsTr("Rename population")
    Material.roundedScale: theme.radius

    property int gateId: 0

    function start(id) {
        gateId = id
        field.text = backend.populationName(id)
        open()
    }
    function accept() {
        if (backend.renameGate(gateId, field.text))
            close()
    }

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
    }

    TextField {
        id: field
        width: parent.width
        placeholderText: qsTr("Name, such as Lymphocytes or CD4+")
        Material.roundedScale: theme.radius
        selectByMouse: true
        Keys.onReturnPressed: root.accept()
        Keys.onEnterPressed: root.accept()
        Keys.onEscapePressed: root.close()
    }

    footer: DialogButtonBox {
        background: Item {}
        Button {
            text: qsTr("Cancel")
            flat: true
            onClicked: root.close()
        }
        Button {
            text: qsTr("Rename")
            highlighted: true
            enabled: field.text.trim() !== ""
            onClicked: root.accept()
        }
    }
    onOpened: {
        field.forceActiveFocus()
        field.selectAll()
    }
}
