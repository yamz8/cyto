import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The spillover matrix, one row per dye: how much of it each detector sees.
Dialog {
    id: root
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(parent ? parent.width - 60 : 900, Math.max(520, 150 + 96 * n + 48))
    height: Math.min(parent ? parent.height - 60 : 700, implicitHeight)
    title: qsTr("Compensation matrix")
    Material.roundedScale: theme.radius
    padding: 24

    readonly property int n: backend.compensationChannels.length

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

    function format(value) { return Number(value).toLocaleString(Qt.locale(), 'f', 4) }

    contentItem: ColumnLayout {
        spacing: 14

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: "#b4b4bc"
            font.pixelSize: 13
            text: root.n > 0
                  ? qsTr("Row: the dye's own channel. Column: where it spills. 0.1200 means 12 % of the dye shows up in that column's detector. Changes apply to every sample, and Ctrl + Z undoes them.")
                  : qsTr("None of the files carries a spillover matrix. Start from an identity matrix over the fluorescence channels and enter the spillover from your single-stain controls.")
        }

        RowLayout {
            Switch {
                text: qsTr("Apply compensation")
                enabled: root.n > 0
                checked: backend.compensate
                onToggled: backend.compensate = checked
            }
            Item { Layout.fillWidth: true }
            Button {
                visible: root.n === 0
                text: qsTr("Start a matrix")
                highlighted: true
                onClicked: backend.createCompensation()
            }
            Button {
                visible: backend.canResetCompensation
                text: qsTr("Use the file's matrix")
                flat: true
                onClicked: backend.resetCompensation()
            }
        }

        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: grid.implicitHeight
            Layout.minimumHeight: Math.min(grid.implicitHeight, 160)
            visible: root.n > 0
            clip: true
            contentWidth: grid.implicitWidth
            contentHeight: grid.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            ScrollBar.horizontal: ScrollBar {}

            GridLayout {
                id: grid
                columns: root.n + 1
                rowSpacing: 4
                columnSpacing: 4

                Item { Layout.preferredWidth: 140; Layout.preferredHeight: 24 }
                Repeater {
                    model: backend.compensationChannels
                    delegate: Label {
                        required property string modelData
                        Layout.preferredWidth: 92
                        text: modelData
                        elide: Text.ElideRight
                        horizontalAlignment: Text.AlignHCenter
                        color: theme.accent
                        font.pixelSize: 12
                        font.bold: true
                    }
                }

                Repeater {
                    model: root.n * (root.n + 1)
                    delegate: Loader {
                        id: cell
                        required property int index
                        readonly property int row: Math.floor(index / (root.n + 1))
                        readonly property int column: index % (root.n + 1) - 1
                        Layout.preferredWidth: column < 0 ? 140 : 92
                        sourceComponent: column < 0 ? rowName : valueField
                        Component {
                            id: rowName
                            Label {
                                text: backend.compensationChannels[cell.row]
                                elide: Text.ElideRight
                                color: theme.accent
                                font.pixelSize: 12
                                font.bold: true
                            }
                        }
                        Component {
                            id: valueField
                            TextField {
                                Material.roundedScale: theme.radius
                                horizontalAlignment: Text.AlignRight
                                font.pixelSize: 13
                                selectByMouse: true
                                text: root.format(backend.compensationMatrix[cell.row * root.n + cell.column])
                                color: cell.row === cell.column ? "#8a8a94" : "#e4e4ea"
                                validator: DoubleValidator { locale: Qt.locale().name }
                                onEditingFinished: {
                                    // Leaving a cell untouched keeps the full value; the
                                    // field only shows it rounded.
                                    const current = backend.compensationMatrix[cell.row * root.n + cell.column]
                                    const value = Number.fromLocaleString(Qt.locale(), text)
                                    if (text !== root.format(current) && !isNaN(value))
                                        backend.setCompensationValue(cell.row, cell.column, value)
                                    text = Qt.binding(() => root.format(backend.compensationMatrix[cell.row * root.n + cell.column]))
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    footer: DialogButtonBox {
        background: Item {}
        Button {
            text: qsTr("Done")
            highlighted: true
            onClicked: root.close()
        }
    }
}
