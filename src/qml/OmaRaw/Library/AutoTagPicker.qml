import QtQuick
import QtQuick.Controls.Basic as C
import OmaRaw.Ui

C.Popup {
    id: root
    objectName: "autoTagPicker"
    parent: C.Overlay.overlay
    anchors.centerIn: parent
    modal: true; focus: true
    width: Math.min(440, parent ? parent.width - 24 : 440)
    height: Math.min(600, parent ? parent.height - 32 : 600)
    padding: Theme.s4
    property string query: ""
    property string group: ""
    property var tags: []
    function refresh() { tags = backend.info(backend.currentId).autoTags || [] }
    function tag(key) { return tags.find(t => t.tag === key) || ({ active: false, decision: 0 }) }
    onOpened: { query = ""; group = ""; refresh(); search.focusInput() }
    Connections {
        target: backend
        function onAutoTagsChanged() { if (root.visible) root.refresh() }
        function onSelectionChanged() { if (root.visible) root.refresh() }
    }
    background: Rectangle { color: Theme.panelRaised; radius: Theme.rMenu; border.width: Theme.hairline; border.color: Theme.borderStrong }
    contentItem: Item {
        Column {
            id: heading; width: parent.width; spacing: Theme.s2
            Text { text: qsTr("Correct subject tags"); font.family: Theme.fontFamily; font.pixelSize: Theme.fsHeading; color: Theme.textPrimary }
            Text {
                width: parent.width; wrapMode: Text.WordWrap
                text: qsTr("Tick a missed subject or untick a wrong one. Your corrections are kept when rescanning.")
                font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textSecondary
            }
            SearchField { id: search; objectName: "autoTagSearch"; width: parent.width; live: true; placeholder: qsTr("Find a subject…"); text: root.query; onTextChanged: root.query = text }
            ComboField {
                width: parent.width
                property var keys: [""].concat(backend.autoTagGroups.map(g => g.key))
                model: [qsTr("All subjects")].concat(backend.autoTagGroups.map(g => g.label))
                currentIndex: keys.indexOf(root.group); onActivated: i => root.group = keys[i]
                tip: qsTr("Filter the subject list by group.")
            }
        }
        C.ScrollView {
            id: scroll; anchors.top: heading.bottom; anchors.topMargin: Theme.s2
            anchors.left: parent.left; anchors.right: parent.right
            anchors.bottom: footer.top; anchors.bottomMargin: Theme.s2
            clip: true; contentWidth: availableWidth
            C.ScrollBar.vertical: ScrollBar {}
            C.ScrollBar.horizontal.policy: C.ScrollBar.AlwaysOff
            Column {
                width: scroll.availableWidth; spacing: Theme.s1
                Repeater {
                    model: backend.autoTagOptions.filter(o => (!root.group || o.group === root.group) && o.label.toLowerCase().indexOf(root.query.toLowerCase()) >= 0)
                    CheckField {
                        required property var modelData
                        objectName: "autoTagChoice_" + modelData.tag
                        width: parent.width; text: modelData.label; checkable: false
                        checked: root.tag(modelData.tag).active; enabled: backend.currentId > 0
                        tip: root.tag(modelData.tag).decision === -1 ? qsTr("Excluded by you. Tick to include it.")
                            : root.tag(modelData.tag).decision === 1 ? qsTr("Added by you. Untick to exclude it.") : qsTr("Correct this subject for the current photo.")
                        onClicked: backend.setCurrentAutoTag(modelData.tag, !checked)
                    }
                }
            }
        }
        Row {
            id: footer; anchors.right: parent.right; anchors.bottom: parent.bottom
            ToolButton { objectName: "autoTagPickerDone"; text: qsTr("Done"); showLabel: true; onClicked: root.close() }
        }
    }
}
