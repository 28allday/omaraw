import QtQuick
import QtQuick.Controls.Basic as C
import OmaRaw.Ui

Column {
    id: root
    required property string zone
    required property string title
    property bool compact: false
    property var params: []
    property real chromaMaximum: zone === "lift" || zone === "offset" ? 0.25 : 1
    property real lumaDefault: zone === "gamma" || zone === "gain" ? 1 : 0
    property real lumaFrom: zone === "gamma" || zone === "gain" ? .5 : -lumaRange
    property real lumaTo: zone === "gamma" || zone === "gain" ? 1.5 : lumaRange
    property real lumaMinimum: zone === "gamma" ? .25 : zone === "gain" ? 0 : zone === "lift" ? -.5 : -1
    property real lumaMaximum: zone === "gamma" || zone === "gain" ? 4 : zone === "lift" ? .5 : 1
    readonly property string levelLabel: qsTr("Level")
    readonly property string levelTip: zone === "lift" ? qsTr("Set the black level while keeping white fixed. Zero is neutral.")
        : zone === "gamma" ? qsTr("Shape the midtones while keeping black and white fixed. One is neutral.")
        : zone === "gain" ? qsTr("Scale brightness towards the white end while keeping black fixed. One is neutral.")
        : qsTr("Shift brightness across the whole picture. Zero is neutral.")
    property real chromaRange: zone === "lift" ? .12 : zone === "offset" ? .08 : .25
    property real lumaRange: zone === "lift" ? .25 : .1
    property real hue: 0
    property real chroma: 0
    property real luminance: 0
    property bool editing: false
    property bool started: false
    property bool pending: false
    property int editImage: -1
    signal enlargeRequested()
    objectName: "colourBalance_" + zone
    spacing: Theme.s1
    enabled: engine.imageId >= 0 && params.length > 0
    function read() {
        if (editing) return
        for (const p of params) {
            if (p.field === zone + "_H") hue = p.value
            if (p.field === zone + "_C") chroma = p.value
            if (p.field === zone + "_Y") luminance = p.value
        }
    }
    onParamsChanged: read()
    Component.onCompleted: read()
    function change(h, c, y) {
        if (!editing) {
            editing = true; started = false; editImage = engine.imageId
            if (typeof engine.setPreviewEditing === "function") engine.setPreviewEditing(root, true)
        }
        if (editImage !== engine.imageId) return
        hue = h; chroma = c; luminance = y; pending = true
        if (!started && !engine.busy) send()
        else if (!throttle.running) throttle.start()
    }
    function send() {
        if (!pending || editImage !== engine.imageId) return
        pending = false
        engine.setPrimaryGrade(zone, hue, chroma, luminance, !started)
        started = true
    }
    function finish() {
        throttle.stop(); send()
        if (typeof engine.setPreviewEditing === "function") engine.setPreviewEditing(root, false)
        editing = false; started = false; read()
    }
    function discardEdit() {
        throttle.stop(); pending = false
        if (editing && typeof engine.setPreviewEditing === "function") engine.setPreviewEditing(root, false)
        editing = false; started = false; editImage = -1; read()
    }
    onVisibleChanged: if (!visible && editing) finish()
    Component.onDestruction: if (editing && typeof engine.setPreviewEditing === "function") engine.setPreviewEditing(root, false)
    function reset() { change(0, 0, lumaDefault); finish() }
    Timer {
        id: throttle
        interval: 50
        onTriggered: {
            // Coalesce the latest puck position while a preview is running.
            // Release still sends the final value immediately.
            if (engine.busy) restart()
            else root.send()
        }
    }
    Connections {
        target: engine
        function onImageChanged() { root.discardEdit() }
        function onHistoryJumped() { root.discardEdit() }
        function onEditStateReplaced() { root.discardEdit() }
        function onAutoExposureRequested() { if (root.editing) root.finish() }
    }
    Item {
        width: parent.width; height: Theme.hControl
        C.AbstractButton {
            objectName: "enlarge_" + root.zone
            anchors.left: parent.left; anchors.right: resetButton.left; height: parent.height
            enabled: root.compact
            onClicked: root.enlargeRequested()
            contentItem: Text { text: root.title; color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; font.weight: Theme.wHeading; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
            Accessible.name: qsTr("Enlarge %1 wheel").arg(root.title)
        }
        IconButton { id: resetButton; objectName: "reset_" + root.zone; anchors.right: parent.right; iconName: "rotate-ccw"; text: qsTr("Reset %1").arg(root.title); tip: qsTr("Clears this range's tint and brightness."); onClicked: root.reset() }
    }
    ColourWheel {
        objectName: "wheel_" + root.zone
        anchors.horizontalCenter: parent.horizontalCenter
        width: root.compact ? Math.min(root.width, 128) : Math.min(root.width, 208); height: width
        primary: true
        label: root.title; hue: root.hue; amount: root.chroma / root.chromaRange
        onEdited: (h, a) => root.change(h, a * root.chromaRange, root.luminance)
        onEditingFinished: root.finish()
    }
    Text {
        visible: root.compact
        text: root.levelLabel
        color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
    }
    SliderField {
        id: compactLuma
        objectName: "brightness_" + root.zone
        visible: root.compact
        width: parent.width
        label: ""; tipTitle: root.title + " " + root.levelLabel
        from: Math.min(root.lumaFrom, root.luminance); to: Math.max(root.lumaTo, root.luminance)
        fieldFrom: root.lumaMinimum; fieldTo: root.lumaMaximum; origin: root.lumaDefault; decimals: 3; value: root.luminance
        resetOnDoubleClick: true; wheelEnabled: true
        tip: root.levelTip + qsTr(" Double-click resets the level only.")
        onEdited: v => { root.change(root.hue, root.chroma, v); if (!pressed) root.finish() }
        onEditingFinished: root.finish()
    }
    Column {
        visible: root.compact; width: parent.width; spacing: Theme.s1
        Repeater {
            model: ["Hue", "Chroma"]
            Item {
                required property int index
                required property string modelData
                width: parent.width; height: Theme.hControl
                Text {
                    anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                    text: parent.index === 0 ? qsTr("Hue") : qsTr("Chroma")
                    color: Theme.textSecondary; font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
                }
                Binding { target: number; property: "value"; value: number.hueField ? root.hue : root.chroma*100 }
                ValueField {
                    id: number
                    readonly property bool hueField: parent.index === 0
                    anchors.right: parent.right
                    label: root.title + " " + (hueField ? qsTr("hue") : qsTr("chroma"))
                    from: 0; to: hueField ? 360 : root.chromaMaximum*100
                    decimals: hueField ? 1 : 2; suffix: hueField ? "°" : "%"
                    step: hueField ? 1 : root.chromaRange/2
                    wheelEnabled: true
                    onEdited: v => { root.change(hueField ? v : root.hue, hueField ? root.chroma : v/100, root.luminance); if (!scrubbing) root.finish() }
                    onEditingFinished: root.finish()
                }
            }
        }
    }
    Column {
        visible: !root.compact; width: parent.width; spacing: Theme.s1
        SliderField {
            id: hueSlider
            objectName: "hue_" + root.zone
            width: parent.width; label: qsTr("Hue"); labelWidth: 72; from: 0; to: 360; decimals: 1; suffix: "°"; value: root.hue
            resetOnDoubleClick: true; wheelEnabled: true
            onEdited: v => { root.change(v, root.chroma, root.luminance); if (!pressed) root.finish() }
            onEditingFinished: root.finish()
        }
        SliderField {
            id: tintSlider
            objectName: "tint_" + root.zone
            width: parent.width; label: qsTr("Chroma"); labelWidth: 72; from: 0; to: Math.max(root.chromaRange, root.chroma)*100
            fieldTo: root.chromaMaximum*100; decimals: 2; suffix: "%"; value: root.chroma*100
            resetOnDoubleClick: true; wheelEnabled: true
            tip: qsTr("Strength of the added colour. Zero is neutral. The wheel uses a gentle range; type a larger value if needed.")
            onEdited: v => { root.change(root.hue, v/100, root.luminance); if (!pressed) root.finish() }
            onEditingFinished: root.finish()
        }
        SliderField {
            id: lumaSlider
            objectName: "luma_" + root.zone
            width: parent.width; label: root.levelLabel; labelWidth: 72
            from: Math.min(root.lumaFrom, root.luminance); to: Math.max(root.lumaTo, root.luminance)
            fieldFrom: root.lumaMinimum; fieldTo: root.lumaMaximum; origin: root.lumaDefault; decimals: 3; value: root.luminance
            resetOnDoubleClick: true; wheelEnabled: true
            tip: root.levelTip + qsTr(" Double-click resets the level only.")
            onEdited: v => { root.change(root.hue, root.chroma, v); if (!pressed) root.finish() }
            onEditingFinished: root.finish()
        }
    }
}
