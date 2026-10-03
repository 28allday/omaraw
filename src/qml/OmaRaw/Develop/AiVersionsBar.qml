import QtQuick
import OmaRaw.Ui

Rectangle {
    id: root
    objectName: "aiVersionsBar"
    property var versions: backend.aiVersions
    readonly property int currentIndex: versions.findIndex(v => v.id === backend.currentId)
    readonly property int originalId: { const original = versions.find(v => v.operation === "original"); return original ? original.id : 0 }
    readonly property bool ready: !engine.busy && !engine.ai.busy && !engine.denoise.busy
    implicitHeight: choices.implicitHeight + Theme.s2 * 2
    color: Theme.panelRaised
    Flow {
        id: choices
        x: Theme.s3; y: Theme.s2; width: parent.width - Theme.s3 * 2; spacing: Theme.s2
        Text {
            height: Theme.hRow; verticalAlignment: Text.AlignVCenter
            text: qsTr("Photo versions")
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textSecondary
        }
        ComboField {
            objectName: "aiVersionPicker"
            width: Math.min(260, choices.width)
            model: root.versions.map(v => v.label)
            currentIndex: root.currentIndex
            enabled: root.ready
            onActivated: i => backend.openAiVersion(root.versions[i].id)
        }
        ToolButton {
            objectName: "openAiOriginal"; text: qsTr("Open original"); showLabel: true
            enabled: root.ready && root.originalId > 0 && backend.currentId !== root.originalId
            tip: qsTr("Returns to the source photo with its earlier edits. Your AI results are kept.")
            onClicked: backend.openAiOriginal()
        }
        ToolButton {
            objectName: "openLatestAiVersion"; text: qsTr("Open latest AI version"); showLabel: true
            enabled: root.ready && root.currentIndex !== root.versions.length - 1
            tip: qsTr("Continues editing the newest AI result. Every version keeps its own edits.")
            onClicked: backend.openLatestAiVersion()
        }
    }
}
