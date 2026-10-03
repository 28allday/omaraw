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
        for (const p of engine.params) if (p.op === "ashift" && p.field === "rotation") return p.value
        return 0
    }
    readonly property var ratios: [[qsTr("Free"), 0, 0], [qsTr("Original"), 1, 0], ["1:1", 1, 1], ["5:4", 5, 4], ["4:3", 4, 3], ["3:2", 3, 2], ["16:9", 16, 9], ["7:5", 7, 5]]
    function correct(operation, policy) {
        root.overlay.ruler = false
        root.overlay.guided = false
        engine.correctGeometry(operation, policy === undefined ? -1 : policy)
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
                objectName: "straightenControl"
                width: Math.min(215, controls.width)
                label: qsTr("Straighten"); labelWidth: 66
                from: -45; to: 45; origin: 0; decimals: 1; suffix: "°"
                tip: qsTr("Adjusts rotation. Constrain crop trims empty corners when enabled.")
                value: root.angle
                onEdited: v => rotationTimer.push(v)
                onEditingFinished: v => rotationTimer.flush(v)
            }
            IconButton {
                objectName: "straightenRuler"
                iconName: "ruler"; text: qsTr("Straighten along a line")
                tip: qsTr("Drag along an edge that should be horizontal or vertical.")
                checked: root.overlay.ruler
                onClicked: { root.overlay.guided = false; root.overlay.ruler = !root.overlay.ruler }
            }
            ComboField {
                width: 100
                readonly property var grids: [["thirds", qsTr("Thirds")], ["golden", qsTr("Golden")], ["diagonals", qsTr("Diagonals")], ["none", qsTr("No grid")]]
                model: grids.map(g => g[1])
                currentIndex: Math.max(0, grids.findIndex(g => g[0] === root.overlay.grid))
                tipTitle: qsTr("Composition guide"); tip: qsTr("Draws a guide inside the crop frame.")
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
                onClicked: { rotationTimer.drop(); engine.cropMode = false }
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
                text: qsTr("Guided"); checkable: true; checked: root.overlay.guided
                tip: qsTr("Draw two edges that should be vertical or two that should be horizontal. Add another pair to correct both directions.")
                onClicked: { root.overlay.ruler = false; root.overlay.guided = !root.overlay.guided; root.overlay.guides = [] }
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
            ToolButton { text: qsTr("Cancel guides"); enabled: !engine.busy; onClicked: { root.overlay.guided = false; root.overlay.guides = [] } }
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
            width: parent.width
            visible: text !== ""
            text: root.overlay.guided ? qsTr("Draw two vertical edges or two horizontal edges. Use four guides for both directions.") + (engine.geometryStatus ? "\n" + engine.geometryStatus : "")
                  : engine.geometryStatus
            textFormat: Text.PlainText; wrapMode: Text.WordWrap
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textSecondary
        }
    }
}
