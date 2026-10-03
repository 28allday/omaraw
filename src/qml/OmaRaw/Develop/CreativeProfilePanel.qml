pragma ComponentBehavior: Bound
import QtQuick
import OmaRaw.Ui

Column {
    id: root
    objectName: "creativeProfilePanel"
    readonly property var rows: (engine.paramsVersion, engine.paramsFor("omarawprofile"))
    readonly property int look: (rows.find(p => p.field === "look") || {}).value || 0
    readonly property string path: (rows.find(p => p.field === "filepath") || {}).text || ""
    readonly property var details: engine.creativeProfileDetails(path)
    readonly property string key: look === 7 ? path : "builtin:" + look
    readonly property var profiles: engine.creativeProfiles
    readonly property int selected: profiles.findIndex(p => p.key === key)
    spacing: Theme.s1
    FilePicker {
        id: picker; title: qsTr("Import a creative profile")
        nameFilters: [qsTr("Creative profiles (*.cube *.CUBE *.xmp *.XMP)")]
        onAccepted: engine.importCreativeProfile(selectedFile.toString())
    }
    ComboField {
        objectName: "creativeProfileChoice"
        x: Theme.s3; width: parent.width - 2*Theme.s3
        model: root.profiles.map(p => p.name); currentIndex: root.selected
        enabled: engine.imageId >= 0
        tipTitle: qsTr("Creative profile"); tip: qsTr("A colour look applied independently of exposure, white balance and film settings.")
        onActivated: i => engine.selectCreativeProfile(root.profiles[i].key)
    }
    Text {
        x: Theme.s3; width: parent.width - 2*Theme.s3; wrapMode: Text.WordWrap
        text: root.look === 7 ? engine.creativeProfileFileStatus(root.path)
                             : (root.profiles[root.selected] || {}).description || ""
        font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
    }
    ModuleRows {
        width: parent.width; headings: false
        rows: root.rows.filter(p => (p.field === "amount" && (!root.details.enhanced || root.details.amount)) || (root.look === 7 && !root.details.enhanced && p.field === "colorspace"))
    }
    ToolButton {
        objectName: "importCreativeProfile"
        x: Theme.s3; iconName: "folder-open"; text: qsTr("Import profile…"); showLabel: true
        enabled: engine.imageId >= 0; onClicked: picker.open()
        tip: qsTr("Import a .cube LUT or an enhanced .xmp profile. OmaRAW keeps its own copy in the profile library.")
    }
}
