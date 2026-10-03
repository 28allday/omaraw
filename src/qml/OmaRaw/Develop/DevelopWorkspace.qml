import QtQuick
import QtQuick.Controls.Basic as C
import OmaRaw.Ui
import OmaRaw.Library

// Develop. Left dock: presets, history, snapshots. Centre: engine
// viewer. Right: adjustments, with the mask shapes on the Local tab.
Item {
    id: root
    objectName: "developWorkspace"
    property var shell: null
    property alias snapshotPanel: snapshots
    // Before/after: the untouched photo in the viewer's compare mode.
    property bool comparingOriginal: false
    // Brush mask painting: the Local panel sets these, the viewer paints.
    property bool brushMode: false
    property bool penMode: false
    property bool penNewLocal: true
    property real penFeather: 0.02
    property alias penEditor: viewer.penEditor
    property alias maskVisualsShown: viewer.maskVisualsShown
    function toggleMaskCoverage() { viewer.toggleMaskCoverage() }
    function peekMaskCoverage(on) { viewer.peekMaskCoverage(on) }
    onMaskVisualsShownChanged: if (!maskVisualsShown) { penMode = false; brushMode = false; pickChannel = -1 }
    function startPen(newLocal) { maskVisualsShown = true; brushMode = false; pickChannel = -1; penEditor.clear(); penNewLocal = newLocal; penMode = true; penEditor.forceActiveFocus() }
    onBrushModeChanged: if (brushMode) { maskVisualsShown = true; penMode = false; pickChannel = -1 }
    onPickChannelChanged: if (pickChannel >= 0) { maskVisualsShown = true; penMode = false; brushMode = false }
    property real brushSize: 0.05
    property real brushHardness: 0.6
    property real brushFlow: 1.0
    // Range picking: the channel (0 luminance, 1 hue, 2 colour) the next
    // click on the picture seeds, -1 when not picking.
    property int pickChannel: -1
    property bool colourRangeNew: false
    function startColourRange(newMask) {
        if (pickChannel === 3 && colourRangeNew === newMask) { pickChannel = -1; return }
        if (engine.ai.mode !== "") engine.ai.cancel()
        colourRangeNew = newMask; wbPickMode = false; pickChannel = 3
        viewer.focusRangePicker()
    }
    // White balance picker: the next click on the picture is a neutral.
    property bool wbPickMode: false
    // Retouch: the tool and size the next placed spot gets, and whether a
    // click on the picture places one.
    property bool spotMode: false
    property int spotAlgorithm: 2
    property int spotShape: 1
    property real spotSize: 0.03
    property real spotFeather: 0.01
    // Crop & straighten: the viewer shows the whole frame with the crop
    // rectangle over it while this is on.
    readonly property bool cropMode: engine.cropMode
    // Lights out: the picture alone on black, docks and filmstrip gone.
    readonly property bool lightsOut: shell ? shell.lightsOut === true : false
    onLightsOutChanged: if (lightsOut) engine.scopeExpanded = false
    readonly property color canvas: Theme.colourCritical ? Theme.pasteboard : lightsOut ? "#000000"
        : shell && shell.viewerBackground === "black" ? "#000000"
        : shell && shell.viewerBackground === "grey" ? "#6E6E6E"
        : shell && shell.viewerBackground === "light" ? "#D4D4D4" : Theme.windowBg
    function openTool(tool) { engine.cropMode = false; right.group = tool }
    function toggleCrop() { if (engine.imageId >= 0) engine.cropMode = !engine.cropMode }
    // Escape: back from Masks or Retouch to the sections. False when neither was open.
    function leaveTool() { if (pickChannel >= 0) { pickChannel = -1; return true }; if (engine.ai.mode !== "") { engine.ai.cancel(); return true }; if (penMode) { penEditor.cancel(); return true }; return right.leaveTool() }
    function saveModulePreset(operation, label) { presets.openModulePreset(operation, label) }
    function compareOriginal() {
        if (engine.imageId < 0) return
        viewer.compareSnapshotId = 0
        engine.clearSnapshotComparison()
        comparingOriginal = true
        snapshots.comparing = 0
        viewer.compareLabel = backend.aiVersions.some(v => v.current && v.operation !== "original") ? qsTr("Before edits on this version") : qsTr("Original")
        viewer.compareSource = engine.originalSource
        engine.renderOriginal()
    }
    function toggleOriginal() {
        if (comparingOriginal) { comparingOriginal = false; viewer.compareSource = "" } else compareOriginal()
    }
    function setOriginal(on) { if (on !== comparingOriginal) toggleOriginal() }
    FilePicker {
        id: iccDialog
        objectName: "iccDialog"
        title: qsTr("Output profile for soft proofing")
        nameFilters: [qsTr("ICC profiles (*.icc *.icm *.ICC *.ICM)")]
        onAccepted: engine.proofProfile = selectedFile.toString()
    }
    Connections {
        target: engine
        function onOriginalChanged() { if (root.comparingOriginal) viewer.compareSource = engine.originalSource }
        function onImageChanged() { root.pickChannel = -1; root.comparingOriginal = false; viewer.compareSource = ""; viewer.compareSnapshotId = 0 }
    }

    // Follow the Library selection (photo or variant) into the engine.
    function loadCurrent() {
        if (backend.currentId > 0 && visible) {
            const info = backend.info(backend.currentId)
            engine.load(info.path, info.variant)
            // Already open and rendered (Output had it): nothing will turn
            // busy, so the first edit is asked for here, before any of yours.
            Qt.callLater(() => { if (root.visible && !engine.busy) backend.giveFirstEdit() })
        }
    }
    onVisibleChanged: {
        loadCurrent()
        if (!visible) engine.scopeExpanded = false
        if (!visible && viewer.compareSnapshotId > 0) {
            snapshots.comparing = 0; viewer.compareSource = ""; viewer.compareSnapshotId = 0
            engine.clearSnapshotComparison()
        }
    }
    Component.onCompleted: loadCurrent()
    Connections {
        target: backend
        function onSelectionChanged() { root.loadCurrent() }
    }
    Connections {
        target: engine
        function onAvailableChanged() { root.loadCurrent() }
        // A newly imported photo gets its automatic first edit the first
        // time it has rendered here (Backend::giveFirstEdit; once per photo).
        function onBusyChanged() { if (!engine.busy && root.visible) backend.giveFirstEdit() }
    }

    Column {
        anchors.fill: parent
        spacing: 0
        AiVersionsBar { id: versions; width: parent.width; visible: !root.lightsOut && backend.aiVersions.length > 1 }
        Rectangle {
            id: smartNotice
            objectName: "smartPreviewNotice"
            width: parent.width
            height: visible ? smartNoticeText.implicitHeight + Theme.s2 * 2 : 0
            visible: engine.sourceKind === "smart-preview"
            color: Theme.panelRaised
            Text {
                id: smartNoticeText
                x: Theme.s3; y: Theme.s2; width: parent.width - Theme.s3 * 2
                text: qsTr("Editing Smart Preview · Reconnect the original for full detail and export. Your edits are saved in this catalog.")
                wrapMode: Text.WordWrap
                font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textSecondary
            }
        }
        Item {
            width: parent.width
            height: parent.height - (versions.visible ? versions.height : 0) - smartNotice.height - (strip.visible ? strip.height : 0)
            Rectangle {
                id: left
                visible: !root.lightsOut
                anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                width: root.shell.sourceDockWidth
                color: Theme.panelBg
                Rectangle { anchors.right: parent.right; width: Theme.hairline; height: parent.height; color: Theme.border }
                SourceDock {
                    id: sourceDock
                    anchors.fill: parent
                    anchors.rightMargin: Theme.hairline
                    SourceDockPanel {
                        id: presetPane
                        dock: sourceDock; panelKey: "presets"; title: qsTr("Presets")
                        defaultListHeight: Math.max(Theme.hRow * 2, Math.min(Theme.hRow * 8, sourceDock.height * .35))
                        listHeight: presets.listHeight
                        PresetPanel { id: presets; width: parent.width; dockPanel: presetPane; maximumListHeight: presetPane.maximumListHeight }
                    }
                    SourceDockPanel {
                        id: historyPane
                        dock: sourceDock; panelKey: "history"; title: qsTr("History")
                        defaultListHeight: Theme.hRow * 10
                        listHeight: history.listHeight
                        HistoryPanel { id: history; width: parent.width; dockPanel: historyPane; maximumListHeight: historyPane.maximumListHeight }
                    }
                    SourceDockPanel {
                        id: snapshotPane
                        dock: sourceDock; panelKey: "snapshots"; title: qsTr("Snapshots")
                        defaultListHeight: 4 * (Theme.hRow * 2 + Theme.s1)
                        listHeight: snapshots.listHeight
                        SnapshotPanel {
                            id: snapshots
                            width: parent.width; dockPanel: snapshotPane; maximumListHeight: snapshotPane.maximumListHeight
                            onCompareRequested: (id, name, preview) => {
                                root.comparingOriginal = false
                                viewer.compareSource = preview; viewer.compareLabel = name; viewer.compareSnapshotId = id
                                backend.compareSnapshot(id)
                            }
                            onCompareCleared: {
                                if (!root.comparingOriginal) viewer.compareSource = ""
                                viewer.compareSnapshotId = 0; engine.clearSnapshotComparison()
                            }
                            onCompareOriginalRequested: root.compareOriginal()
                        }
                    }
                    SourceDockPanel {
                        id: proofPane
                        dock: sourceDock; panelKey: "proof"; title: qsTr("Soft Proof")
                        resizable: false
                        Column {
                            width: parent.width
                            DockHeader {
                                dockPanel: proofPane
                                width: parent.width
                                tabs: [qsTr("Soft Proof")]
                                helpSection: "print"
                                trailing: [
                                    Toggle { anchors.verticalCenter: parent.verticalCenter; checked: engine.proofOn; text: qsTr("Soft proof"); tip: qsTr("Shows the picture as the chosen printer or paper profile would render it."); enabled: engine.proofProfile !== ""; onClicked: engine.proofOn = !engine.proofOn }
                                ]
                            }
                            Column {
                                width: parent.width
                                spacing: Theme.s1
                                Row {
                                    x: Theme.s3; spacing: Theme.s2
                                    topPadding: Theme.s1
                                    ToolButton { iconName: "palette"; text: engine.proofProfile === "" ? qsTr("Choose profile…") : engine.proofProfileName; showLabel: true; tip: qsTr("Picks the ICC profile of the printer and paper to proof against."); onClicked: iccDialog.open() }
                                    IconButton { visible: engine.proofProfile !== ""; iconName: "x"; text: qsTr("Forget the profile"); tip: qsTr("Drops the proofing profile and switches proofing off."); onClicked: { engine.proofOn = false; engine.proofProfile = "" } }
                                }
                                Item {
                                    visible: engine.proofProfile !== ""
                                    width: parent.width; height: Theme.hRow + Theme.s1
                                    Text {
                                        anchors.left: parent.left; anchors.leftMargin: Theme.s3; width: 60
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: qsTr("Intent")
                                        font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
                                    }
                                    SegmentedControl {
                                        anchors.left: parent.left; anchors.leftMargin: Theme.s3 + 60
                                        anchors.verticalCenter: parent.verticalCenter
                                        labels: [qsTr("Perceptual"), qsTr("Relative")]
                                        tips: [qsTr("Squeezes every colour in so relationships are kept; the usual choice for photos."), qsTr("Keeps colours that fit and clips the rest; exact where it can be.")]
                                        currentIndex: engine.proofIntent === 0 ? 0 : 1
                                        onActivated: i => engine.proofIntent = i === 0 ? 0 : 1
                                    }
                                }
                                Column {
                                    visible: engine.proofProfile !== ""
                                    x: Theme.s3; width: parent.width - Theme.s3 * 2
                                    Toggle { label: qsTr("Gamut warning (magenta)"); tip: qsTr("Paints magenta over colours the profile cannot reproduce."); checked: engine.proofGamutWarning; onClicked: engine.proofGamutWarning = !engine.proofGamutWarning }
                                }
                            }
                        }
                    }
                }
            }
            DevelopViewer {
                id: viewer
                localToolsActive: right.group === "Local" && !root.lightsOut
                objectName: "developViewer"
                anchors.left: left.visible ? left.right : parent.left; anchors.right: right.visible ? right.left : parent.right
                anchors.top: parent.top; anchors.bottom: parent.bottom
                canvas: root.canvas
                brushMode: root.brushMode
                penMode: root.penMode
                penNewLocal: root.penNewLocal
                penFeather: root.penFeather
                onPenFinished: root.penMode = false
                brushSize: root.brushSize
                brushHardness: root.brushHardness
                brushFlow: root.brushFlow
                pickChannel: root.pickChannel
                colourRangeNew: root.colourRangeNew
                onPicked: root.pickChannel = -1
                wbPickMode: root.wbPickMode
                onWbPicked: root.wbPickMode = false
                spotMode: root.spotMode
                spotAlgorithm: root.spotAlgorithm
                spotShape: root.spotShape
                spotSize: root.spotSize
                spotFeather: root.spotFeather
                retouchShown: right.group === "Retouch"
                onCompareClosed: {
                    snapshots.comparing = 0; root.comparingOriginal = false; compareSource = ""; compareSnapshotId = 0
                    engine.clearSnapshotComparison()
                }
            }
            AdjustmentPanel {
                id: right
                objectName: "adjustmentPanel"
                visible: !root.lightsOut
                anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
                width: root.shell.inspectorWidth
                develop: root
            }
            ScopeOverlay {
                parent: viewer
                visible: root.visible && !root.lightsOut && engine.scopeExpanded
            }
        }
        Filmstrip {
            id: strip
            shell: root.shell
            visible: root.shell.filmstripVisible && !root.lightsOut
            width: parent.width
            height: root.shell.filmstripHeight
        }
    }
}
