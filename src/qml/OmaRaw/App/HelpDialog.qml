pragma ComponentBehavior: Bound
import QtQuick
import QtCore
import QtQuick.Controls.Basic as C
import OmaRaw.Ui

// The help: a section list on the left, the page on the right, a search
// over titles and page text. Sections are the backend's table; the pages
// ship with the program. Not modal, so the picture can be worked on while
// a page is open. F1 opens it at the current workspace; the ? in a panel
// header opens it at that panel's page.
C.Popup {
    id: root
    objectName: "helpDialog"
    property var shell: null
    property string section: "start"
    property string filter: ""
    property int textSize: 18
    property bool expanded: false
    Settings {
        category: "help"
        property alias textSize: root.textSize
        property alias expanded: root.expanded
    }
    readonly property var sections: backend.helpSections()
    readonly property var shown: {
        const q = root.filter.trim().toLowerCase()
        if (q === "") return root.sections
        return root.sections.filter(s => s.title.toLowerCase().indexOf(q) >= 0 || backend.helpText(s.id).toLowerCase().indexOf(q) >= 0)
    }
    readonly property string body: backend.helpText(root.section)
    readonly property string title: { const s = root.sections.find(x => x.id === root.section); return s ? s.title : "" }

    function openSection(id) {
        const known = root.sections.some(s => s.id === id)
        root.section = known ? id : "start"
        open()
        scroll.C.ScrollBar.vertical.position = 0
    }

    modal: false
    focus: true
    closePolicy: C.Popup.CloseOnEscape
    parent: C.Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(expanded ? 100000 : 1120, (parent ? parent.width : 1160) - 24)
    height: Math.min(expanded ? 100000 : 800, (parent ? parent.height : 824) - 24)
    padding: 0
    background: Rectangle { color: Theme.panelRaised; border.width: Theme.hairline; border.color: Theme.borderStrong; radius: Theme.rMenu }

    Item {
        anchors.fill: parent
        // ── header ───────────────────────────────────────────────────────
        Item {
            id: header
            readonly property bool compact: width < 720
            width: parent.width; height: compact ? 132 : 92
            Text {
                x: Theme.s4; y: 12
                width: guideButton.x - x - Theme.s2
                text: qsTr("OmaRAW Help"); elide: Text.ElideRight
                font.family: Theme.fontFamily; font.pixelSize: Theme.fsTitle; font.weight: Theme.wHeading; color: Theme.textPrimary
            }
            IconButton {
                id: guideButton
                anchors.right: closeButton.left; anchors.rightMargin: Theme.s1
                y: 6
                iconName: "file"; text: qsTr("The written guide")
                tip: qsTr("Open the folder holding the full guide and help pages.")
                onClicked: backend.revealDocumentation()
            }
            IconButton {
                id: closeButton
                anchors.right: parent.right; anchors.rightMargin: Theme.s2
                y: 6
                iconName: "x"; text: qsTr("Close"); shortcut: "Esc"
                onClicked: root.close()
            }
            SearchField {
                id: search
                objectName: "helpSearch"
                x: Theme.s4; y: 48
                width: header.compact ? parent.width - Theme.s4 * 2 : Math.min(300, tools.x - x - Theme.s3)
                placeholder: qsTr("Search the help")
                tip: qsTr("Find pages by their title or any words in the guide.")
                onTextChanged: root.filter = text
            }
            Row {
                id: tools
                anchors.right: parent.right; anchors.rightMargin: Theme.s4
                y: header.compact ? 88 : 48
                spacing: Theme.s2
                ToolButton {
                    objectName: "helpSmaller"
                    text: "A−"; showLabel: true; tip: qsTr("Make help text smaller.")
                    enabled: root.textSize > 12
                    onClicked: root.textSize = Math.max(12, root.textSize - 2)
                }
                ToolButton {
                    objectName: "helpLarger"
                    text: "A+"; showLabel: true; tip: qsTr("Make help text larger.")
                    enabled: root.textSize < 32
                    onClicked: root.textSize = Math.min(32, root.textSize + 2)
                }
                ToolButton {
                    objectName: "helpExpand"
                    text: root.expanded ? qsTr("Restore size") : qsTr("Expand"); showLabel: true
                    tip: qsTr("Use the full application window for reading.")
                    onClicked: root.expanded = !root.expanded
                }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: Theme.hairline; color: Theme.border }
        }
        // ── sections ─────────────────────────────────────────────────────
        C.ScrollView {
            id: list
            objectName: "helpList"
            anchors.top: header.bottom; anchors.bottom: parent.bottom; anchors.left: parent.left
            width: Math.min(230, root.width * 0.28)
            clip: true
            C.ScrollBar.vertical: ScrollBar {}
            C.ScrollBar.horizontal.policy: C.ScrollBar.AlwaysOff
            contentWidth: availableWidth
            Column {
                width: list.availableWidth
                topPadding: Theme.s2; bottomPadding: Theme.s2
                Repeater {
                    model: root.shown
                    Item {
                        id: row
                        required property int index
                        required property var modelData
                        readonly property bool groupStart: index === 0 || root.shown[index - 1].group !== modelData.group
                        readonly property bool active: modelData.id === root.section
                        width: parent.width; height: (groupStart ? Theme.hRow : 0) + Theme.hRow
                        Text {
                            visible: row.groupStart
                            x: Theme.s3; height: Theme.hRow
                            verticalAlignment: Text.AlignVCenter
                            text: row.modelData.group.toUpperCase()
                            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; font.weight: Theme.wHeading; color: Theme.textMuted
                        }
                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: parent.width; height: Theme.hRow
                            color: hover.hovered ? Theme.hovered(row.active ? Theme.controlBg : Theme.panelRaised)
                                                 : row.active ? Theme.controlBg : "transparent"
                            Text {
                                anchors.left: parent.left; anchors.leftMargin: Theme.s4
                                anchors.verticalCenter: parent.verticalCenter
                                text: row.modelData.title
                                width: parent.width - Theme.s4 * 2; elide: Text.ElideRight
                                font.family: Theme.fontFamily; font.pixelSize: Theme.fsControl; color: row.active ? Theme.textPrimary : Theme.textSecondary
                            }
                            HoverHandler { id: hover }
                            TapHandler { onTapped: root.openSection(row.modelData.id) }
                        }
                    }
                }
                Text {
                    visible: root.shown.length === 0
                    x: Theme.s3; topPadding: Theme.s2
                    text: qsTr("Nothing matches.")
                    font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
                }
            }
        }
        Rectangle { anchors.top: header.bottom; anchors.bottom: parent.bottom; x: list.width; width: Theme.hairline; color: Theme.border }
        // ── page ─────────────────────────────────────────────────────────
        C.ScrollView {
            id: scroll
            anchors.top: header.bottom; anchors.bottom: parent.bottom
            anchors.left: list.right; anchors.right: parent.right
            clip: true
            C.ScrollBar.vertical: ScrollBar {}
            C.ScrollBar.horizontal.policy: C.ScrollBar.AlwaysOff
            contentWidth: availableWidth
            Text {
                id: page
                objectName: "helpBody"
                width: scroll.availableWidth
                leftPadding: Theme.s5; rightPadding: Theme.s5; topPadding: Theme.s3; bottomPadding: Theme.s5
                textFormat: Text.MarkdownText
                wrapMode: Text.Wrap
                text: root.body
                lineHeight: 1.2
                font.family: Theme.fontFamily; font.pixelSize: Math.max(12, Math.min(32, root.textSize))
                color: Theme.textPrimary
                linkColor: Theme.accent
                onLinkActivated: link => Qt.openUrlExternally(link)
            }
        }
    }
}
