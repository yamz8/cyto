import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Cyto

ApplicationWindow {
    id: win
    width: 1320
    height: 820
    minimumWidth: 920
    minimumHeight: 560
    visible: true
    title: (backend.dirty ? "• " : "") + backend.fileName + " — Cyto"
    color: "#0e0e10"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    property bool statsVisible: false
    property bool quitting: false
    // Runs once the workspace is saved, for a quit or an open that asked first.
    property var afterSave: null
    readonly property bool hasSamples: backend.samples.length > 0
    readonly property bool dialogOpen: unsaved.visible || rename.visible || compensation.visible || help.visible

    // Asks before unsaved gates would be lost, then does the thing.
    function guard(action) {
        if (backend.dirty && hasSamples) {
            unsaved.action = action
            unsaved.open()
        } else {
            action()
        }
    }
    function quit() { guard(() => { win.quitting = true; Qt.quit() }) }
    function renameSelected() {
        const id = backend.selectedGate !== 0 ? backend.selectedGate : backend.currentPopulation
        if (id !== 0)
            rename.start(id)
    }
    function setTool(tool) {
        statsVisible = false
        plot.tool = tool
    }

    onClosing: (close) => {
        if (!quitting && backend.dirty && hasSamples) {
            close.accepted = false
            quit()
        }
    }

    Connections {
        target: backend
        function onPickerCancelled() { win.afterSave = null }
        function onSaved() {
            const action = win.afterSave
            win.afterSave = null
            if (action)
                action()
        }
    }

    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; enabled: !unsaved.visible && !rename.visible && !compensation.visible; onActivated: help.visible ? help.close() : help.open() }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen; onActivated: win.quit() }
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen; onActivated: backend.stepSample(1) }
    Shortcut { sequence: "Shift+Space"; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen; onActivated: backend.stepSample(-1) }
    Shortcut { sequence: "Ctrl+O"; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen; onActivated: backend.addSamples() }
    Shortcut { sequence: "Ctrl+Shift+O"; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen; onActivated: win.guard(() => backend.openWorkspace()) }
    Shortcut { sequence: "Ctrl+S"; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen && win.hasSamples; onActivated: { win.afterSave = null; backend.saveWorkspace() } }
    Shortcut { sequence: "Ctrl+Shift+S"; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen && win.hasSamples; onActivated: { win.afterSave = null; backend.saveWorkspaceAs() } }
    Shortcut { sequence: "Ctrl+E"; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen; onActivated: backend.exportStats() }
    Shortcut { sequences: ["Ctrl+Z"]; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen; onActivated: backend.undo() }
    Shortcut { sequences: ["Ctrl+Shift+Z", "Ctrl+Y"]; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen; onActivated: backend.redo() }
    Shortcut { sequence: "V"; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen; onActivated: win.setTool(0) }
    Shortcut { sequence: "R"; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen && win.hasSamples; onActivated: win.setTool(1) }
    Shortcut { sequence: "P"; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen && win.hasSamples; onActivated: win.setTool(2) }
    Shortcut { sequence: "E"; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen && win.hasSamples; onActivated: win.setTool(3) }
    Shortcut { sequence: "H"; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen && win.hasSamples; onActivated: backend.histogram = !backend.histogram }
    Shortcut { sequence: "X"; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen; onActivated: backend.cycleXChannel(1) }
    Shortcut { sequence: "Shift+X"; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen; onActivated: backend.cycleXChannel(-1) }
    Shortcut { sequence: "Y"; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen && !backend.histogram; onActivated: backend.cycleYChannel(1) }
    Shortcut { sequence: "Shift+Y"; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen && !backend.histogram; onActivated: backend.cycleYChannel(-1) }
    Shortcut { sequence: "S"; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen; onActivated: backend.swapAxes() }
    Shortcut { sequence: "L"; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen; onActivated: backend.cycleXScale() }
    Shortcut { sequence: "Shift+L"; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen && !backend.histogram; onActivated: backend.cycleYScale() }
    Shortcut { sequence: "C"; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen && backend.compensationChannels.length > 0; onActivated: backend.compensate = !backend.compensate }
    Shortcut { sequence: "Shift+C"; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen && win.hasSamples; onActivated: compensation.open() }
    Shortcut { sequence: "T"; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen; onActivated: win.statsVisible = !win.statsVisible }
    Shortcut { sequence: "M"; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen && win.statsVisible; onActivated: backend.stats.cycleMeasure() }
    Shortcut { sequence: "F2"; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen; onActivated: win.renameSelected() }
    Shortcut { sequences: ["Delete", "Backspace"]; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen && backend.selectedGate !== 0; onActivated: backend.deleteGate(backend.selectedGate) }
    Shortcut { sequence: "Shift+Delete"; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen && win.hasSamples; onActivated: backend.removeSample(backend.currentSample) }
    Shortcut { sequence: "Up"; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen && !win.statsVisible; onActivated: backend.stepPopulation(-1) }
    Shortcut { sequence: "Down"; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen && !win.statsVisible; onActivated: backend.stepPopulation(1) }
    Shortcut { sequence: "Left"; context: Qt.ApplicationShortcut; enabled: !win.dialogOpen && !win.statsVisible; onActivated: backend.openParent() }
    Shortcut {
        sequences: ["Right", "Return", "Enter"]
        context: Qt.ApplicationShortcut
        enabled: !win.dialogOpen && !win.statsVisible
        onActivated: plot.drawing ? plot.finishPolygon() : backend.openGate(backend.selectedGate)
    }
    Shortcut {
        sequence: "Esc"
        context: Qt.ApplicationShortcut
        enabled: !win.dialogOpen
        onActivated: {
            if (plot.drawing) plot.cancelDrawing()
            else if (plot.tool !== 0) plot.tool = 0
            else if (win.statsVisible) win.statsVisible = false
            else if (backend.selectedGate !== 0) backend.selectedGate = 0
            else if (backend.problem !== "") backend.dismissProblem()
        }
    }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        DropArea {
            id: drop
            anchors.fill: parent
            onDropped: (event) => {
                if (!event.hasUrls)
                    return
                const urls = event.urls
                const workspace = urls.some(url => url.toString().toLowerCase().endsWith(".cyto"))
                if (workspace)
                    win.guard(() => backend.openUrls(urls))
                else
                    backend.openUrls(urls)
            }
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            Toolbar {
                Layout.fillWidth: true
                statsVisible: win.statsVisible
                onToggleStats: win.statsVisible = !win.statsVisible
                onEditCompensation: compensation.open()
                onShowHelp: help.open()
            }

            // What went wrong, said plainly, until it's dismissed or fixed.
            Rectangle {
                Layout.fillWidth: true
                visible: backend.problem !== ""
                implicitHeight: problemText.implicitHeight + 20
                color: "#2a1517"
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 8
                    Label {
                        id: problemText
                        Layout.fillWidth: true
                        text: backend.problem
                        wrapMode: Text.Wrap
                        color: "#f2b8b5"
                        font.pixelSize: 13
                    }
                    ToolButton {
                        text: "✕"
                        onClicked: backend.dismissProblem()
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Dismiss")
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                Sidebar {
                    Layout.preferredWidth: 290
                    Layout.fillHeight: true
                    visible: win.hasSamples
                    onRenameRequested: (id) => rename.start(id)
                }

                ToolStrip {
                    Layout.fillHeight: true
                    visible: win.hasSamples
                    plot: plot
                    statsVisible: win.statsVisible
                    onPickTool: (tool) => win.setTool(tool)
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    Plot {
                        id: plot
                        anchors.fill: parent
                        anchors.margins: 12
                        visible: win.hasSamples && !win.statsVisible
                        model: backend
                        accent: theme.accent
                    }

                    StatsTable {
                        objectName: "stats"
                        anchors.fill: parent
                        visible: win.hasSamples && win.statsVisible
                    }

                    EmptyState {
                        objectName: "empty"
                        anchors.centerIn: parent
                        visible: !win.hasSamples
                        onAddFiles: backend.addSamples()
                        onOpenWorkspace: backend.openWorkspace()
                    }

                    Rectangle {
                        anchors.fill: parent
                        visible: drop.containsDrag
                        color: "#20ffffff"
                        border.color: theme.accent
                        border.width: 2
                        radius: theme.radius
                        Label {
                            anchors.centerIn: parent
                            text: qsTr("Drop FCS files to add them")
                            font.pixelSize: 18
                        }
                    }
                }
            }

            StatusLine {
                Layout.fillWidth: true
                pointer: plot.visible ? plot.pointer : ""
                drawing: plot.drawing
                tool: plot.tool
            }
        }
    }

    UnsavedDialog {
        id: unsaved
        objectName: "unsaved"
        property var action: null
        onSaveChosen: {
            win.afterSave = action
            backend.saveWorkspace()
        }
        onDiscardChosen: if (action) action()
    }

    RenameDialog { id: rename; objectName: "rename" }
    CompensationDialog { id: compensation; objectName: "compensation" }
    ShortcutOverlay { id: help; objectName: "help" }
}
