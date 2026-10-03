import QtQuick
import QtQuick.Controls as C

// Numeric field with horizontal drag-scrubbing — the Inspector's Position,
// Scale and Rotation boxes. Dragging is how these are actually used in an
// edit; typing is the precise fallback, so both must work and neither may
// fight the other: a press that never travels becomes a text edit, a press
// that travels becomes a scrub.
Rectangle {
    id: root

    property real value: 0
    property real from: -999999
    property real to: 999999
    // Value change per pixel dragged. Fine drag (Shift) divides this by ten.
    property real step: 1.0
    property int decimals: 1
    // Whole figures print without their decimals ("12", but "12.5").
    property bool adaptiveDecimals: false
    // The grid a typed, scrubbed or stepped value lands on (0 = the shown
    // decimals only): a whole-number control never takes 3.6 to truncate.
    property real snap: 0
    property string suffix: ""
    property string label: ""
    property bool editable: true
    // The tooltip's sentence; the scrub hint follows it.
    property string tip: ""

    // A quiet readout: plain right-aligned figures until the pointer or the
    // keyboard reaches it, for panels where a column of boxes reads as noise.
    property bool flat: false
    readonly property bool scrubbing: scrub.scrubbing
    property bool wheelEnabled: false
    signal editingFinished(real value)
    signal edited(real value)

    implicitWidth: Math.max(72, Math.ceil(valueMetrics.advanceWidth) + Theme.s2 * 2 + 2 * Theme.hairline)
    implicitHeight: Theme.hControl
    color: root.flat && !input.activeFocus && !scrub.containsMouse && !scrub.scrubbing ? "transparent" : Theme.inputBg
    radius: Theme.rControl
    border.width: Theme.hairline
    border.color: root.flat && !input.activeFocus && !scrub.containsMouse && !scrub.scrubbing ? "transparent"
                : input.activeFocus ? Theme.accent
                : scrub.scrubbing ? Theme.borderStrong
                : scrub.containsMouse ? Theme.borderStrong : Theme.border
    opacity: enabled ? 1.0 : Theme.disabledOpacity

    Accessible.role: Accessible.SpinBox
    Accessible.name: label
    Accessible.description: display()

    function figure(v) {
        return adaptiveDecimals && Math.abs(v - Math.round(v)) < 0.5 * Math.pow(10, -decimals) ? Math.round(v).toFixed(0) : v.toFixed(decimals)
    }
    function display() {
        return figure(value) + (suffix ? " " + suffix : "")
    }

    function commit(v) {
        if (!Number.isFinite(v)) return
        if (snap > 0) v = from + Math.round((v - from) / snap) * snap
        v = Number(v.toFixed(Math.max(decimals, snap > 0 ? Math.max(0, -Math.floor(Math.log10(snap) + 1e-9)) : 0)))
        const clamped = Math.max(from, Math.min(to, v))
        if (clamped !== value) {
            value = clamped
            edited(clamped)
        }
    }

    Tooltip {
        text: root.label
        description: (root.tip !== "" ? root.tip + " " : "")
                     + (root.editable ? qsTr("Drag sideways to change, Shift for fine steps; click to type a value.") : "")
        visible: scrub.containsMouse && !scrub.scrubbing && !input.activeFocus && root.label !== "" && description !== ""
    }

    TextMetrics {
        id: valueMetrics
        font.family: Theme.monoFamily
        font.pixelSize: Theme.fsControl
        text: [root.from, root.to, root.value].map(v => v.toFixed(root.decimals)).reduce((a, b) => a.length >= b.length ? a : b)
              + (root.suffix ? " " + root.suffix : "")
    }

    C.TextField {
        id: input
        objectName: "valueInput"
        anchors.fill: parent
        anchors.leftMargin: Theme.s2
        anchors.rightMargin: Theme.s2
        readOnly: !root.editable

        text: root.display()
        color: Theme.textPrimary
        font.family: Theme.monoFamily
        font.pixelSize: Theme.fsControl
        horizontalAlignment: root.flat && !activeFocus ? Text.AlignRight : Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        selectionColor: Theme.accent
        selectedTextColor: Theme.accentText
        padding: 0
        background: null
        validator: DoubleValidator { bottom: root.from; top: root.to; locale: "C" }

        onActiveFocusChanged: {
            // Entering the field swaps the formatted display for a raw,
            // fully-selected number so the whole value is replaceable.
            if (activeFocus) {
                text = root.figure(root.value)
                selectAll()
            } else {
                if (acceptableInput) root.commit(Number(text))
                text = Qt.binding(() => root.display())
            }
        }
        onAccepted: {
            root.commit(parseFloat(text))
            focus = false
        }
        Keys.onEscapePressed: focus = false
        Keys.onUpPressed: root.commit(root.value + root.step)
        Keys.onDownPressed: root.commit(root.value - root.step)
    }

    // Sits above the field and swallows the press only while the field is not
    // being typed into; once it has focus, this steps aside so selection,
    // caret placement and drag-select all behave normally.
    MouseArea {
        id: scrub
        anchors.fill: parent
        enabled: root.editable && !input.activeFocus
        hoverEnabled: true
        cursorShape: scrubbing ? Qt.SizeHorCursor
                   : containsMouse ? Qt.SizeHorCursor : Qt.ArrowCursor

        property bool scrubbing: false
        property real startX: 0
        property real startValue: 0

        onPressed: function (mouse) {
            startX = mouse.x
            startValue = root.value
            scrubbing = false
        }

        onPositionChanged: function (mouse) {
            if (!pressed) return
            const dx = mouse.x - startX
            if (!scrubbing) {
                if (Math.abs(dx) < Theme.dragThreshold) return
                scrubbing = true
            }
            const fine = (mouse.modifiers & Qt.ShiftModifier) !== 0
            root.commit(startValue + dx * root.step * (fine ? 0.1 : 1.0))
        }

        onReleased: {
            // A press that never travelled is a request to type, not to scrub.
            if (!scrubbing)
                input.forceActiveFocus()
            const wasScrubbing = scrubbing
            scrubbing = false
            if (wasScrubbing) root.editingFinished(root.value)
        }
        onCanceled: { scrubbing = false; root.editingFinished(root.value) }
    }
    WheelHandler {
        enabled: root.wheelEnabled
        target: null
        onWheel: event => {
            const ticks = event.angleDelta.y ? event.angleDelta.y / 120 : event.pixelDelta.y / 30
            root.commit(root.value + ticks * root.step * ((event.modifiers & Qt.ShiftModifier) ? 0.1 : 1))
            root.editingFinished(root.value)
            event.accepted = true
        }
    }
}
