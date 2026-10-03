pragma ComponentBehavior: Bound
import QtQuick
import OmaRaw.Ui

Column {
    id: root
    property bool embedded: false
    property bool saveEnabled: true
    signal savePresetRequested(string operation, string label)
    readonly property var params: (engine.paramsVersion, engine.paramsFor("colorbalancergb"))
    readonly property var generalRows: params.filter(p => ["saturation_global","vibrance","chroma_global","brilliance_global","hue_angle"].includes(p.field))
    readonly property bool moduleOn: params.length > 0 && params[0].enabled
    // Contrast belongs to Light; Primary correction belongs to Look.
    property bool more: false
    readonly property var shownRows: ["vibrance","saturation_global","chroma_global","brilliance_global","hue_angle"]
        .map(f => root.generalRows.find(p => p.field === f)).filter(p => p !== undefined)
    spacing: Theme.s1
    height: visible ? implicitHeight : 0
    BlockHeading {
        visible: !root.embedded
        height: visible ? Theme.hRow + Theme.s1 : 0
        text: qsTr("Vibrance & saturation"); tip: qsTr("How strong the colours are. Vibrance lifts the muted ones first and is kinder to skin.")
        operation: "colorbalancergb"; on: root.moduleOn; saveEnabled: root.saveEnabled
        onEyeClicked: engine.setModuleEnabled("colorbalancergb", !root.moduleOn)
        onSavePresetRequested: (operation, label) => root.savePresetRequested(operation, label)
    }
    ModuleRows { width: root.width; headings: false; rows: root.shownRows.slice(0, 2) }
    SlideSection {
        width: root.width; expanded: root.more
        ModuleRows { width: root.width; headings: false; rows: root.shownRows.slice(2) }
    }
}
