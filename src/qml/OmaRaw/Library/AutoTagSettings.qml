import QtQuick
import QtQuick.Controls.Basic as C
import OmaRaw.Ui

C.Popup {
    id: root
    objectName: "autoTagSettings"
    parent: C.Overlay.overlay
    anchors.centerIn: parent
    modal: true; focus: true
    width: Math.min(480, parent ? parent.width - 24 : 480)
    height: Math.min(individual ? 660 : 490, parent ? parent.height - 32 : 660)
    padding: Theme.s4
    property bool individual: false
    property string query: ""
    background: Rectangle { color: Theme.panelRaised; radius: Theme.rMenu; border.width: Theme.hairline; border.color: Theme.borderStrong }
    contentItem: Item {
        Column {
            id: heading
            width: parent.width; spacing: Theme.s2
            Text { text: qsTr("Automatic tagging"); font.family: Theme.fontFamily; font.pixelSize: Theme.fsHeading; color: Theme.textPrimary }
            Toggle {
                objectName: "autoTagSettingsMaster"
                width: parent.width; label: qsTr("Automatically tag imported photos")
                checked: backend.autoTagEnabled; checkable: false
                tip: qsTr("Scan new imports offline on the CPU. Turning this off stops current and future scans; existing tags are kept.")
                onClicked: backend.autoTagEnabled = !backend.autoTagEnabled
            }
            Text {
                width: parent.width; wrapMode: Text.WordWrap; textFormat: Text.PlainText
                text: backend.autoTagEnabled ? qsTr("New imports are scanned in the background. Use Scan selected for existing photos.")
                    : qsTr("Scanning is off. Existing tags and your corrections are kept. Turn it on to resume queued scans.")
                font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textSecondary
            }
            Text { text: qsTr("Automatic collections for this catalog"); font.family: Theme.fontFamily; font.pixelSize: Theme.fsControl; color: Theme.textPrimary }
            Text {
                width: parent.width; wrapMode: Text.WordWrap
                text: qsTr("Choose which collections appear. They are created when matching photos exist. Unticking hides a collection and keeps its name and tags.")
                font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
            }
        }
        C.ScrollView {
            id: scroll
            anchors.top: heading.bottom; anchors.topMargin: Theme.s2
            anchors.left: parent.left; anchors.right: parent.right
            anchors.bottom: footer.top; anchors.bottomMargin: Theme.s2
            clip: true; contentWidth: availableWidth
            C.ScrollBar.vertical: ScrollBar {}
            C.ScrollBar.horizontal.policy: C.ScrollBar.AlwaysOff
            Column {
                width: scroll.availableWidth; spacing: Theme.s1
                Repeater {
                    model: backend.autoTagCollections.filter(row => row.group)
                    CheckField {
                        required property var modelData
                        objectName: "autoTagCollection_" + modelData.key
                        width: parent.width; text: modelData.label; checked: modelData.enabled; checkable: false
                        onClicked: backend.setAutoTagCollectionEnabled(modelData.key, !modelData.enabled)
                    }
                }
                ToolButton { objectName: "autoTagIndividualCollections"; text: root.individual ? qsTr("Hide individual collections") : qsTr("Choose individual collections…"); showLabel: true; onClicked: root.individual = !root.individual }
                SearchField { width: parent.width; visible: root.individual; placeholder: qsTr("Find a collection…"); live: true; text: root.query; onTextChanged: root.query = text }
                Repeater {
                    model: root.individual ? backend.autoTagCollections.filter(row => !row.group && row.label.toLowerCase().indexOf(root.query.toLowerCase()) >= 0) : []
                    CheckField {
                        required property var modelData
                        objectName: "autoTagCollection_" + modelData.key
                        width: parent.width; text: modelData.label; checked: modelData.enabled; checkable: false
                        onClicked: backend.setAutoTagCollectionEnabled(modelData.key, !modelData.enabled)
                    }
                }
            }
        }
        Row {
            id: footer; anchors.right: parent.right; anchors.bottom: parent.bottom
            ToolButton { objectName: "autoTagSettingsDone"; text: qsTr("Done"); showLabel: true; onClicked: root.close() }
        }
    }
}
