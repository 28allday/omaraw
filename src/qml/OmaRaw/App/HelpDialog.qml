pragma ComponentBehavior: Bound
import QtQuick
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
    closePolicy: C.Popup.CloseOnEscape
    parent: C.Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(980, (parent ? parent.width : 1000) - 2 * Theme.s5)
    height: Math.min(720, (parent ? parent.height : 800) - 2 * Theme.s5)
    padding: 0
    background: Rectangle { color: Theme.panelRaised; border.width: Theme.hairline; border.color: Theme.borderStrong; radius: Theme.rMenu }

    Item {
        anchors.fill: parent
        // ── header ───────────────────────────────────────────────────────
        Item {
            id: header
            width: parent.width; height: Theme.hDockHeader + Theme.s2
            Text {
                anchors.left: parent.left; anchors.leftMargin: Theme.s4
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("OmaRAW Help")
                font.family: Theme.fontFamily; font.pixelSize: Theme.fsTitle; font.weight: Theme.wHeading; color: Theme.textPrimary
            }
            SearchField {
                id: search
                objectName: "helpSearch"
                anchors.right: guideButton.left; anchors.rightMargin: Theme.s2
                anchors.verticalCenter: parent.verticalCenter
                width: 240
                placeholder: qsTr("Search the help")
                tip: qsTr("Find pages by their title or any words in the guide.")
                onTextChanged: root.filter = text
            }
            IconButton {
                id: guideButton
                anchors.right: closeButton.left; anchors.rightMargin: Theme.s1
                anchors.verticalCenter: parent.verticalCenter
                iconName: "file"; text: qsTr("The written guide")
                tip: qsTr("Opens the folder holding the full guide and these pages as files, installed with the program.")
                onClicked: backend.revealDocumentation()
            }
            IconButton {
                id: closeButton
                anchors.right: parent.right; anchors.rightMargin: Theme.s2
                anchors.verticalCenter: parent.verticalCenter
                iconName: "x"; text: qsTr("Close"); shortcut: "Esc"
                onClicked: root.close()
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: Theme.hairline; color: Theme.border }
        }
        // ── sections ─────────────────────────────────────────────────────
        C.ScrollView {
            id: list
            objectName: "helpList"
            anchors.top: header.bottom; anchors.bottom: parent.bottom; anchors.left: parent.left
            width: 230
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
                font.family: Theme.fontFamily; font.pixelSize: Theme.fsControl
                color: Theme.textPrimary
                linkColor: Theme.accent
                onLinkActivated: link => Qt.openUrlExternally(link)
            }
        }
    }
}
