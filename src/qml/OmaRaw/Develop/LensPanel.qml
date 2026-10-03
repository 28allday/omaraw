pragma ComponentBehavior: Bound
import QtQuick
import OmaRaw.Ui

// Lens profile: what the lens database matched for this photo's body and
// lens, and a search over the lenses it knows for the mount to pick
// another when the file names its lens badly or not at all.
Column {
    id: root
    readonly property var lp: engine.lensProfile || ({})
    readonly property bool found: lp.found === true
    readonly property bool overridden: lp.overridden === true
    property bool searching: false
    property string query: ""
    readonly property var candidates: engine.lensCandidates || []
    readonly property var matches: {
        const q = root.query.trim().toLowerCase()
        const all = root.candidates
        if (q === "") return all.slice(0, 40)
        const words = q.split(/\s+/)
        return all.filter(n => { const l = n.toLowerCase(); return words.every(w => l.indexOf(w) >= 0) }).slice(0, 40)
    }
    spacing: Theme.s1
    visible: found
    onVisibleChanged: if (!visible) { searching = false; query = "" }

    Item {
        width: parent.width; height: Theme.hRow
        Text {
            anchors.left: parent.left; anchors.leftMargin: Theme.s3
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("lens profile")
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; font.weight: Theme.wHeading; font.capitalization: Font.AllUppercase; font.letterSpacing: 0.6
            color: Theme.textSecondary
        }
        IconButton {
            objectName: "resetLensProfile"
            visible: root.overridden
            anchors.right: parent.right; anchors.rightMargin: Theme.s3
            anchors.verticalCenter: parent.verticalCenter
            iconName: "rotate-ccw"
            text: qsTr("Back to the lens the file names")
            tip: qsTr("Drops the lens you picked by hand and matches from the file's own data again.")
            onClicked: engine.setLensOverride("")
        }
    }
    component InfoRow: Item {
        property string label: ""
        property string value: ""
        property bool known: true
        width: parent.width; height: Theme.hRow
        Text {
            anchors.left: parent.left; anchors.leftMargin: Theme.s3; width: 60
            anchors.verticalCenter: parent.verticalCenter
            text: parent.label
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
        }
        Text {
            anchors.left: parent.left; anchors.leftMargin: Theme.s3 + 60
            anchors.right: parent.right; anchors.rightMargin: Theme.s3
            anchors.verticalCenter: parent.verticalCenter
            text: parent.value
            elide: Text.ElideMiddle
            textFormat: Text.PlainText
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
            color: parent.known ? Theme.textSecondary : Theme.warning
        }
    }
    InfoRow {
        label: qsTr("Camera")
        known: root.lp.cameraFound === true
        value: known ? root.lp.camera : (root.lp.exifCamera ? qsTr("%1 — not in the database").arg(root.lp.exifCamera) : qsTr("unknown body"))
    }
    InfoRow {
        label: qsTr("Lens")
        known: root.lp.lensFound === true
        value: known ? root.lp.lens + (root.overridden ? qsTr(" (chosen)") : "")
                     : (root.lp.exifLens ? qsTr("%1 — not in the database").arg(root.lp.exifLens) : qsTr("no lens named in the file"))
    }
    Item {
        width: parent.width; height: Theme.hControl + Theme.s1
        SearchField {
            id: search
            objectName: "lensProfileSearch"
            anchors.left: parent.left; anchors.leftMargin: Theme.s3
            anchors.right: parent.right; anchors.rightMargin: Theme.s3
            anchors.verticalCenter: parent.verticalCenter
            placeholder: root.lp.cameraFound === true ? qsTr("Find a lens for this mount…") : qsTr("Find a lens…")
            tip: qsTr("Type part of a lens name to pick a profile by hand when the file's own is missing or wrong.")
            // SearchField's live mode emits accepted on every keystroke.
            // Filtering is live; selecting a profile must be explicit.
            live: false
            onTextChanged: { root.query = text; if (text !== "") root.searching = true }
            onActiveChanged: if (active) { root.searching = true; if (root.candidates.length === 0) engine.loadLensCandidates() }
            onCleared: { root.query = ""; root.searching = false }
            onAccepted: t => { if (t.trim() !== "" && root.matches.length > 0) root.pick(root.matches[0]) }
        }
    }
    function pick(name) {
        engine.setLensOverride(name)
        search.text = ""
        root.query = ""
        root.searching = false
    }
    Text {
        visible: root.searching && root.candidates.length === 0
        anchors.left: parent.left; anchors.leftMargin: Theme.s3
        text: qsTr("Reading the lens database…")
        font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
    }
    Rectangle {
        visible: root.searching && root.candidates.length > 0
        width: parent.width - Theme.s3 * 2; x: Theme.s3
        height: Math.min(8, Math.max(1, root.matches.length)) * Theme.hRow + Theme.hairline * 2
        color: Theme.controlBg; radius: Theme.rControl
        border.width: Theme.hairline; border.color: Theme.border
        ListView {
            id: list
            objectName: "lensProfileMatches"
            anchors.fill: parent; anchors.margins: Theme.hairline
            clip: true
            model: root.matches
            delegate: Rectangle {
                id: row
                required property string modelData
                required property int index
                objectName: "lensProfileMatch_" + index
                width: list.width; height: Theme.hRow
                color: hover.hovered ? Theme.panelRaised : "transparent"
                Text {
                    anchors.left: parent.left; anchors.leftMargin: Theme.s2
                    anchors.right: parent.right; anchors.rightMargin: Theme.s2
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.modelData
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                    font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textSecondary
                }
                HoverHandler { id: hover }
                TapHandler { onTapped: root.pick(row.modelData) }
            }
            Text {
                visible: root.matches.length === 0
                anchors.centerIn: parent
                text: qsTr("No lens matches")
                font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
            }
        }
    }
}
