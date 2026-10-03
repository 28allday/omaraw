import QtQuick
import QtQuick.Controls.Basic as C
import OmaRaw.Ui

// Auto-apply rules for one preset: which photos get it on import - by
// camera, lens, ISO range and format. Every field is optional; an empty
// rule matches every photo. The rules already set for the preset are
// listed with a way to remove them.
C.Popup {
    id: root
    objectName: "presetRuleDialog"
    property string preset: ""
    readonly property var rule: ({camera: cameraField.text.trim(), lens: lensField.text.trim(),
        format: formatField.text.trim(), isoMin: Number(isoMinField.text), isoMax: Number(isoMaxField.text),
        apertureMin: apertureMinField.text.trim() || 0, apertureMax: apertureMaxField.text.trim() || 0,
        shutterMin: shutterMinField.text.trim() || 0, shutterMax: shutterMaxField.text.trim() || 0})
    readonly property var existing: { backend.presetRuleCount; return backend.presetRules().filter(r => r.preset === root.preset) }
    property string saveError: ""
    function validIso(text) { return text.trim() === "" || (/^[1-9][0-9]*$/.test(text.trim()) && Number(text) <= 2147483647) }
    function exposureValue(text, fraction) {
        text = text.trim()
        if (text === "") return 0
        const number = value => /^[+]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][+-]?[0-9]+)?$/.test(value.trim()) ? Number(value) : NaN
        const parts = text.split("/")
        if (parts.length === 1) return number(text)
        if (fraction && parts.length === 2 && number(parts[1]) > 0) return number(parts[0]) / number(parts[1])
        return NaN
    }
    function validExposure(text, fraction) { const value = exposureValue(text, fraction); return text.trim() === "" || (Number.isFinite(value) && value > 0) }
    function orderedExposure(first, last, fraction) { const a = exposureValue(first, fraction), b = exposureValue(last, fraction); return a === 0 || b === 0 || a <= b }
    readonly property string ruleError: !validIso(isoMinField.text) || !validIso(isoMaxField.text)
        ? qsTr("Enter a positive whole ISO value, or leave the field empty.")
        : rule.isoMin > 0 && rule.isoMax > 0 && rule.isoMin > rule.isoMax
          ? qsTr("The starting ISO must not exceed the ending ISO.")
        : !validExposure(apertureMinField.text, false) || !validExposure(apertureMaxField.text, false)
          ? qsTr("Enter positive aperture values such as 2.8, or leave the fields empty.")
        : !validExposure(shutterMinField.text, true) || !validExposure(shutterMaxField.text, true)
          ? qsTr("Enter shutter times in seconds or fractions such as 1/125, or leave the fields empty.")
        : !orderedExposure(apertureMinField.text, apertureMaxField.text, false) || !orderedExposure(shutterMinField.text, shutterMaxField.text, true)
          ? qsTr("Put the lower aperture number or shorter shutter time first.") : ""
    onRuleChanged: saveError = ""
    function openFor(name) {
        preset = name
        cameraField.text = ""; lensField.text = ""; formatField.text = ""
        isoMinField.text = ""; isoMaxField.text = ""; saveError = ""
        apertureMinField.text = ""; apertureMaxField.text = ""; shutterMinField.text = ""; shutterMaxField.text = ""
        open()
    }
    function add() {
        if (ruleError !== "") return
        if (backend.createPresetRule(preset, rule) > 0) close()
        else saveError = backend.statusMessage
    }

    modal: true
    parent: C.Overlay.overlay
    anchors.centerIn: parent
    width: 440; padding: Theme.s4
    background: Rectangle { color: Theme.panelRaised; border.width: Theme.hairline; border.color: Theme.borderStrong; radius: Theme.rMenu }
    onOpened: cameraField.focusInput()

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

    Column {
        width: parent.width
        spacing: Theme.s2
        Text { text: qsTr("Auto-apply \"%1\"").arg(root.preset); font.family: Theme.fontFamily; font.pixelSize: Theme.fsHeading; font.weight: Theme.wHeading; color: Theme.textPrimary; elide: Text.ElideRight; width: parent.width }
        Text {
            width: parent.width; wrapMode: Text.WordWrap
            text: qsTr("Photos that match get this preset on import. If several rules match, the last matching rule wins. Leave a field empty to match anything.")
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
        }
        RuleRow { label: qsTr("Camera"); SearchField { id: cameraField; objectName: "presetRuleCamera"; anchors.fill: parent; placeholder: qsTr("make or model contains…"); tip: qsTr("The preset applies on import to photos whose camera make or model contains this.") } }
        RuleRow { label: qsTr("Lens"); SearchField { id: lensField; objectName: "presetRuleLens"; anchors.fill: parent; placeholder: qsTr("contains…"); tip: qsTr("Only photos whose lens name contains this.") } }
        RuleRow { label: qsTr("Format"); SearchField { id: formatField; objectName: "presetRuleFormat"; anchors.fill: parent; placeholder: qsTr("RAF, NEF, JPEG… or RAW for any raw"); tip: qsTr("Only files of this type; RAW matches every raw format.") } }
        RuleRow {
            label: qsTr("ISO")
            Row {
                anchors.fill: parent
                spacing: Theme.s2
                SearchField { id: isoMinField; objectName: "presetRuleIsoMin"; width: (parent.width - Theme.s2) / 2; height: parent.height; placeholder: qsTr("from"); tip: qsTr("Lowest ISO the rule applies to; blank for no lower limit.") }
                SearchField { id: isoMaxField; objectName: "presetRuleIsoMax"; width: (parent.width - Theme.s2) / 2; height: parent.height; placeholder: qsTr("to"); tip: qsTr("Highest ISO the rule applies to; blank for no upper limit.") }
            }
        }
        RuleRow {
            label: qsTr("Aperture · f/")
            Row {
                anchors.fill: parent; spacing: Theme.s2
                SearchField { id: apertureMinField; objectName: "presetRuleApertureMin"; width: (parent.width - Theme.s2) / 2; height: parent.height; placeholder: qsTr("from, e.g. 2.8"); tip: qsTr("Widest aperture the rule applies to; blank for no limit.") }
                SearchField { id: apertureMaxField; objectName: "presetRuleApertureMax"; width: (parent.width - Theme.s2) / 2; height: parent.height; placeholder: qsTr("to, e.g. 8"); tip: qsTr("Narrowest aperture the rule applies to; blank for no limit.") }
            }
        }
        RuleRow {
            label: qsTr("Shutter · s")
            Row {
                anchors.fill: parent; spacing: Theme.s2
                SearchField { id: shutterMinField; objectName: "presetRuleShutterMin"; width: (parent.width - Theme.s2) / 2; height: parent.height; placeholder: qsTr("from, e.g. 1/1000"); tip: qsTr("Fastest shutter the rule applies to; blank for no limit.") }
                SearchField { id: shutterMaxField; objectName: "presetRuleShutterMax"; width: (parent.width - Theme.s2) / 2; height: parent.height; placeholder: qsTr("to, e.g. 1/60"); tip: qsTr("Slowest shutter the rule applies to; blank for no limit.") }
            }
        }
        Text {
            width: parent.width; wrapMode: Text.WordWrap
            text: qsTr("Ranges include both ends. Photos with missing exposure metadata do not match a restricted range.")
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
        }
        Text {
            width: parent.width; wrapMode: Text.WordWrap
            visible: text !== ""; text: root.ruleError || root.saveError
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.danger
        }
        Item { width: 1; height: Theme.s1; visible: root.existing.length > 0 }
        Text {
            visible: root.existing.length > 0
            text: qsTr("Already applied to")
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; font.weight: Theme.wHeading; color: Theme.textSecondary
        }
        Repeater {
            model: root.existing
            Item {
                required property var modelData
                width: parent.width; height: Theme.hRow
                Text {
                    anchors.left: parent.left; anchors.right: remove.left; anchors.rightMargin: Theme.s2
                    anchors.verticalCenter: parent.verticalCenter
                    text: parent.modelData.text
                    elide: Text.ElideRight
                    font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textSecondary
                }
                IconButton { id: remove; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; iconName: "x"; text: qsTr("Remove this rule"); tip: qsTr("Imports stop applying the preset by this rule."); onClicked: backend.deletePresetRule(parent.modelData.id) }
            }
        }
        Item { width: 1; height: Theme.s1 }
        Row {
            anchors.right: parent.right
            spacing: Theme.s2
            ToolButton { text: qsTr("Close"); showLabel: true; onClicked: root.close() }
            ToolButton { objectName: "presetRuleAdd"; text: qsTr("Add Rule"); showLabel: true; tip: qsTr("From now on, photos matching every field filled in get this preset when they are imported."); enabled: root.ruleError === ""; onClicked: root.add() }
        }
    }
}
