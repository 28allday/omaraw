pragma ComponentBehavior: Bound
import QtQuick
import OmaRaw.Ui

// Retouch: heal, clone, blur and fill spots. Pick a tool and a size,
// press Place, then click the picture; heal and clone take their pixels
// from a source circle that starts beside the spot and can be dragged.
// Each spot stays editable: click it here or on the picture.
Column {
    id: root
    width: parent ? parent.width : 300
    spacing: Theme.s1
    // The Develop workspace, for placing spots on the viewer. Null in isolation.
    property var develop: null
    property var openAiRemoval: null
    readonly property var spots: engine.spots
    StableList { id: spotList; source: root.spots }
    readonly property var toolNames: ["", qsTr("Clone"), qsTr("Heal"), qsTr("Blur"), qsTr("Fill")]
    readonly property var toolIcons: ["", "stamp", "bandage", "droplet", "paint-bucket"]
    readonly property bool placing: root.develop ? root.develop.spotMode : false
    function parameter(field) { const p = (engine.paramsVersion, engine.paramsFor("retouch")).find(p => p.field === field); return p ? p.value : 0 }
    readonly property int layers: parameter("num_scales")
    readonly property int selectedLayer: parameter("curr_scale")
    onSelectedLayerChanged: if(engine.retouchPreviewScale >= 0) engine.retouchPreviewScale = selectedLayer
    onVisibleChanged: if(!visible) engine.retouchPreviewScale = -1
    readonly property var layerLabels: [qsTr("Whole image"), ...Array.from({length: layers}, (_, i) => qsTr("Detail layer %1").arg(i + 1)), qsTr("Residual tone")]

    Item { width: 1; height: Theme.s1 }
    AiPanel { visible: root.openAiRemoval === null; kind: "remove"; develop: root.develop }
    ToolButton {
        x: Theme.s3; visible: root.openAiRemoval !== null
        text: qsTr("AI object removal"); showLabel: true; iconName: "sparkles"
        tip: qsTr("Open object removal in Prepare at the start of the editing workflow.")
        onClicked: root.openAiRemoval()
    }
    BlockHeading {
        text: qsTr("Retouch"); operation: "retouch"; resetName: "resetTool_retouch"
        on: (engine.paramsVersion, engine.moduleEnabled(operation))
        saveEnabled: root.develop !== null
        onEyeClicked: engine.setModuleEnabled(operation, !on)
        onSavePresetRequested: (operation,label) => root.develop.saveModulePreset(operation,label)
    }
    // Rows as elsewhere in Develop: a caption over each choice, sliders with
    // their name and figure over a full-width track.
    component Caption: Text {
        x: Theme.s3; width: (parent ? parent.width : 300) - Theme.s3 * 2; height: Theme.hRow
        verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
        font.family: Theme.fontFamily; font.pixelSize: Theme.fsControl; color: Theme.textSecondary
    }
    Caption { text: qsTr("Shape") }
    ComboField {
        x: Theme.s3; width: parent.width - Theme.s3 * 2
        model: [qsTr("Circle"), qsTr("Brush stroke"), qsTr("Polygon path")]
        currentIndex: root.develop ? [1,3,4].indexOf(root.develop.spotShape) : 0
        tipTitle: qsTr("Retouch shape"); tip: qsTr("Brush: drag over the area. Polygon: click corners, then double-click to finish.")
        onActivated: i => { if(root.develop) root.develop.spotShape = [1,3,4][i] }
    }
    SliderField {
        x: Theme.s3; width: parent.width - Theme.s3 * 2; stacked: true
        label: qsTr("Frequency layers"); from: 0; to: 15; stepSize: 1; decimals: 0; value: root.layers
        tip: qsTr("Split tone and texture for independent retouching. Zero edits the whole image.")
        onEditingFinished: v => { engine.setParam("retouch", "num_scales", Math.round(v)); if(root.selectedLayer > v + 1) engine.setParam("retouch", "curr_scale", 0) }
    }
    Caption { visible: root.layers > 0; text: qsTr("Layer for new shapes") }
    ComboField {
        x: Theme.s3; width: parent.width - Theme.s3 * 2; visible: root.layers > 0
        model: root.layerLabels; currentIndex: Math.min(root.selectedLayer, root.layers + 1)
        tipTitle: qsTr("Layer for new shapes"); tip: qsTr("Lower detail layers hold finer texture. Residual tone holds the broad colour and lighting.")
        onActivated: i => engine.setParam("retouch", "curr_scale", i)
    }
    ToolButton {
        x: Theme.s3; visible: root.layers > 0
        iconName: "layers"; text: qsTr("Preview selected layer"); showLabel: true
        checked: engine.retouchPreviewScale >= 0
        tip: qsTr("Shows the selected native frequency layer at preview resolution. Fine layers may need a larger preview to be visible. Exports keep the complete image.")
        onClicked: engine.retouchPreviewScale = engine.retouchPreviewScale >= 0 ? -1 : root.selectedLayer
    }
    Caption { text: qsTr("Tool") }
    Item {
        width: parent.width; height: Theme.hControl + Theme.s1
        SegmentedControl {
            anchors.left: parent.left; anchors.leftMargin: Theme.s3
            anchors.verticalCenter: parent.verticalCenter
            labels: [qsTr("Heal"), qsTr("Clone"), qsTr("Blur"), qsTr("Fill")]
            tips: [qsTr("Copies texture from the source and blends its colour into the surroundings."), qsTr("Copies the source exactly."), qsTr("Softens what is under the spot."), qsTr("Paints the spot with a flat tone.")]
            currentIndex: root.develop ? [2, 1, 3, 4].indexOf(root.develop.spotAlgorithm) : 0
            onActivated: i => { if (root.develop) root.develop.spotAlgorithm = [2, 1, 3, 4][i] }
        }
    }
    SliderField {
        x: Theme.s3; width: parent.width - Theme.s3 * 2; stacked: true
        label: qsTr("Size"); from: 0.5; to: 30; stepSize: 0.5; decimals: 1; suffix: " %"
        tip: qsTr("Radius of the next spot you place, as a percentage of the picture.")
        value: root.develop ? root.develop.spotSize * 100 : 3
        onEdited: v => { if (root.develop) root.develop.spotSize = v / 100 }
    }
    SliderField {
        x: Theme.s3; width: parent.width - Theme.s3 * 2; stacked: true
        label: qsTr("Feather"); from: 0; to: 20; stepSize: 0.5; decimals: 1; suffix: " %"
        tip: qsTr("How softly the next spot's edge blends in, as a percentage of the picture.")
        value: root.develop ? root.develop.spotFeather * 100 : 1
        onEdited: v => { if (root.develop) root.develop.spotFeather = v / 100 }
    }
    Row {
        x: Theme.s3; spacing: Theme.s2
        ToolButton {
            iconName: "crosshair"; text: root.placing ? qsTr("Placing — click the picture") : qsTr("Place spots"); showLabel: true
            tip: qsTr("Then click a blemish in the picture; each click lays one spot with the tool, size and feather set here.")
            enabled: engine.imageId >= 0
            checked: root.placing
            onClicked: if (root.develop) root.develop.spotMode = !root.develop.spotMode
        }
    }
    Text {
        x: Theme.s3; width: parent.width - Theme.s3 * 2; wrapMode: Text.WordWrap
        text: engine.imageId < 0 ? qsTr("Open a photo in the engine first.")
              : root.spots.length === 0 ? qsTr("Press Place, then click a blemish. Heal and clone read from the source circle beside the spot — drag it to choose better pixels.")
              : qsTr("Drag a shape or its source on the picture. Polygon: click corners, double-click to finish; right-click cancels. Click a shape here to edit it.")
        font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
    }
    Item { width: 1; height: Theme.s1 }
    Repeater {
        model: spotList.model
        Column {
            id: item
            required property int index
            required property var modelData
            readonly property var live: root.spots[index] || modelData
            readonly property bool active: engine.activeSpot === index
            readonly property bool sourced: live.algorithm === 1 || live.algorithm === 2
            width: parent ? parent.width : 0
            spacing: 0
            Rectangle {
                width: parent.width; height: Theme.hRow
                color: item.active ? Theme.controlBg : "transparent"
                Rectangle { anchors.left: parent.left; width: 2; height: parent.height; color: Theme.accent; visible: item.active }
                Icon {
                    anchors.left: parent.left; anchors.leftMargin: Theme.s3
                    anchors.verticalCenter: parent.verticalCenter
                    name: root.toolIcons[item.live.algorithm] || "bandage"; size: 12
                    color: item.active ? Theme.accent : Theme.textMuted
                }
                Text {
                    anchors.left: parent.left; anchors.leftMargin: Theme.s3 + 20
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("%1 %2").arg(root.toolNames[item.live.algorithm] || qsTr("Spot")).arg(item.index + 1)
                    font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
                    color: item.active ? Theme.textPrimary : Theme.textSecondary
                }
                IconButton {
                    anchors.right: parent.right; anchors.rightMargin: Theme.s2
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "trash-2"; text: qsTr("Remove this spot")
                    tip: qsTr("Takes this spot off the picture.")
                    onClicked: engine.removeSpot(item.index)
                }
                TapHandler { onTapped: engine.activeSpot = item.index }
            }
            Column {
                visible: item.active
                width: parent.width
                spacing: 0
                function push(cx, cy, r, b, sx, sy, o) { engine.setSpot(item.index, cx, cy, r, b, sx, sy, o) }
                ComboField {
                    x: Theme.s3; width: parent.width - Theme.s3 * 2; visible: root.layers > 0
                    model: root.layerLabels; currentIndex: item.live.scale || 0
                    tipTitle: qsTr("This shape's layer"); tip: qsTr("Choose the frequency layer affected by this shape.")
                    onActivated: i => engine.toolAction("retouch", "scale", {index: item.index, scale: i})
                }
                Item {
                    width: parent.width; height: Theme.hRow + Theme.s1
                    SegmentedControl {
                        anchors.left: parent.left; anchors.leftMargin: Theme.s3 + 12
                        anchors.verticalCenter: parent.verticalCenter
                        labels: [qsTr("Heal"), qsTr("Clone"), qsTr("Blur"), qsTr("Fill")]
                        tips: [qsTr("Copies texture from the source and blends its colour into the surroundings."), qsTr("Copies the source exactly."), qsTr("Softens what is under the spot."), qsTr("Paints the spot with a flat tone.")]
                        currentIndex: [2, 1, 3, 4].indexOf(item.live.algorithm)
                        onActivated: i => engine.setSpotAlgorithm(item.index, [2, 1, 3, 4][i], item.live.blurRadius, item.live.fillBrightness)
                    }
                }
                SliderField {
                    x: Theme.s3 + 12; width: parent.width - Theme.s3 * 2 - 12; stacked: true
                    label: qsTr("Size"); from: 0.5; to: 30; stepSize: 0.5; decimals: 1; suffix: " %"
                    tip: qsTr("Radius of this spot, as a percentage of the picture.")
                    value: item.live.radius * 100
                    onEditingFinished: v => parent.push(item.live.cx, item.live.cy, v / 100, item.live.border, item.live.sx, item.live.sy, item.live.opacity)
                }
                SliderField {
                    x: Theme.s3 + 12; width: parent.width - Theme.s3 * 2 - 12; stacked: true
                    label: qsTr("Feather"); from: 0; to: 20; stepSize: 0.5; decimals: 1; suffix: " %"
                    tip: qsTr("How softly this spot's edge blends in, as a percentage of the picture.")
                    value: item.live.border * 100
                    onEditingFinished: v => parent.push(item.live.cx, item.live.cy, item.live.radius, v / 100, item.live.sx, item.live.sy, item.live.opacity)
                }
                SliderField {
                    x: Theme.s3 + 12; width: parent.width - Theme.s3 * 2 - 12; stacked: true
                    label: qsTr("Opacity"); from: 0; to: 100; stepSize: 5; decimals: 0; suffix: " %"
                    tip: qsTr("How much of the repair shows; lower lets some of the original through.")
                    value: item.live.opacity * 100
                    onEditingFinished: v => parent.push(item.live.cx, item.live.cy, item.live.radius, item.live.border, item.live.sx, item.live.sy, v / 100)
                }
                SliderField {
                    visible: item.live.algorithm === 3
                    x: Theme.s3 + 12; width: parent.width - Theme.s3 * 2 - 12; stacked: true
                    label: qsTr("Blur radius"); from: 0.1; to: 200; stepSize: 0.5; decimals: 1; suffix: " px"
                    tip: qsTr("How strongly the spot is softened.")
                    value: item.live.blurRadius
                    onEditingFinished: v => engine.setSpotAlgorithm(item.index, item.live.algorithm, v, item.live.fillBrightness)
                }
                SliderField {
                    visible: item.live.algorithm === 4
                    x: Theme.s3 + 12; width: parent.width - Theme.s3 * 2 - 12; stacked: true
                    label: qsTr("Brightness"); from: -100; to: 100; stepSize: 5; decimals: 0; origin: 0
                    tip: qsTr("How light or dark the flat fill is.")
                    value: item.live.fillBrightness * 100
                    onEditingFinished: v => engine.setSpotAlgorithm(item.index, item.live.algorithm, item.live.blurRadius, v / 100)
                }
                Item { width: 1; height: Theme.s1 }
            }
        }
    }
}
