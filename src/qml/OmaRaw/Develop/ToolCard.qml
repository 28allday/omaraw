pragma ComponentBehavior: Bound
import QtQuick
import OmaRaw.Ui

// Folding is presentation only. Editors stay alive while hidden so queued
// gestures finish normally and switching tools never writes photo settings.
Column {
    id: root
    required property var controller
    required property string key
    required property string tab
    required property string title
    property string tip: ""
    property string operation: ""
    property var toggleEnabled: null
    property bool available: true
    property bool specialist: false
    property bool changed: operation !== "" && controller.moduleChanged(operation)
    property bool moduleOn: operation === "" || (engine.paramsVersion, engine.moduleEnabled(operation))
    property bool showStatus: operation !== ""
    property bool contributes: moduleOn && changed
    property var resetAction: () => engine.resetTool(root.key)
    property string resetTip: qsTr("Restores this tool's starting state. Other tools keep their edits. Undo brings this effect back.")
    readonly property bool open: controller.isToolOpen(key)
    default property alias toolContent: body.sectionContent
    objectName: "toolCard_" + key
    width: parent ? parent.width : 300
    visible: available && controller.group === tab && controller.toolVisible(key, specialist, contributes)
    height: visible ? implicitHeight : 0
    onOpenChanged: if (open && visible) controller.revealTool(root)
    onContributesChanged: controller.retainTool(root)
    onAvailableChanged: controller.retainTool(root)
    Component.onCompleted: controller.registerTool(root)
    Component.onDestruction: controller.unregisterTool(root)
    data: [
        BlockHeading {
            objectName: "toolHeading_" + root.key
            text: root.title + (root.changed ? "  ·" : "")
            tip: root.tip + (root.changed ? " " + qsTr("This tool has edited settings.") : "")
            operation: root.operation; on: root.moduleOn
            showStatus: root.showStatus
            showReset: true; resetName: "resetTool_" + root.key
            resetAction: root.resetAction; resetTip: root.resetTip
            collapsible: true; open: root.open
            saveEnabled: root.controller.develop !== null
            onToggled: root.controller.toggleTool(root.key, root.tab)
            onEyeClicked: if (root.toggleEnabled) root.toggleEnabled(); else engine.setModuleEnabled(root.operation, !root.moduleOn)
            onSavePresetRequested: (operation, label) => root.controller.saveModulePreset(operation, label)
        },
        SlideSection {
            id: body
            width: root.width
            expanded: root.open
            spacing: Theme.s1
            bottomPadding: Theme.s3
        }
    ]
}
