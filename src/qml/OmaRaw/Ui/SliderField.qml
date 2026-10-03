import QtQuick
import QtQuick.Controls as C

// Label + track + numeric readout, as used for Opacity and the grading
// controls. The readout is a real ValueField, so a value can always be typed
// exactly rather than hunted for with the handle.
Item {
    id: root

    readonly property bool pressed: slider.pressed || trackPointer.pressed || readout.scrubbing || finishDelay.running
    property bool resetOnDoubleClick: false
    property bool wheelEnabled: false
    property real fieldFrom: from
    property real fieldTo: to
    property int fieldDecimals: decimals
    property real fieldStep: stepSize > 0 ? stepSize : (to - from) / 200
    // What an arrow key moves: a round figure near 1/200 of the range, never
    // finer than the readout shows. Shift makes it ten times, Alt a tenth.
    readonly property real keyStep: {
        const raw = stepSize > 0 ? stepSize : (to - from) / 200
        if (!(raw > 0)) return 1
        const mag = Math.pow(10, Math.floor(Math.log10(raw)))
        const n = raw / mag
        const nice = n < 1.5 ? 1 : n < 3.5 ? 2 : n < 7.5 ? 5 : 10
        return Math.max(nice * mag, Math.pow(10, -decimals))
    }
    function nudge(direction, modifiers) {
        let step = keyStep
        if (modifiers & Qt.ShiftModifier) step *= 10
        else if (modifiers & Qt.AltModifier) step = Math.max(step / 10, fineStep > 0 ? fineStep : Math.pow(10, -decimals))
        const next = Math.max(from, Math.min(to, shownValue + direction * step))
        if (next === shownValue) return
        finishDelay.stop()
        root.ask(next); root.finish(next)
    }
    property bool showReadout: true
    // Caption and figure on one line, the track across the full width below:
    // a longer track is a finer one, and the column of boxes goes away.
    property bool stacked: false
    // Away from its origin. Stacked rows brighten their caption and offer a
    // reset button while this holds.
    readonly property bool modified: Math.abs(shownValue - origin) > (to - from) * 1e-5
    // Emitted by the reset button; callers with their own idea of a reset
    // (an engine default rather than the origin) connect here instead.
    signal resetRequested()
    property bool ownReset: false
    // Stacked: width a caller keeps for its own buttons left of the figure,
    // and the x at which that room ends.
    property real captionReserve: 0
    // Stacked: room before the caption for a caller's own marker (a colour
    // dot), so the track below keeps the full width and lines up with its
    // neighbours.
    property real captionIndent: 0
    readonly property real trailingX: offIndicator.visible ? offIndicator.x - Theme.s1 : resetButton.x
    // Shown in place of the figure while the control's adjustment is not
    // applied: a number there would claim an effect the picture does not have.
    property bool notApplied: false
    property bool showOffIndicator: true
    // Whether the origin is a value worth going back to (a default, not
    // merely the low end of the range): enables the reset button.
    property bool resettable: resetOnDoubleClick
    property string label: ""
    property real value: 0
    property real from: 0
    property real to: 100
    property real stepSize: 0
    // Values near the low end get more track space without giving up the
    // full range. Numeric entry and key steps remain in the displayed units.
    property real responsePower: 1
    function mapTrack(v, power) {
        if (power === 1) return v
        if (to === from) return from
        const position = Math.max(0, Math.min(1, (v - from) / (to - from)))
        return from + (to - from) * Math.pow(position, power)
    }
    function trackValue(v) { return mapTrack(v, 1 / Math.max(1, responsePower)) }
    function valueForTrack(v) { return mapTrack(v, Math.max(1, responsePower)) }
    // The finer grid for Shift-drags, Alt-arrows and typed figures; the
    // plain step unless a control allows finer (a ±100 figure takes tenths).
    property real fineStep: stepSize
    property int decimals: 1
    property bool adaptiveDecimals: false
    property string suffix: ""
    property int labelWidth: 84
    // Where the fill starts. Bipolar controls (a −1..1 colour trim) fill out
    // from the centre instead of from the left.
    property real origin: from
    // One sentence on what the control does, shown over the track. Never
    // while dragging: the handle is the thing under the pointer then.
    property string tip: ""
    // The tooltip's title, for a slider that shows no caption of its own.
    property string tipTitle: label

    signal edited(real value)
    // Emitted once at the end of a drag, for callers that must not commit an
    // undo step per pixel.
    signal editingFinished(real value)

    // What the handle and figure show. `value` is usually the engine's
    // read-back, which waits behind the render in flight, so a control drawn
    // from it trails the pointer. While a gesture lasts, and until the
    // read-back catches up, the control shows what was asked for instead.
    readonly property real shownValue: holding ? heldValue : value
    property real heldValue: 0
    property bool holding: false
    property bool editCanceled: false
    readonly property real settleTolerance: Math.max((to - from) * 1e-4, 0.5 * Math.pow(10, -decimals))
    function ask(v) {
        editCanceled = false
        heldValue = v; holding = true; settle.restart()
        root.edited(v)
    }
    function release() { settle.stop(); holding = false }
    function finish(v) { if (!editCanceled) root.editingFinished(v) }
    function discardEdit() { finishDelay.stop(); editCanceled = true; release() }
    onValueChanged: if (holding && !pressed && Math.abs(value - heldValue) <= settleTolerance) release()
    Timer {
        id: settle
        // Long enough for a render on a large RAW; a read-back that never
        // matches (the engine rounded or clamped it) takes over after this.
        interval: 1500
        onTriggered: if (root.pressed) restart(); else root.holding = false
    }
    // Another photo's figure is never what was asked for.
    Connections {
        target: typeof engine !== "undefined" ? engine : null
        ignoreUnknownSignals: true
        // A delayed finish or release must not restore the replaced edit.
        function onEditStateReplaced() { root.discardEdit() }
        function onImageChanged() { root.discardEdit() }
        function onHistoryJumped() { root.discardEdit() }
        function onAutoExposureRequested() {
            if (finishDelay.running) { finishDelay.stop(); root.finish(trackPointer.last) }
        }
    }

    implicitHeight: stacked ? Theme.hRow + Theme.hControl - Theme.s1 : Theme.hControl
    // The wheel scrolls the panel; with Ctrl held it steps the slider under
    // the pointer instead, by the arrow keys' step (Shift for ten times).
    // (acceptedModifiers is an exact match, so one handler per combination.)
    function wheelStep(event) {
        const ticks = event.angleDelta.y ? event.angleDelta.y / 120 : event.pixelDelta.y / 30
        if (ticks !== 0) root.nudge(ticks > 0 ? 1 : -1, event.modifiers & Qt.ShiftModifier)
    }
    WheelHandler { acceptedModifiers: Qt.ControlModifier; enabled: root.enabled; onWheel: event => root.wheelStep(event) }
    WheelHandler { acceptedModifiers: Qt.ControlModifier | Qt.ShiftModifier; enabled: root.enabled; onWheel: event => root.wheelStep(event) }
    implicitWidth: labelWidth + 160 + Theme.s2 * 2
    HoverHandler { id: rowHover }
    opacity: enabled ? 1.0 : Theme.disabledOpacity

    Text {
        id: caption
        text: root.label
        visible: root.label !== ""
        width: root.stacked ? Math.max(0, Math.min(implicitWidth, parent.width - readout.width - resetButton.width - root.captionReserve - root.captionIndent - Theme.s2 - (offIndicator.visible ? offIndicator.width : 0))) : root.labelWidth
        height: root.stacked ? Theme.hRow : implicitHeight
        verticalAlignment: Text.AlignVCenter
        anchors.left: parent.left
        anchors.leftMargin: root.stacked ? root.captionIndent : 0
        anchors.top: root.stacked ? parent.top : undefined
        anchors.verticalCenter: root.stacked ? undefined : parent.verticalCenter
        font.family: Theme.fontFamily
        font.pixelSize: root.stacked ? Theme.fsControl : Theme.fsLabel
        color: root.stacked && root.modified ? Theme.textPrimary : Theme.textSecondary
        elide: Text.ElideRight
        objectName: "sliderCaption"
        // The name is a reset target too, as it is in the editors people arrive from.
        TapHandler {
            enabled: root.resetOnDoubleClick
            onDoubleTapped: if (root.ownReset) { root.release(); root.resetRequested() } else root.resetValue()
        }
    }
    IconButton {
        id: resetButton
        objectName: "sliderReset"
        visible: root.stacked && root.resettable && !root.notApplied && root.modified && (rowHover.hovered || activeFocus)
        width: visible ? Theme.hRow : 0; height: Theme.hRow
        anchors.right: readout.left; anchors.top: parent.top
        iconName: "rotate-ccw"; text: qsTr("Reset %1").arg(root.tipTitle)
        tip: qsTr("Puts this control back where it started. Double-clicking the slider does the same.")
        onClicked: if (root.ownReset) { root.release(); root.resetRequested() } else root.resetValue()
    }

    C.Slider {
        id: slider
        objectName: "slider"
        anchors.left: root.label !== "" && !root.stacked ? caption.right : parent.left
        anchors.leftMargin: root.label !== "" && !root.stacked ? Theme.s2 : 0
        anchors.right: root.stacked ? parent.right : readout.left
        anchors.rightMargin: root.stacked ? 0 : Theme.s2
        anchors.verticalCenter: root.stacked ? undefined : parent.verticalCenter
        anchors.bottom: root.stacked ? parent.bottom : undefined
        height: root.stacked ? Theme.hControl - Theme.s1 : parent.height
        leftPadding: root.stacked ? 0 : undefined; rightPadding: root.stacked ? 0 : undefined

        from: root.from
        to: root.to
        // Qt clamps value as each bound changes. Read the live bounds here
        // too, so a later range update reapplies the requested value instead
        // of leaving the handle at an earlier, temporarily clamped position.
        value: Math.max(Math.min(from, to), Math.min(Math.max(from, to), root.trackValue(root.shownValue)))
        stepSize: root.responsePower === 1 ? root.stepSize : 0
        // A step is a promise: a whole-number control (columns, quality)
        // never hands out 3.6 to be truncated, and edits land on the grid.
        snapMode: stepSize > 0 ? C.Slider.SnapAlways : C.Slider.NoSnap
        function snapped(v, fine) {
            const step = fine && root.fineStep > 0 ? root.fineStep : root.stepSize
            if (!(step > 0)) return v
            const n = Math.round((v - root.from) / step)
            return Math.max(Math.min(root.from, root.to), Math.min(Math.max(root.from, root.to), Number((root.from + n * step).toFixed(10))))
        }

        wheelEnabled: root.wheelEnabled
        // Tab reaches every slider in turn; the arrows then step it (see
        // keyStep). Handled here so Slider's own 0.1 steps never apply.
        activeFocusOnTab: true
        function arrow(key) { return key === Qt.Key_Right || key === Qt.Key_Up ? 1 : key === Qt.Key_Left || key === Qt.Key_Down ? -1 : 0 }
        // Left and Right are also the photo-stepping shortcuts; while a slider
        // has the keyboard they are its, as in any editor's inspector.
        Keys.onShortcutOverride: event => { if (slider.arrow(event.key) !== 0) event.accepted = true }
        Keys.onPressed: event => {
            const direction = slider.arrow(event.key)
            if (direction === 0) return
            event.accepted = true
            root.nudge(direction, event.modifiers)
        }
        onMoved: { const v = snapped(root.valueForTrack(value), false); root.ask(v); if (!pressed) root.finish(v) }
        onPressedChanged: if (!pressed) root.finish(snapped(root.valueForTrack(value), false))
        // Opt-in grading gestures share the first click and double-click in
        // one edit. Handling their pointer sequence here prevents Slider's
        // second press from overwriting the reset with its clicked position.
        MouseArea {
            id: trackPointer
            anchors.fill: parent
            enabled: root.resetOnDoubleClick
            // A click gives the slider the keyboard, as a bare Slider's would.
            onPressedChanged: if (pressed) slider.forceActiveFocus()
            preventStealing: true
            acceptedButtons: Qt.LeftButton
            property real originX: 0
            property real originPosition: 0
            property bool moved: false
            property bool doubleReset: false
            // What this press last asked for. `root.value` only follows once
            // the caller has read the edit back, which a quick drag outruns:
            // finishing with it would commit the value from before the drag.
            property real last: 0
            function update(x, fine) {
                const span = Math.max(1,slider.availableWidth-slider.handle.width)
                const position = Math.max(0,Math.min(1,(x-slider.leftPadding-slider.handle.width/2)/span))
                last = slider.snapped(root.valueForTrack(slider.from + position * (slider.to - slider.from)), fine)
                root.ask(last)
            }
            onPressed: mouse => {
                finishDelay.stop(); slider.forceActiveFocus()
                originX = mouse.x; moved = false; doubleReset = false
                const handleCentre = slider.leftPadding+slider.visualPosition*(slider.availableWidth-slider.handle.width)+slider.handle.width/2
                originPosition = Math.abs(mouse.x-handleCentre) < slider.handle.width ? handleCentre : mouse.x
                update(originPosition)
            }
            onPositionChanged: mouse => {
                if (!pressed) return
                if (Math.abs(mouse.x-originX)>Theme.dragThreshold) moved = true
                const fine = (mouse.modifiers & Qt.ShiftModifier) !== 0
                update(originPosition+(mouse.x-originX)*(fine?0.1:1), fine)
            }
            onReleased: {
                if (doubleReset) return
                if (moved) root.finish(last)
                else finishDelay.restart()
            }
            onDoubleClicked: { doubleReset = true; if (root.ownReset) { finishDelay.stop(); root.release(); root.resetRequested() } else root.resetValue() }
            onCanceled: { finishDelay.stop(); root.finish(last) }
            onWheel: event => {
                if (!root.wheelEnabled) { event.accepted = false; return }
                if (finishDelay.running) { finishDelay.stop(); root.finish(last) }
                const ticks = event.angleDelta.y ? event.angleDelta.y/120 : event.pixelDelta.y/30
                const fine = (event.modifiers & Qt.ShiftModifier) !== 0
                const value = slider.snapped(Math.max(root.from,Math.min(root.to,root.shownValue+ticks*root.fieldStep*(fine?0.1:1))), fine)
                root.ask(value); root.finish(value); event.accepted = true
            }
        }
        hoverEnabled: true

        Tooltip {
            text: root.tipTitle
            description: root.tip
            visible: slider.hovered && !root.pressed && root.tip !== ""
        }

        Accessible.role: Accessible.Slider
        Accessible.name: root.tipTitle

        background: Rectangle {
            x: slider.leftPadding
            y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: slider.availableWidth
            height: 3
            radius: 1
            color: Theme.inputBg

            // Fill runs from `origin` to the handle, so a bipolar control
            // reads its direction from the centre outwards.
            Rectangle {
                readonly property real originPos:
                    (root.trackValue(root.origin) - slider.from) / (slider.to - slider.from)
                x: Math.min(originPos, slider.visualPosition) * parent.width
                width: Math.abs(slider.visualPosition - originPos) * parent.width
                height: parent.height
                radius: parent.radius
                color: Theme.accent
            }
        }

        handle: Rectangle {
            readonly property bool engaged: root.enabled && (slider.hovered || trackPointer.containsMouse || root.pressed || slider.visualFocus)
            x: slider.leftPadding + slider.visualPosition
               * (slider.availableWidth - width)
            y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: engaged ? Theme.szSliderHandleActive : Theme.szSliderHandle
            height: width
            radius: width / 2
            color: (slider.pressed || trackPointer.pressed) ? Theme.accentPressed : engaged ? Theme.accent : Theme.textPrimary
            border.width: slider.visualFocus ? Theme.focusRing : 0
            border.color: Theme.accent

            Behavior on color {
                ColorAnimation { duration: Theme.dFast; easing.type: Theme.easing }
            }
        }
    }

    Item {
        id: offIndicator
        visible: root.stacked && root.notApplied && root.showOffIndicator
        anchors.right: parent.right
        anchors.top: parent.top; height: Theme.hRow
        width: Theme.szIconHit
        EffectDot { anchors.centerIn: parent }
    }
    function resetValue() {
        finishDelay.stop()
        root.ask(root.origin)
        root.finish(root.origin)
    }
    Timer {
        id: finishDelay
        interval: Qt.styleHints.mouseDoubleClickInterval + 20
        onTriggered: root.finish(trackPointer.last)
    }
    onVisibleChanged: if (!visible && finishDelay.running) { finishDelay.stop(); root.finish(trackPointer.last) }

    // ValueField updates its own value while typing/scrubbing. An explicit
    // Binding keeps later engine read-backs (including undo/reset) connected
    // after that local assignment, unlike an inline value binding.
    Binding { target: readout; property: "value"; value: root.shownValue }
    ValueField {
        id: readout
        anchors.right: parent.right
        anchors.verticalCenter: root.stacked ? undefined : parent.verticalCenter
        anchors.top: root.stacked ? parent.top : undefined
        height: root.stacked ? Theme.hRow : implicitHeight
        flat: root.stacked
        visible: root.showReadout && !root.notApplied
        width: visible ? implicitWidth : 0
        label: root.tipTitle
        from: root.fieldFrom
        to: root.fieldTo
        decimals: root.fieldDecimals
        adaptiveDecimals: root.adaptiveDecimals
        snap: root.fineStep
        suffix: root.suffix
        step: root.fieldStep
        wheelEnabled: root.wheelEnabled
        tip: root.tip
        onEdited: function (v) {
            root.ask(v)
            if (!scrubbing) root.finish(v)
        }
        onEditingFinished: v => root.finish(v)
        TapHandler {
            enabled: root.resetOnDoubleClick
            onDoubleTapped: if (root.ownReset) { root.release(); root.resetRequested() } else root.resetValue()
        }
    }
}
