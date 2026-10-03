pragma ComponentBehavior: Bound
import QtQuick
import OmaRaw.Ui

Rectangle {
    id: root
    objectName: "cropGeometryToolbar"
    property bool advanced: false
    required property var overlay
    implicitHeight: controls.implicitHeight + Theme.s2 * 2
    radius: Theme.rControl
    color: Theme.scrim
    readonly property real angle: {
        for (const p of engine.params) if (p.op === "ashift" && p.field === "rotation") return p.enabled === false ? 0 : p.value
        return 0
    }
    readonly property var ratios: [[qsTr("Free"), 0, 0], [qsTr("Original"), 1, 0], ["1:1", 1, 1], ["5:4", 5, 4], ["4:3", 4, 3], ["3:2", 3, 2], ["16:9", 16, 9], ["7:5", 7, 5]]
    function correct(operation, policy) {
        root.overlay.stopDrawing()
        engine.correctGeometry(operation, policy === undefined ? -1 : policy)
    }
    Connections {
        target: engine
        function onGeometryCompleted(operation, ok) {
            if (!engine.cropMode) return
            if (ok) root.overlay.stopDrawing()
            else if (operation === 1) root.overlay.drawLevelLine()
            else if (operation >= 2 && operation <= 4) root.overlay.drawPerspectiveGuides()
        }
    }
    EditCoalescer {
        id: rotationTimer
        onSend: v => engine.setParam("ashift", "rotation", v)
    }
    Column {
        id: controls
        x: Theme.s2; y: Theme.s2
        width: parent.width - Theme.s2 * 2
        spacing: Theme.s1
        Flow {
            width: parent.width; spacing: Theme.s1
            enabled: !engine.geometryBusy
            ComboField {
                width: 104
                objectName: "cropAspectControl"
                model: root.ratios.map(r => r[0])
                currentIndex: Math.max(0, root.ratios.findIndex(r => r[1] === (engine.crop.ratioN || 0) && r[2] === Math.abs(engine.crop.ratioD || 0)))
                tipTitle: qsTr("Aspect ratio"); tip: qsTr("Locks the crop frame to a shape. Free allows any aspect ratio.")
                onActivated: i => root.overlay.setRatio(root.ratios[i][1], root.ratios[i][2])
            }
            IconButton {
                objectName: "cropAspectSwap"
                iconName: "rotate-cw"; text: qsTr("Swap landscape and portrait")
                enabled: (engine.crop.ratioN || 0) > 0 && (engine.crop.ratioD || 0) !== 0
                onClicked: root.overlay.flipRatio()
            }
            SliderField {
                id: rotationControl
                objectName: "straightenControl"
                width: Math.min(320, controls.width)
                label: qsTr("Straighten"); labelWidth: 66
                from: -10; to: 10; origin: 0; decimals: 2; suffix: "°"
                fieldFrom: -45; fieldTo: 45; fieldStep: 0.05
                stepSize: 0.05; fineStep: 0.01; resetOnDoubleClick: true
                tip: qsTr("Drag for small rotation corrections; hold Shift for finer control. Click the angle to type any value from −45° to 45°. Double-click the track to reset.")
                value: root.angle
                onEdited: v => { root.overlay.stopDrawing(); rotationTimer.push(v) }
                onEditingFinished: v => rotationTimer.flush(v)
            }
            ToolButton {
                objectName: "straightenRuler"
                iconName: "ruler"; text: qsTr("Draw level line"); showLabel: true
                tip: qsTr("Click here, then drag across the photo along a horizon or upright edge. Release to straighten.")
                checked: root.overlay.ruler
                onClicked: root.overlay.ruler ? root.overlay.stopDrawing() : root.overlay.drawLevelLine()
            }
            ComboField {
                width: 100
                readonly property var grids: [["thirds", qsTr("Thirds")], ["golden", qsTr("Golden")], ["diagonals", qsTr("Diagonals")], ["none", qsTr("No grid")]]
                model: grids.map(g => g[1])
                currentIndex: Math.max(0, grids.findIndex(g => g[0] === root.overlay.grid))
                tipTitle: qsTr("Composition grid"); tip: qsTr("Shows a composition grid inside the crop frame.")
                onActivated: i => root.overlay.grid = grids[i][0]
            }
            IconButton {
                objectName: "resetCropFrame"
                iconName: "rotate-ccw"; text: qsTr("Reset crop frame")
                tip: qsTr("Restores the whole frame and Free aspect ratio. Rotation and perspective have their own reset.")
                onClicked: root.overlay.reset()
            }
            ToolButton {
                objectName: "cropDone"
                iconName: "check"; text: qsTr("Done"); showLabel: true
                tip: qsTr("Keeps the changes and leaves Crop & straighten.")
                onClicked: {
                    if (rotationTimer.editing) rotationTimer.flush(rotationControl.shownValue)
                    engine.cropMode = false
                }
            }
        }
        Flow {
            width: parent.width; spacing: Theme.s1
            enabled: engine.crop.geometryAvailable === true && !engine.busy
            ToolButton { objectName: "autoLevelButton"; text: qsTr("Auto level"); tip: qsTr("Detects clear horizontal and vertical edges and straightens the photo."); onClicked: root.correct(1) }
            ComboField {
                width: 165
                objectName: "autoPerspectiveControl"
                model: [qsTr("Auto perspective…"), qsTr("Vertical"), qsTr("Horizontal"), qsTr("Full")]
                currentIndex: 0
                tipTitle: qsTr("Automatic perspective"); tip: qsTr("Fits perspective to detected lines. Full needs clear lines in both directions.")
                onActivated: i => { if (i > 0) root.correct(i + 1); currentIndex = 0 }
            }
            ToolButton {
                objectName: "guidedPerspectiveButton"
                text: qsTr("Draw perspective guides"); checked: root.overlay.guided
                tip: qsTr("Draw two edges that should be vertical or two that should be horizontal. Add another pair to correct both directions.")
                onClicked: root.overlay.guided ? root.overlay.stopDrawing() : root.overlay.drawPerspectiveGuides()
            }
            ComboField {
                objectName: "constrainCropControl"
                width: 165
                model: [qsTr("Crop edges: Off"), qsTr("Crop edges: Largest"), qsTr("Crop edges: Original")]
                currentIndex: engine.crop.constrain || 0
                tipTitle: qsTr("Constrain crop"); tip: qsTr("Trims empty corners after rotation and perspective. Original keeps the original aspect ratio.")
                onActivated: i => root.correct(0, i)
            }
            ToolButton { objectName: "resetGeometry"; text: qsTr("Reset geometry"); tip: qsTr("Resets rotation and perspective. Keeps the crop frame and other adjustments."); onClicked: root.correct(6) }
        }
        Flow {
            width: parent.width; spacing: Theme.s1
            visible: root.overlay.guided
            ToolButton {
                objectName: "applyGuidesButton"
                text: qsTr("Apply guides (%1)").arg(root.overlay.guides.length)
                enabled: root.overlay.guides.length >= 2 && !engine.busy
                onClicked: engine.correctGeometry(5, -1, root.overlay.guides)
            }
            ToolButton { text: qsTr("Clear guides"); enabled: !engine.busy; onClicked: root.overlay.guides = [] }
            ToolButton { text: qsTr("Cancel guides"); enabled: !engine.busy; onClicked: root.overlay.stopDrawing() }
        }
        ToolButton {
            objectName: "advancedCropControls"
            text: qsTr("Precise crop edges"); showLabel: true
            iconName: root.advanced ? "chevron-up" : "chevron-down"
            onClicked: root.advanced = !root.advanced
        }
        Grid {
            id: preciseCrop
            width: parent.width; visible: root.advanced
            columns: width >= 560 ? 4 : 2; spacing: Theme.s1
            readonly property var rows: (engine.paramsVersion, engine.paramsFor("crop"))
            StableList { id: cropRows; source: preciseCrop.rows; key: p => p.field }
            Repeater {
                model: cropRows.model
                AdjustmentRow {
                    required property int index
                    required property var modelData
                    width: (preciseCrop.width - preciseCrop.spacing * (preciseCrop.columns - 1)) / preciseCrop.columns
                    param: preciseCrop.rows[index] || modelData
                }
            }
        }
        Text {
            objectName: "cropDrawingHint"
            width: parent.width
            visible: text !== ""
            readonly property string drawingHint: root.overlay.ruler
                ? qsTr("Drag along a horizon or an upright edge in the photo. Release to straighten. Click Draw level line again to cancel.")
                : root.overlay.guided ? qsTr("Drag along two vertical edges or two horizontal edges in the photo, then click Apply guides. Use four guides for both directions.") : ""
            text: [engine.geometryStatus, drawingHint].filter(s => s !== "").join("\n")
            textFormat: Text.PlainText; wrapMode: Text.WordWrap
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textSecondary
        }
    }
}
