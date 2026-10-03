pragma ComponentBehavior: Bound
import QtQuick
import OmaRaw.Ui

// Lab colour. Lab keeps brightness apart from colour, so colour can be pulled
// apart, boosted on one side or shifted without the picture getting lighter,
// darker or harsher. The sliders are the whole tool for most people; each one
// only places points on the two Lab colour curves (see labcolour.h), which
// "Show curves" opens for shaping by hand. The curves belong to the engine's
// Lab tone curve, the module whose lightness curve Tone regions drive.
Column {
    id: root
    property bool embedded: false
    objectName: "labCurvePanel"
    readonly property var parametric: engine.parametric || ({})
    readonly property var curves: parametric.ab || []
    readonly property var lab: parametric.lab || ({})
    readonly property bool found: parametric.found === true && curves.length === 2
    readonly property bool moduleOn: parametric.enabled === true
    readonly property bool custom: lab.custom === true
    readonly property bool changed: curves.some(c => (c.xs || []).some((x, i) => Math.abs(x - c.ys[i]) > 1e-5))
    // Folded to its heading until asked for.
    property bool open: false
    property bool showCurves: false
    // 0 = a, 1 = b
    property int axis: 0
    readonly property var current: curves[axis] || ({})
    readonly property int curveType: current.type !== undefined ? current.type : 2
    readonly property var typeOrder: [2, 1, 0]
    function apply(xs, ys) { engine.setLabCurve(root.axis + 1, xs, ys, root.curveType) }
    spacing: Theme.s1
    visible: found
    height: visible ? implicitHeight : 0

    // One slider of the tool: −100 to 100 around a neutral 0.
    component LabSlider: Item {
        id: slot
        property string key: ""
        property string label: ""
        property string tip: ""
        property color dot: "transparent"
        property real from: -100
        objectName: "labSlider_" + key
        width: parent ? parent.width : 0
        height: slider.implicitHeight + Theme.s1
        EditCoalescer {
            id: throttle
            onSend: v => engine.setLabColour(slot.key, v)
        }
        Rectangle {
            visible: slot.dot.a > 0
            x: Theme.s3; y: (Theme.hRow - height) / 2
            width: 10; height: 10; radius: 5; color: slot.dot
        }
        SliderField {
            id: slider
            anchors.left: parent.left; anchors.right: parent.right
            anchors.leftMargin: Theme.s3; anchors.rightMargin: Theme.s3
            captionIndent: slot.dot.a > 0 ? 18 : 0
            stacked: true; resetOnDoubleClick: true
            label: slot.label
            tip: slot.tip + " " + qsTr("Double-click resets it.")
            from: slot.from; to: 100; origin: 0; decimals: 0
            value: root.lab[slot.key] !== undefined ? root.lab[slot.key] : 0
            opacity: root.moduleOn || !root.changed ? 1 : 0.6
            onEdited: v => throttle.push(v)
            onEditingFinished: v => throttle.flush(v)
        }
    }

    BlockHeading {
        visible: !root.embedded
        height: visible ? Theme.hRow + Theme.s1 : 0
        text: qsTr("Lab colour")
        tip: qsTr("Richer, better separated colour without touching brightness. One slider for the whole picture, four for the colour families, two to take out a cast.")
        collapsible: true; open: root.open; onToggled: root.open = !root.open
        IconButton {
            objectName: "resetLabCurves"
            iconName: "rotate-ccw"; text: qsTr("Reset Lab colour")
            tip: qsTr("Puts every slider here back to zero and both curves back to a straight line. Tone regions keep their settings.")
            enabled: root.changed
            onClicked: { editor.selected = -1; engine.resetLabColour() }
        }
    }
    Column {
        visible: root.open || height > 0
        width: parent.width; height: root.open ? implicitHeight : 0
        // Clipped while it folds, so it does not paint over the lines below.
        clip: labFold.running
        Behavior on height { NumberAnimation { id: labFold; duration: Theme.dSlow; easing.type: Theme.easing } }
        spacing: Theme.s1
        // With the curves open this note sits under them instead: appearing
        // here, above the graph, it pushed the graph down under the pointer
        // the moment a drag made the curve hand-shaped.
        Text {
            visible: root.custom && !root.showCurves
            x: Theme.s3; width: parent.width - Theme.s3 * 2
            text: qsTr("The curves below were shaped by hand, so the sliders read zero. Moving a slider replaces the hand-made shape.")
            wrapMode: Text.WordWrap
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
        }
        LabSlider {
            key: "separation"; label: qsTr("Colour separation")
            tip: qsTr("Pulls the picture's colours apart from each other and from grey. Muted colours gain the most, strong ones are held back, greys stay grey and brightness does not move.")
        }
        Text {
            x: Theme.s3; topPadding: Theme.s1
            text: qsTr("By colour family")
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
        }
        LabSlider { key: "greens"; label: qsTr("Greens"); dot: "#79b95c"; tip: qsTr("Richer or quieter greens only: foliage, grass. The other families are left alone.") }
        LabSlider { key: "magentas"; label: qsTr("Reds & magentas"); dot: "#d9607f"; tip: qsTr("Richer or quieter reds, pinks and magentas only: flowers, lips, brickwork.") }
        LabSlider { key: "blues"; label: qsTr("Blues"); dot: "#639de4"; tip: qsTr("Richer or quieter blues only: skies, water, shade.") }
        LabSlider { key: "yellows"; label: qsTr("Yellows & warm tones"); dot: "#ddc25d"; tip: qsTr("Richer or quieter yellows and oranges only: sunlight, sand, skin.") }
        Text {
            x: Theme.s3; topPadding: Theme.s1
            text: qsTr("Remove a cast")
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
        }
        LabSlider { key: "tintA"; label: qsTr("Green–magenta"); tip: qsTr("Slides every colour towards green (left) or magenta (right). Move it away from the cast you see.") }
        LabSlider { key: "tintB"; label: qsTr("Blue–yellow"); tip: qsTr("Slides every colour towards blue (left) or yellow (right): cooler or warmer, after all the other processing.") }

        ToolButton {
            objectName: "showLabCurves"
            x: Theme.s3
            iconName: root.showCurves ? "chevron-up" : "chevron-down"
            text: root.showCurves ? qsTr("Hide curves") : qsTr("Show curves"); showLabel: true
            tip: qsTr("The two Lab colour curves these sliders shape. Bend them by hand for anything the sliders cannot do.")
            onClicked: root.showCurves = !root.showCurves
        }
        Column {
            visible: root.showCurves
            width: parent.width; height: visible ? implicitHeight : 0
            spacing: Theme.s1
            Item {
                width: parent.width; height: Theme.hControl
                SegmentedControl {
                    objectName: "labCurveAxis"
                    anchors.left: parent.left; anchors.leftMargin: Theme.s3
                    anchors.verticalCenter: parent.verticalCenter
                    labels: [qsTr("a · green–magenta"), qsTr("b · blue–yellow")]
                    tips: [qsTr("Lab's a axis: green at the left and bottom, magenta at the right and top."), qsTr("Lab's b axis: blue at the left and bottom, yellow at the right and top.")]
                    currentIndex: root.axis
                    onActivated: i => { editor.selected = -1; root.axis = i }
                }
                IconButton {
                    anchors.right: parent.right; anchors.rightMargin: Theme.s3
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "rotate-ccw"
                    text: qsTr("Reset this curve to a straight line")
                    onClicked: { editor.selected = -1; engine.resetLabCurve(root.axis + 1) }
                }
            }
            CurveEditor {
                id: editor
                objectName: "labCurveEditor"
                x: Theme.s3
                width: parent.width - Theme.s3 * 2
                height: width
                channel: root.current
                showHistogram: false
                axisFrom: root.axis === 0 ? "#3fae6a" : "#4a7fe0"
                axisTo: root.axis === 0 ? "#d457b5" : "#e2c53f"
                lineColor: Theme.accent
                dimmed: !root.moduleOn
                onEdited: (xs, ys, final) => {
                    if (final) curveThrottle.flush({xs: xs, ys: ys})
                    else curveThrottle.push({xs: xs, ys: ys})
                }
            }
            EditCoalescer {
                id: curveThrottle
                onSend: p => root.apply(p.xs, p.ys)
            }
            Item {
                id: readout
                width: parent.width; height: Theme.hControl
                // In Lab's own units, −128 to +127, the way other editors show them.
                function units(v) { return Math.round(v * 256 - 128) }
                Text {
                    anchors.left: parent.left; anchors.leftMargin: Theme.s3
                    anchors.verticalCenter: parent.verticalCenter
                    text: editor.selected >= 0 && editor.selected < editor.xs.length
                          ? qsTr("Point %1: %2 → %3").arg(editor.selected + 1).arg(readout.units(editor.xs[editor.selected])).arg(readout.units(editor.ys[editor.selected]))
                          : editor.hoverX >= 0 ? qsTr("%1 → %2").arg(readout.units(editor.hoverX)).arg(readout.units(editor.curveAt(editor.hoverX)))
                          : qsTr("%1 points").arg(editor.xs.length)
                    font.family: Theme.monoFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
                }
                ComboField {
                    anchors.right: parent.right; anchors.rightMargin: Theme.s3
                    anchors.verticalCenter: parent.verticalCenter
                    width: 128
                    model: [qsTr("Monotone"), qsTr("Catmull-Rom"), qsTr("Cubic spline")]
                    tipTitle: qsTr("Curve type"); tip: qsTr("How the line runs between points: monotone never overshoots, the splines are smoother.")
                    currentIndex: Math.max(0, root.typeOrder.indexOf(root.curveType))
                    onActivated: i => engine.setLabCurve(root.axis + 1, root.current.xs || [0, 0.5, 1], root.current.ys || [0, 0.5, 1], root.typeOrder[i])
                }
            }
            Text {
                objectName: "labCustomNote"
                visible: root.custom
                x: Theme.s3; width: parent.width - Theme.s3 * 2
                text: qsTr("These curves were shaped by hand, so the sliders above read zero. Moving a slider replaces the hand-made shape.")
                wrapMode: Text.WordWrap
                font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
            }
            Text {
                x: Theme.s3; width: parent.width - Theme.s3 * 2
                text: qsTr("The centre is neutral grey: keep the line through it and greys stay grey. Most colours sit near the centre, so small moves go a long way.")
                wrapMode: Text.WordWrap
                font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
            }
        }
        Text {
            visible: root.changed
            x: Theme.s3; width: parent.width - Theme.s3 * 2
            text: qsTr("While Lab colour is in use, Tone regions (under Adjust ▸ Curves) act on lightness alone.")
            wrapMode: Text.WordWrap
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
        }
    }
}
