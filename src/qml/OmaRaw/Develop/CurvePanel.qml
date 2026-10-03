pragma ComponentBehavior: Bound
import QtQuick
import OmaRaw.Ui

// Tone curve: the engine's rgb curve module as a point curve. RGB shapes
// one curve for all three channels; R, G and B split them (the shared
// curve is copied to each first, so the picture holds until one moves).
Column {
    id: root
    property bool saveEnabled: true
    signal savePresetRequested(string operation, string label)
    readonly property var curve: engine.curve || ({})
    readonly property bool found: curve.found === true
    readonly property bool linked: curve.linked !== false
    readonly property bool moduleOn: curve.enabled === true
    // 0 = RGB while linked, else red; 1 green; 2 blue
    property int channel: 0
    readonly property int segment: linked ? 0 : channel + 1
    readonly property var channels: curve.channels || []
    readonly property var current: channel < channels.length ? channels[channel] : ({})
    readonly property int curveType: current.type !== undefined ? current.type : 2
    readonly property var lineColors: [Theme.accent, "#F04545", "#21C45E", "#3B82F6"]
    // Interpolation choices in the order shown; engine types 2, 1, 0.
    readonly property var typeOrder: [2, 1, 0]
    spacing: Theme.s1

    function apply(xs, ys) { engine.setCurve(root.channel, xs, ys, root.curveType) }
    readonly property var parametric: engine.parametric || ({})

    // Parametric: four regions over the display-referred tone curve module.
    BlockHeading {
        text: qsTr("Tone regions"); tip: qsTr("Lighten or darken the highlights, lights, darks and shadows with four sliders: a curve without drawing one.")
        operation: root.parametric.found === true ? "tonecurve" : ""; on: root.parametric.enabled === true
        eyeName: "parametricEnabled"; saveEnabled: root.saveEnabled
        onEyeClicked: engine.setModuleEnabled("tonecurve", !root.parametric.enabled)
        onSavePresetRequested: (operation, label) => root.savePresetRequested(operation, label)
        IconButton { iconName: "rotate-ccw"; text: qsTr("Reset the regions"); tip: qsTr("Puts highlights, lights, darks and shadows back to zero."); enabled: root.parametric.found === true; onClicked: engine.resetParametric() }
    }
    Repeater {
        model: [[0, qsTr("Highlights"), "highlights"], [1, qsTr("Lights"), "lights"], [2, qsTr("Darks"), "darks"], [3, qsTr("Shadows"), "shadows"]]
        Item {
            id: region
            required property var modelData
            width: parent.width; height: regionField.implicitHeight + Theme.s1
            EditCoalescer {
                id: rthrottle
                onSend: v => engine.setParametric(region.modelData[0], v)
            }
            SliderField {
                id: regionField
                anchors.left: parent.left; anchors.right: parent.right
                anchors.leftMargin: Theme.s3; anchors.rightMargin: Theme.s3
                anchors.verticalCenter: parent.verticalCenter
                stacked: true; resetOnDoubleClick: true
                label: region.modelData[1]
                tip: qsTr("Lightens or darkens this part of the tonal range; the four regions blend into each other. Double-click resets it.")
                labelWidth: 96
                from: -100; to: 100; origin: 0; decimals: 0
                value: root.parametric[region.modelData[2]] !== undefined ? root.parametric[region.modelData[2]] : 0
                opacity: root.parametric.enabled === true ? 1 : 0.7
                onEdited: v => rthrottle.push(v)
                onEditingFinished: v => rthrottle.flush(v)
                TapHandler { acceptedButtons: Qt.RightButton; onTapped: engine.setParametric(region.modelData[0], 0) }
            }
        }
    }
    Item { width: parent.width; height: Theme.s2 }

    BlockHeading {
        text: qsTr("Curve"); tip: qsTr("Draw the tone curve yourself: click to add a point and drag it; right-click a point, or drag it off the graph, to remove it.")
        operation: root.found ? "rgbcurve" : ""; on: root.moduleOn; saveEnabled: root.saveEnabled
        onEyeClicked: engine.setModuleEnabled("rgbcurve", !root.moduleOn)
        onSavePresetRequested: (operation, label) => root.savePresetRequested(operation, label)
    }
    Item {
        width: parent.width; height: Theme.hControl
        SegmentedControl {
            anchors.left: parent.left; anchors.leftMargin: Theme.s3
            anchors.verticalCenter: parent.verticalCenter
            labels: ["RGB", "R", "G", "B"]
            tips: [qsTr("One curve for all three channels."), qsTr("A curve for red alone; the channels unlink."), qsTr("A curve for green alone; the channels unlink."), qsTr("A curve for blue alone; the channels unlink.")]
            currentIndex: root.segment
            enabled: root.found
            onActivated: i => {
                if (i === 0) { engine.setCurveLinked(true); root.channel = 0 }
                else { if (root.linked) engine.setCurveLinked(false); root.channel = i - 1 }
            }
        }
        IconButton {
            anchors.right: parent.right; anchors.rightMargin: Theme.s3
            anchors.verticalCenter: parent.verticalCenter
            iconName: "rotate-ccw"
            text: qsTr("Reset this curve to a straight line")
            tip: qsTr("Removes every point on the channel shown.")
            enabled: root.found
            onClicked: { editor.selected = -1; engine.resetCurve(root.channel) }
        }
    }
    CurveEditor {
        id: editor
        objectName: "curveEditor"
        x: Theme.s3
        width: parent.width - Theme.s3 * 2
        height: width
        channel: root.current
        lineColor: root.lineColors[root.segment]
        dimmed: !root.moduleOn
        enabled: root.found
        onEdited: (xs, ys, final) => {
            if (final) throttle.flush({xs: xs, ys: ys})
            else throttle.push({xs: xs, ys: ys})
        }
    }
    EditCoalescer {
        id: throttle
        onSend: p => root.apply(p.xs, p.ys)
    }
    Item {
        width: parent.width; height: Theme.hControl
        Text {
            anchors.left: parent.left; anchors.leftMargin: Theme.s3
            anchors.verticalCenter: parent.verticalCenter
            text: editor.selected >= 0 && editor.selected < editor.xs.length
                  ? qsTr("Node %1: %2 → %3").arg(editor.selected + 1).arg(editor.xs[editor.selected].toFixed(3)).arg(editor.ys[editor.selected].toFixed(3))
                  : editor.hoverX >= 0 ? qsTr("%1 → %2").arg(editor.hoverX.toFixed(3)).arg(editor.curveAt(editor.hoverX).toFixed(3))
                  : qsTr("%1 nodes").arg(editor.xs.length)
            font.family: Theme.monoFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
        }
        ComboField {
            anchors.right: parent.right; anchors.rightMargin: Theme.s3
            anchors.verticalCenter: parent.verticalCenter
            width: 128
            model: [qsTr("Monotone"), qsTr("Catmull-Rom"), qsTr("Cubic spline")]
            tipTitle: qsTr("Curve type"); tip: qsTr("How the line runs between points: monotone never overshoots, the splines are smoother.")
            currentIndex: Math.max(0, root.typeOrder.indexOf(root.curveType))
            enabled: root.found
            onActivated: i => engine.setCurve(root.channel, root.current.xs || [0, 1], root.current.ys || [0, 1], root.typeOrder[i])
        }
    }
    Text {
        x: Theme.s3; width: parent.width - Theme.s3 * 2
        text: qsTr("Click the curve to add a node, drag to shape it, right-click a node to remove it. Mid grey sits at the centre.")
        wrapMode: Text.WordWrap
        font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
    }
}
