import QtQuick
import QtQuick.Controls.Basic as C
import OmaRaw.Ui

// One engine parameter: its name and figure over a full-width slider. A drag's
// first sample goes to the engine at once and later ones as fast as it can
// render them (EditCoalescer); release always lands. Double-clicking the
// slider, or the arrow that appears beside
// a changed figure, puts it back. A bool field is a tick box, an enum is a
// dropdown of the module's choices, and a "file" row is a chooser for a path.
Item {
    id: root
    objectName: "adjustmentRow_" + (param.op || "") + "_" + (param.field || "")
    property var param: ({})
    // Replaces the control's own name (a lone "Amount" becomes its adjustment's name).
    property string caption: ""
    // Room kept free at the right of the caption line, and where it ends.
    property real trailingReserve: 0
    property bool showOffIndicator: true
    readonly property real trailingX: field.x + field.trailingX
    readonly property bool hovered: hh.hovered
    width: parent ? parent.width : 300
    readonly property bool isSlider: !isSwitch && !isFile && !isEnum
    height: (isSlider ? field.implicitHeight : isEnum || isFile ? Theme.hRow + Theme.hControl : Theme.hControl) + Theme.s1
    readonly property bool moduleOff: !(param.enabled || false)
    readonly property real factor: param.factor || 1
    readonly property real offset: param.offset || 0
    // A unitless control's figure (see curatedParams): "strength" reads 0 at
    // the default and -100..+100 at the ends, "amount" 0..100. The slider
    // moves in those figures; the engine is sent its own values.
    // An unbounded field (darktable declares none) keeps its own figures.
    readonly property string display: Number.isFinite(hi - lo) && hi - lo < 1e6 ? param.display || "" : ""
    readonly property real lo: param.min !== undefined ? param.min : -1
    readonly property real hi: param.max !== undefined ? param.max : 1
    readonly property real dflt: param.reset !== undefined ? param.reset : param.def !== undefined ? param.def : 0
    function shown(v) {
        if (display === "amount") return hi > lo ? (v - lo) / (hi - lo) * 100 : 0
        if (display === "strength") return v >= dflt ? (hi > dflt ? (v - dflt) / (hi - dflt) * 100 : 0)
                                                      : (dflt > lo ? (v - dflt) / (dflt - lo) * 100 : 0)
        return v * factor + offset
    }
    function engineValue(s) {
        if (display === "amount") return lo + s / 100 * (hi - lo)
        if (display === "strength") return s >= 0 ? dflt + s / 100 * (hi - dflt) : dflt + s / 100 * (dflt - lo)
        return (s - offset) / factor
    }
    // The engine-facing label, in the words a photographer expects.
    readonly property string label: caption !== "" ? caption : Names.control(param.op, param.field, param.label || "")
    readonly property string tip: [Names.describe(param.op, param.field),
                                   root.moduleOff ? qsTr("This adjustment is hidden; moving the slider shows it again.") : "",
                                   root.isSlider ? qsTr("Double-click resets it.") : qsTr("Right-click resets it.")].filter(t => t !== "").join(" ")
    readonly property bool isSwitch: param.type === 2
    readonly property bool isFile: param.kind === "file"
    readonly property bool isEnum: param.type === 3 && !isFile
    readonly property var options: param.options || []
    readonly property int optionIndex: {
        const v = Math.round(root.param.value || 0)
        for (let i = 0; i < root.options.length; ++i) if (root.options[i].value === v) return i
        return -1
    }
    readonly property string fileName: {
        const t = root.param.text || ""
        return t === "" ? "" : t.substring(t.lastIndexOf("/") + 1)
    }

    // Only a file row has a dialog, made when the row is one.
    Loader {
        id: fileDialog
        active: root.isFile
        function open() { if (item) item.open() }
        sourceComponent: FilePicker {
            readonly property bool cameraProfile: root.param.op === "omarawprint" && root.param.field === "profile"
            title: cameraProfile ? qsTr("Choose camera profile tables") : qsTr("Choose a LUT (.cube, .png, .3dl, .gmz)")
            nameFilters: cameraProfile ? [qsTr("Profile tables (*.ompt *.OMPT)"), qsTr("All files (*)")]
                                       : [qsTr("LUT files (*.cube *.CUBE *.png *.PNG *.3dl *.3DL *.gmz *.GMZ)"), qsTr("All files (*)")]
            onAccepted: {
                let path = selectedFile.toString()
                if (path.startsWith("file://")) path = path.substring(7)
                engine.setParamString(root.param.op, root.param.field, decodeURIComponent(path))
            }
        }
    }
    CheckField {
        visible: root.isSwitch
        anchors.left: parent.left; anchors.right: parent.right
        anchors.leftMargin: Theme.s3; anchors.rightMargin: Theme.s3
        anchors.verticalCenter: parent.verticalCenter
        checked: (root.param.value || 0) > 0.5
        text: root.label
        tip: root.tip
        opacity: root.moduleOff ? 0.7 : 1
        // The engine's read-back is the truth; never keep a local tick.
        onClicked: { const on = (root.param.value || 0) > 0.5; checked = Qt.binding(() => (root.param.value || 0) > 0.5); engine.setParam(root.param.op, root.param.field, on ? 0 : 1) }
    }
    Item {
        visible: root.isFile
        anchors.fill: parent
        Text {
            objectName: "fileCaption"
            anchors.left: parent.left; anchors.leftMargin: Theme.s3
            anchors.right: parent.right; anchors.rightMargin: Theme.s3
            anchors.top: parent.top; height: Theme.hRow
            verticalAlignment: Text.AlignVCenter
            text: root.label
            textFormat: Text.PlainText; elide: Text.ElideRight
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsControl; color: Theme.textSecondary
        }
        Item {
            anchors.left: parent.left; anchors.leftMargin: Theme.s3
            anchors.right: parent.right; anchors.rightMargin: Theme.s3
            anchors.top: parent.top; anchors.topMargin: Theme.hRow
            height: Theme.hControl
            C.AbstractButton {
                id: fileButton
                objectName: "fileChoose"
                anchors.left: parent.left; anchors.right: clearFile.left
                anchors.rightMargin: clearFile.visible ? Theme.s1 : 0
                height: parent.height
                text: root.fileName === "" ? qsTr("Choose…") : root.fileName
                hoverEnabled: true; focusPolicy: Qt.TabFocus
                Accessible.name: qsTr("Choose %1").arg(root.label)
                Accessible.description: root.param.text || ""
                background: Rectangle {
                    color: fileButton.down ? Theme.pressedOn(Theme.controlBg) : fileButton.hovered ? Theme.hovered(Theme.controlBg) : Theme.controlBg
                    radius: Theme.rControl
                    border.width: fileButton.visualFocus ? Theme.focusRing : Theme.hairline
                    border.color: fileButton.visualFocus ? Theme.accent : Theme.border
                }
                contentItem: Item {
                    Icon { id: folderIcon; anchors.left: parent.left; anchors.leftMargin: Theme.s2; anchors.verticalCenter: parent.verticalCenter; name: "folder-open"; size: Theme.szIcon; color: Theme.textSecondary }
                    Text {
                        objectName: "fileName"
                        anchors.left: folderIcon.right; anchors.leftMargin: Theme.s1
                        anchors.right: parent.right; anchors.rightMargin: Theme.s2
                        anchors.verticalCenter: parent.verticalCenter
                        text: fileButton.text; textFormat: Text.PlainText; elide: Text.ElideMiddle
                        font.family: Theme.fontFamily; font.pixelSize: Theme.fsControl; color: Theme.textPrimary
                    }
                }
                Tooltip { text: root.label; description: root.param.text || Names.describe(root.param.op, root.param.field); visible: fileButton.hovered && !fileButton.down }
                onClicked: fileDialog.open()
            }
            IconButton {
                id: clearFile
                objectName: "fileClear"
                anchors.right: parent.right
                width: visible ? Theme.szIconHit : 0; height: parent.height
                visible: root.fileName !== ""; iconName: "x"; text: qsTr("Clear %1").arg(root.label)
                onClicked: engine.setParamString(root.param.op, root.param.field, "")
            }
        }
    }

    Item {
        visible: root.isEnum
        anchors.fill: parent
        // Its name above it, like the sliders: the choices say what they do
        // and need the width to say it.
        Text {
            anchors.left: parent.left; anchors.leftMargin: Theme.s3
            anchors.right: parent.right; anchors.rightMargin: Theme.s3
            anchors.top: parent.top; height: Theme.hRow
            verticalAlignment: Text.AlignVCenter
            text: root.label
            elide: Text.ElideRight
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsControl; color: Theme.textSecondary
            opacity: root.moduleOff ? 0.6 : 1
        }
        ComboField {
            id: combo
            readonly property bool stockPrompt: root.param.op === "omarawprint" && root.param.field === "stock" && root.moduleOff
            anchors.left: parent.left; anchors.leftMargin: Theme.s3
            anchors.right: parent.right; anchors.rightMargin: Theme.s3
            anchors.top: parent.top; anchors.topMargin: Theme.hRow
            model: root.options.map(o => Names.choice(o.label))
            tipTitle: root.label
            tip: root.tip
            currentIndex: stockPrompt ? -1 : root.optionIndex
            displayText: stockPrompt ? qsTr("Select stock…") : currentText
            opacity: root.moduleOff ? 0.7 : 1
            onActivated: index => {
                if (index >= 0 && index < root.options.length)
                    engine.setParam(root.param.op, root.param.field, root.options[index].value)
            }
            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: engine.resetParam(root.param.op, root.param.field)
            }
        }
    }

    EditCoalescer {
        id: throttle
        onSend: v => engine.setParam(root.param.op, root.param.field, v)
    }
    SliderField {
        id: field
        visible: root.isSlider
        stacked: true
        captionReserve: root.trailingReserve
        notApplied: root.moduleOff
        showOffIndicator: root.showOffIndicator
        resetOnDoubleClick: true
        ownReset: true
        onResetRequested: { throttle.drop(); engine.resetParam(root.param.op, root.param.field) }
        anchors.left: parent.left; anchors.right: parent.right
        anchors.leftMargin: Theme.s3; anchors.rightMargin: Theme.s3
        anchors.verticalCenter: parent.verticalCenter
        label: root.label
        tip: root.tip
        labelWidth: 96
        from: root.shown(root.lo)
        to: root.shown(root.hi)
        origin: root.shown(root.dflt)
        // Whole figures on a drag, tenths with Shift, the Alt-arrows or a
        // typed figure: one figure on a lopsided range (Contrast's upper
        // side, Blacks) is too coarse a step to be the finest there is.
        stepSize: root.display !== "" ? 1 : 0
        fineStep: root.display !== "" ? 0.1 : 0
        decimals: root.display !== "" ? 1 : root.param.decimals !== undefined ? root.param.decimals : 2
        adaptiveDecimals: root.display !== ""
        suffix: root.display !== "" ? "" : root.param.suffix || ""
        value: root.shown(root.moduleOff && root.param.idle !== undefined ? root.param.idle
                         : root.param.value !== undefined ? root.param.value : root.dflt)
        opacity: root.moduleOff ? 0.6 : 1
        onEdited: v => throttle.push(root.engineValue(v))
        onEditingFinished: v => throttle.flush(root.engineValue(v))
        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: engine.resetParam(root.param.op, root.param.field)
        }
    }
    HoverHandler { id: hh }
}
