pragma ComponentBehavior: Bound
import QtQuick
import OmaRaw.Ui

// The line above one adjustment's controls, the same wherever an adjustment
// appears: its name, and at the right the menu (reset, copy, save) and the
// dot that shows or hides it. The action menu waits for the pointer;
// the effect's state remains visible.
// A collapsible heading also folds its controls away, for the larger tools.
Item {
    id: root
    property string text: ""
    property string tip: ""
    // The engine adjustment behind it; "" leaves out the menu and the eye.
    property string operation: ""
    property bool on: true
    property bool showStatus: operation !== ""
    property bool eyeEnabled: true
    property string eyeName: "moduleEye_" + operation
    property bool saveEnabled: true
    property bool showReset: operation !== ""
    property string resetName: "resetAdjustment_" + operation
    property string resetTip: qsTr("Restores this adjustment's starting state. Other tools keep their edits. Undo brings this effect back.")
    property var resetAction: () => engine.resetModule(root.operation)
    property bool collapsible: false
    property bool open: true
    // Extra buttons, left of the menu; shown with it.
    default property alias extra: extraSlot.data
    readonly property bool hovered: lineHover.hovered
    signal toggled()
    signal eyeClicked()
    signal savePresetRequested(string operation, string label)

    width: parent ? parent.width : 300
    height: Theme.hRow + Theme.s1
    HoverHandler { id: lineHover }
    // A folding heading is a button to the keyboard and to screen readers:
    // Tab reaches it, Space or Return opens and closes it.
    activeFocusOnTab: collapsible
    Accessible.role: collapsible ? Accessible.Button : Accessible.StaticText
    Accessible.name: text
    Accessible.description: (collapsible ? (open ? qsTr("Open. ") : qsTr("Closed. ")) : "") + tip
    Accessible.onPressAction: if (collapsible) root.toggled()
    Keys.onPressed: event => {
        if (!root.collapsible) return
        if (event.key === Qt.Key_Space || event.key === Qt.Key_Return || event.key === Qt.Key_Enter) { event.accepted = true; root.toggled() }
    }
    Rectangle {
        anchors.fill: parent
        color: Theme.hovered(Theme.panelBg)
        visible: root.enabled && root.collapsible && lineHover.hovered
    }
    Rectangle {
        anchors.fill: parent; anchors.margins: 1
        visible: root.activeFocus
        color: "transparent"; radius: Theme.rControl
        border.width: Theme.focusRing; border.color: Theme.accent
    }
    // Whether keyboard focus is on one of the heading's own buttons.
    function holdsFocus(item) {
        for (let i = Window.activeFocusItem; i; i = i.parent) if (i === item) return true
        return false
    }
    Icon {
        id: chevron
        visible: root.collapsible
        anchors.left: parent.left; anchors.leftMargin: Theme.s3 - 3
        anchors.verticalCenter: label.verticalCenter
        name: root.open ? "chevron-down" : "chevron-right"; size: 12
        color: lineHover.hovered ? Theme.textPrimary : Theme.textMuted
    }
    Text {
        id: label
        anchors.left: parent.left; anchors.leftMargin: root.collapsible ? Theme.s3 + 12 : Theme.s3
        anchors.right: controls.left; anchors.rightMargin: Theme.s2
        anchors.bottom: parent.bottom; height: Theme.hRow
        verticalAlignment: Text.AlignVCenter
        text: root.text
        font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; font.weight: Theme.wHeading
        font.capitalization: Font.AllUppercase; font.letterSpacing: 0.6
        color: !root.on ? Theme.textMuted : root.collapsible && lineHover.hovered ? Theme.textPrimary : Theme.textSecondary
        elide: Text.ElideRight
        HoverHandler { id: labelHover }
        Tooltip { text: root.text; description: [root.tip, root.operation !== "" ? qsTr("Alt-click the name to reset the adjustment.") : "", root.operation !== "" && !root.on ? qsTr("Not applied to the picture. Move one of its controls, or click the status dot, to bring it in.") : ""].filter(t => t !== "").join(" "); visible: labelHover.hovered && description !== "" }
    }
    // Under the buttons, so they keep their own clicks.
    TapHandler { enabled: root.collapsible; acceptedModifiers: Qt.NoModifier; onTapped: root.toggled() }
    // Alt-click on the name puts the whole adjustment back, as the ⋯ menu's
    // Reset does; the habit comes with people from other editors.
    TapHandler {
        enabled: root.operation !== ""
        acceptedModifiers: Qt.AltModifier
        onTapped: root.resetAction()
    }
    Row {
        id: controls
        anchors.right: parent.right; anchors.rightMargin: Theme.s3 - 2
        anchors.bottom: parent.bottom
        spacing: 0
        IconButton {
            objectName: root.resetName
            visible: root.showReset
            iconName: "rotate-ccw"; text: qsTr("Reset %1").arg(root.text)
            tip: root.resetTip
            // Reset is queued safely behind an in-flight edit, like the
            // sliders. Render activity must not pulse every heading's icon.
            enabled: engine.imageId >= 0
            onClicked: root.resetAction()
        }
        Row { id: extraSlot; spacing: 0; opacity: lineHover.hovered || root.holdsFocus(extraSlot) ? 1 : 0; anchors.verticalCenter: parent.verticalCenter }
        ModuleActions {
            visible: root.operation !== ""
            opacity: lineHover.hovered || activeFocus ? 1 : 0
            operation: root.operation; title: root.text
            resetAction: root.resetAction
            saveEnabled: root.saveEnabled
            onSavePresetRequested: (operation, label) => root.savePresetRequested(operation, label)
        }
        EyeToggle {
            objectName: root.eyeName
            visible: root.operation !== ""
            enabled: root.eyeEnabled
            prominent: true
            opacity: !enabled ? Theme.disabledOpacity : 1
            checked: root.on; text: root.text
            onClicked: root.eyeClicked()
        }
        Item {
            visible: root.showStatus && root.operation === ""
            width: Theme.szIconHit; height: Theme.szIconHit
            EffectDot { anchors.centerIn: parent; applied: root.on }
            HoverHandler { id: statusHover }
            Tooltip { text: root.text; description: root.on ? qsTr("Contains applied adjustments. Each adjustment has its own switch.") : qsTr("No adjustments in this tool are applied."); visible: statusHover.hovered }
        }
    }
}
