import QtQuick
import OmaRaw.Ui

Column {
    id: root
    property string kind: "mask"
    property var develop: null
    property bool compact: false
    property bool showSetup: false
    readonly property bool selecting: engine.ai.mode === kind
    x: Theme.s3
    width: parent ? parent.width - Theme.s3 * 2 : 280
    spacing: Theme.s2
    readonly property var repair: (engine.paramsVersion, engine.paramsFor("omarawrepair")).find(p => p.field === "file")
    Row {
        visible: root.kind === "remove" && root.repair !== undefined && (root.repair.text || "") !== ""
        width: parent.width; spacing: Theme.s2
        CheckField {
            width: Math.max(80, parent.width - clearRepairs.implicitWidth - Theme.s2)
            text: qsTr("Object removals")
            checked: root.repair !== undefined && root.repair.enabled === true
            onClicked: { const on = root.repair.enabled !== true; checked = Qt.binding(() => root.repair !== undefined && root.repair.enabled === true); engine.setModuleEnabled("omarawrepair", on) }
        }
        ToolButton {
            id: clearRepairs
            text: qsTr("Reset removals"); iconName: "rotate-ccw"
            onClicked: engine.applyValues([{op:"omarawrepair",field:"file",text:"",enabled:false}])
        }
    }
    onVisibleChanged: if (!visible && selecting) engine.ai.cancel()
    ToolButton {
        objectName: root.kind === "mask" ? "startAiMask" : "startAiRemove"
        text: root.kind === "mask" ? qsTr("AI object mask") : qsTr("AI object removal")
        showLabel: true; iconName: "sparkles"; checked: root.selecting
        enabled: !engine.ai.busy && engine.imageId >= 0 && !engine.busy
        onClicked: {
            if (root.selecting) { engine.ai.cancel(); return }
            if (root.compact && !engine.ai.installed) { root.showSetup = !root.showSetup; return }
            for (const k of ["penMode", "brushMode", "spotMode", "wbPickMode"])
                if (root.develop && typeof root.develop[k] === "boolean") root.develop[k] = false
            if (root.develop) root.develop.pickChannel = -1
            engine.ai.start(root.kind)
        }
    }
    Column {
        width: parent.width; spacing: Theme.s2; visible: !engine.ai.installed && (!root.compact || root.showSetup)
        Text {
            width: parent.width; wrapMode: Text.WordWrap
            text: qsTr("AI tools are included with OmaRAW. Their files are missing or damaged; reinstall the OmaRAW package to restore them. Photos are processed on this computer.")
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
        }
    }
    Column {
        width: parent.width; spacing: Theme.s2; visible: root.selecting
        CheckField {
            width: parent.width; text: qsTr("Use GPU for selection"); checked: engine.ai.useGpu; enabled: !engine.ai.busy
            tip: qsTr("Optional Vulkan acceleration. CPU works on every supported machine and may be faster on integrated graphics. Removal currently uses CPU.")
            onClicked: engine.ai.useGpu = !engine.ai.useGpu
        }
        Text {
            width: parent.width; wrapMode: Text.WordWrap
            text: engine.ai.brushMode
                ? (root.kind === "remove"
                    ? qsTr("Paint over anything to remove, including shadows and reflections. Right-drag erases from the selection.")
                    : (engine.ai.objectBrush
                        ? qsTr("Paint inside the object, then release. AI finds its outline. Right-drag over unwanted areas to exclude them.")
                        : qsTr("Paint adds exactly the area you brush. Right-drag erases. Use AI brush to find an object's outline.")))
                : qsTr("Click to include. Right-click to exclude. Drag a box around an object. Add more points to refine the edge.")
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textSecondary
        }
        SegmentedControl {
            objectName: root.kind === "mask" ? "aiMaskSelectionMode" : "aiRemovalSelectionMode"
            enabled: !engine.ai.busy
            labels: root.kind === "mask" ? [qsTr("Select object"), qsTr("AI brush"), qsTr("Paint")]
                                          : [qsTr("Select object"), qsTr("Brush")]
            currentIndex: !engine.ai.brushMode ? 0 : root.kind === "mask" && !engine.ai.objectBrush ? 2 : 1
            onActivated: i => { engine.ai.objectBrush = i !== 2; engine.ai.brushMode = i !== 0 }
        }
        SliderField {
            objectName: root.kind === "mask" ? "aiMaskBrushSize" : "aiRemovalBrushSize"
            width: parent.width; stacked: true; visible: engine.ai.brushMode
            enabled: !engine.ai.busy; label: qsTr("Brush size")
            from: .2; to: 10; stepSize: .1; decimals: 1; suffix: " %"; value: engine.ai.brushSize * 100
            tip: qsTr("Brush radius as a percentage of the photo's long edge.")
            onEditingFinished: v => engine.ai.brushSize = v / 100
        }
        SliderField {
            width: parent.width; stacked: true; visible: root.kind === "remove"
            enabled: !engine.ai.busy; label: qsTr("Edge coverage")
            from: 0; to: 3; stepSize: .1; decimals: 1; suffix: " %"; value: engine.ai.removalMargin * 100
            tip: qsTr("Expands the removal area to cover fringes. The blue overlay includes this margin and scales with the full-size photo.")
            onEditingFinished: v => engine.ai.removalMargin = v / 100
        }
        Flow {
            width: parent.width; spacing: Theme.s1
            ToolButton {
                objectName: "refineAiEdges"
                text: qsTr("Refine edges"); visible: root.kind === "mask"
                enabled: !engine.ai.busy && engine.ai.hasSelection
                tip: qsTr("Adjust the selection outline to nearby colour edges. Undo selection restores the previous outline. Fine hair and transparency still need manual work.")
                onClicked: engine.ai.refineEdges()
            }
            ToolButton { text: qsTr("Undo selection"); enabled: !engine.ai.busy && engine.ai.canUndoSelection; onClicked: engine.ai.undoPoint() }
            ToolButton { text: qsTr("Invert"); enabled: !engine.ai.busy && engine.ai.hasSelection; onClicked: engine.ai.invert() }
            ToolButton { text: qsTr("Clear"); enabled: !engine.ai.busy; onClicked: engine.ai.clear() }
        }
        Text {
            width: parent.width; visible: root.kind === "remove"; wrapMode: Text.WordWrap
            text: qsTr("Include shadows and reflections with the brush. Preview, then Apply removal to keep editing. Repairs follow your colour and geometry adjustments. Apply, then select another object. Large or complex removals may need further retouching.")
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
        }
        Text {
            objectName: "aiSelectionStatus"
            width: parent.width; wrapMode: Text.WordWrap; textFormat: Text.PlainText
            text: engine.ai.status; visible: text !== ""
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textSecondary
        }
        Flow {
            width: parent.width; spacing: Theme.s1
            ToolButton {
                objectName: "acceptAiSelection"
                text: engine.ai.creatingMask ? qsTr("Creating editable mask…")
                      : root.kind === "mask" ? qsTr("Create editable mask") : qsTr("Preview removal")
                enabled: !engine.ai.busy && engine.ai.hasSelection
                onClicked: {
                    if (root.kind === "mask") {
                        if (root.develop) root.develop.maskVisualsShown = true
                        engine.ai.acceptMask()
                    } else engine.ai.remove()
                }
            }
            ToolButton { objectName: "applyAiRemoval"; text: qsTr("Apply removal"); visible: engine.ai.result !== ""; enabled: !engine.ai.busy; onClicked: engine.ai.apply() }
            ToolButton { objectName: "saveAiCopy"; text: qsTr("Save rendered DNG…"); visible: engine.ai.result !== ""; enabled: !engine.ai.busy; onClicked: saveDialog.open() }
        }
        CheckField { visible: engine.ai.result !== ""; text: qsTr("Show removal preview"); checked: engine.ai.showResult; onClicked: engine.ai.showResult = !engine.ai.showResult }
    }
    Text {
        width: parent.width; wrapMode: Text.WordWrap; textFormat: Text.PlainText
        text: engine.ai.status; visible: !root.selecting && text !== ""
        font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textSecondary
    }
    ToolButton { text: qsTr("Cancel"); visible: root.selecting || engine.ai.busy; enabled: !engine.ai.creatingMask; onClicked: engine.ai.cancel() }
    FilePicker {
        id: saveDialog
        title: qsTr("Save AI removal DNG"); fileMode: PathPicker.SaveFile; defaultSuffix: "dng"
        selectedFile: engine.imagePath.replace(/\.[^/.]+$/, "") + "-removed.dng"
        acceptLabel: qsTr("Save copy")
        nameFilters: [qsTr("DNG photographs (*.dng)")]
        onAccepted: engine.ai.saveCopy(selectedFile.toString())
    }
}
