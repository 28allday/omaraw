pragma ComponentBehavior: Bound
import QtQuick
import OmaRaw.Ui

Item {
    id: root
    required property string operation
    property string title: operation
    property bool saveEnabled: true
    property var resetAction: () => engine.resetModule(root.operation)
    signal savePresetRequested(string operation, string label)
    objectName: "moduleActions_" + operation
    width: Theme.hRow; height: Theme.hRow
    enabled: engine.imageId >= 0 && !engine.busy
    function open() { menu.popup(root, 0, root.height) }
    IconButton {
        anchors.centerIn: parent
        iconName: "ellipsis"; text: qsTr("%1 actions").arg(root.title)
        tip: qsTr("Reset this adjustment, copy or paste its settings between photos, or save them as a preset.")
        onClicked: root.open()
    }
    ContextMenu {
        id: menu
        implicitWidth: 260
        MenuAction { objectName: "copyModule_" + root.operation; text: qsTr("Copy adjustment settings"); iconName: "copy"; onTriggered: engine.copyModuleSettings(root.operation) }
        MenuAction { objectName: "pasteModule_" + root.operation; text: qsTr("Paste adjustment settings"); iconName: "copy"; enabled: engine.clipboardModules.indexOf(root.operation) >= 0; onTriggered: engine.pasteModuleSettings(root.operation) }
        MenuAction {
            objectName: "resetModule_" + root.operation; text: qsTr("Reset this adjustment"); iconName: "rotate-ccw"
            enabled: engine.imageId >= 0 && !engine.busy
            onTriggered: root.resetAction()
        }
        MenuAction { objectName: "saveModule_" + root.operation; text: qsTr("Save adjustment preset…"); iconName: "bookmark"; enabled: root.saveEnabled; onTriggered: root.savePresetRequested(root.operation, root.title) }
    }
}
