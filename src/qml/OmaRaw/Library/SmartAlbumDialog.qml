import QtQuick
import QtQuick.Controls.Basic as C
import OmaRaw.Ui

// Rule builder for smart albums. Every field is optional; the album
// matches all of them (or any). The live count says how many photos the
// rules catch right now.
C.Popup {
    id: root
    property int albumId: 0
    property var rule: ({})
    readonly property int matches: backend.smartRuleCount(rule)
    function openFor(id, name) {
        albumId = id
        rule = id ? backend.smartAlbumRule(id) : { match: "all" }
        nameField.text = name || ""
        open()
    }
    function set(key, value) {
        const r = Object.assign({}, rule)
        if (value === "" || value === 0 || value === undefined) delete r[key]; else r[key] = value
        rule = r
    }
    function save() {
        if (nameField.text.trim() === "") return
        if (albumId) backend.updateSmartAlbum(albumId, nameField.text.trim(), rule)
        else backend.createSmartAlbum(nameField.text.trim(), rule)
        close()
    }
    modal: true
    parent: C.Overlay.overlay
    anchors.centerIn: parent
    width: 440; padding: Theme.s4
    background: Rectangle { color: Theme.panelRaised; border.width: Theme.hairline; border.color: Theme.borderStrong; radius: Theme.rMenu }
    onOpened: nameField.forceActiveFocus()

    component RuleRow: Item {
        property string label: ""
        default property alias content: slot.data
        width: parent.width; height: Theme.hRow + Theme.s1
        Text {
            anchors.left: parent.left; width: 96
            anchors.verticalCenter: parent.verticalCenter
            text: parent.label
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
        }
        Item { id: slot; anchors.left: parent.left; anchors.leftMargin: 96; anchors.right: parent.right; height: parent.height }
    }
    component RuleText: SearchField {
        property string key: ""
        anchors.fill: parent
        live: true
        text: root.rule[key] || ""
        onTextChanged: root.set(key, text)
        onCleared: root.set(key, "")
    }

    Column {
        width: parent.width
        spacing: Theme.s2
        Text { text: root.albumId ? qsTr("Edit smart album") : qsTr("New smart album"); font.family: Theme.fontFamily; font.pixelSize: Theme.fsHeading; font.weight: Theme.wHeading; color: Theme.textPrimary }
        RuleRow { label: qsTr("Name"); SearchField { id: nameField; anchors.fill: parent; placeholder: qsTr("Smart album name"); tip: qsTr("The name shown in the sidebar."); live: false; onAccepted: root.save() } }
        RuleRow {
            label: qsTr("Match")
            SegmentedControl {
                anchors.verticalCenter: parent.verticalCenter
                labels: [qsTr("All rules"), qsTr("Any rule")]
                tips: [qsTr("A photo must satisfy every rule below."), qsTr("A photo needs to satisfy only one of the rules below.")]
                currentIndex: root.rule.match === "any" ? 1 : 0
                onActivated: i => root.set("match", i === 1 ? "any" : "all")
            }
        }
        RuleRow {
            label: qsTr("Rating")
            Row {
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.s2
                RatingStars { anchors.verticalCenter: parent.verticalCenter; rating: root.rule.rating || 0; interactive: true; onRated: r => root.set("rating", r === (root.rule.rating || 0) ? 0 : r) }
                Text { anchors.verticalCenter: parent.verticalCenter; text: root.rule.rating ? qsTr("and up") : qsTr("any"); font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted }
            }
        }
        RuleRow {
            label: qsTr("Flag")
            SegmentedControl {
                anchors.verticalCenter: parent.verticalCenter
                labels: [qsTr("Any"), qsTr("Picks"), qsTr("Unflagged"), qsTr("Rejected")]
                tip: qsTr("Which flag the photos must carry.")
                readonly property var keys: ["", "pick", "unflagged", "reject"]
                currentIndex: Math.max(0, keys.indexOf(root.rule.flag || ""))
                onActivated: i => root.set("flag", keys[i])
            }
        }
        RuleRow {
            label: qsTr("Label")
            LabelDots { anchors.verticalCenter: parent.verticalCenter; picker: true; size: 12; label: root.rule.label || ""; onPicked: l => root.set("label", (root.rule.label || "") === l ? "" : l) }
        }
        RuleRow {
            label: qsTr("Auto tag")
            ComboField {
                objectName: "smartAutoTag"
                anchors.fill: parent
                readonly property var keys: [""].concat(backend.autoTagCollections.map(o => o.key))
                model: [qsTr("Any")].concat(backend.autoTagCollections.map(o => o.group ? o.label + qsTr(" (group)") : o.label))
                currentIndex: Math.max(0, keys.indexOf(root.rule.autoTag || ""))
                tip: qsTr("Automatic subject tags, including your corrections.")
                onActivated: i => root.set("autoTag", keys[i])
            }
        }
        RuleRow { label: qsTr("Keyword"); RuleText { key: "keyword"; placeholder: qsTr("contains…"); tip: qsTr("Photos carrying a keyword that contains this.") } }
        RuleRow { label: qsTr("File name"); RuleText { key: "text"; placeholder: qsTr("contains…"); tip: qsTr("Photos whose file name contains this.") } }
        RuleRow { label: qsTr("Camera"); RuleText { key: "camera"; placeholder: qsTr("make or model contains…"); tip: qsTr("Photos whose camera make or model contains this.") } }
        RuleRow { label: qsTr("Lens"); RuleText { key: "lens"; placeholder: qsTr("contains…"); tip: qsTr("Photos whose lens name contains this.") } }
        RuleRow {
            label: qsTr("Format")
            ComboField {
                anchors.fill: parent
                readonly property var keys: ["", "raw", "JPEG", "TIFF", "PNG", "HEIF", "AVIF", "DNG"]
                model: [qsTr("Any"), qsTr("RAW of any kind"), "JPEG", "TIFF", "PNG", "HEIF", "AVIF", "DNG"]
                tipTitle: qsTr("Format"); tip: qsTr("Only files of this type.")
                currentIndex: Math.max(0, keys.indexOf(root.rule.format || ""))
                onActivated: i => root.set("format", keys[i])
            }
        }
        RuleRow {
            label: qsTr("Edited")
            SegmentedControl {
                anchors.verticalCenter: parent.verticalCenter
                labels: [qsTr("Either"), qsTr("Edited"), qsTr("Untouched")]
                tip: qsTr("Whether the photos must have develop edits.")
                readonly property var keys: ["", "yes", "no"]
                currentIndex: Math.max(0, keys.indexOf(root.rule.edited || ""))
                onActivated: i => root.set("edited", keys[i])
            }
        }
        RuleRow { label: qsTr("Taken from"); RuleText { key: "from"; placeholder: "YYYY-MM-DD"; tip: qsTr("Earliest capture date, as year-month-day; blank for no limit.") } }
        RuleRow { label: qsTr("Taken to"); RuleText { key: "to"; placeholder: "YYYY-MM-DD"; tip: qsTr("Latest capture date, as year-month-day; blank for no limit.") } }
        RuleRow {
            label: qsTr("Stacks")
            Toggle { anchors.verticalCenter: parent.verticalCenter; label: qsTr("Stack tops only"); checked: root.rule.stacked === "top"; onClicked: root.set("stacked", root.rule.stacked === "top" ? "" : "top") }
        }
        Text {
            width: parent.width; wrapMode: Text.WordWrap
            text: qsTr("%1 photo%2 match right now. Rejected photos stay out, as everywhere.").arg(root.matches).arg(root.matches === 1 ? "" : "s")
            font.family: Theme.monoFamily; font.pixelSize: Theme.fsLabel; color: Theme.textSecondary
        }
        Row {
            anchors.right: parent.right
            spacing: Theme.s2
            ToolButton { text: qsTr("Cancel"); showLabel: true; onClicked: root.close() }
            ToolButton { text: root.albumId ? qsTr("Save") : qsTr("Create"); showLabel: true; tip: qsTr("The album keeps itself up to date as photos come and go."); enabled: nameField.text.trim() !== ""; onClicked: root.save() }
        }
    }
}
