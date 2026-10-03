pragma ComponentBehavior: Bound
import QtQuick
import OmaRaw.Ui

Column {
    id: root
    property bool embedded: false
    objectName: "primaryGradePanel"
    property bool saveEnabled: true
    signal savePresetRequested(string operation, string label)
    readonly property var params: (engine.paramsVersion, engine.paramsFor("omarawgrade"))
    readonly property var zones: ["lift", "gamma", "gain", "offset"]
    readonly property var titles: [qsTr("Lift"), qsTr("Gamma"), qsTr("Gain"), qsTr("Offset")]
    readonly property var titleTips: [qsTr("Mostly the shadows."), qsTr("Mostly the midtones."), qsTr("Mostly the highlights."), qsTr("The whole picture, evenly.")]
    readonly property bool moduleOn: params.length > 0 && params[0].enabled
    readonly property bool narrow: width < 360
    readonly property bool overview: view === 0 && !narrow
    readonly property int activeZone: Math.max(0, view-1)
    // Zero chooses the responsive overview, 1..4 choose individual ranges.
    // Resizing never writes image parameters or destroys a wheel component.
    property int view: 0
    // Folded to its heading until asked for: four wheels are a lot to meet first.
    property bool open: false
    spacing: Theme.s1
    height: visible ? implicitHeight : 0
    function resetWheels() {
        engine.applyValues(params
                           .map(p => ({op: p.op, field: p.field, value: p.def})))
    }
    function toggleBefore() { engine.applyValues([{op: "omarawgrade", enabled: !moduleOn}]) }
    BlockHeading {
        visible: !root.embedded
        height: visible ? Theme.hRow + Theme.s1 : 0
        objectName: "primaryGradeHeading"
        text: qsTr("Colour wheels"); tip: qsTr("Tint and lighten the shadows, midtones and highlights separately, or the whole picture.")
        collapsible: true; open: root.open; onToggled: root.open = !root.open
        operation: "omarawgrade"; on: root.moduleOn; eyeName: "primaryGradeBefore"; eyeEnabled: root.params.length > 0
        saveEnabled: root.saveEnabled
        onEyeClicked: root.toggleBefore()
        onSavePresetRequested: (operation, label) => root.savePresetRequested(operation, label)
        IconButton { objectName: "resetPrimaryGrade"; iconName: "rotate-ccw"; text: qsTr("Reset all colour wheels"); tip: qsTr("Puts all four wheels back to neutral. Other colour settings keep their values."); enabled: root.params.length > 0; onClicked: root.resetWheels() }
    }
    Row {
        visible: !root.narrow && root.open
        x: Theme.s3; spacing: Theme.s2
        TabButton { text: qsTr("Four wheels"); uppercase: false; checked: root.overview; onClicked: root.view = 0 }
        TabButton { text: qsTr("Single wheel"); uppercase: false; checked: !root.overview; onClicked: if (root.view === 0) root.view = 1 }
    }
    DockHeader {
        objectName: "colourBalanceTabs"
        visible: !root.overview && root.open; width: parent.width
        tabs: root.titles; tips: root.titleTips; currentIndex: root.activeZone
        onActivated: i => root.view = i+1
    }
    Grid {
        visible: root.overview && root.open
        x: Theme.s3; width: parent.width - Theme.s3*2
        columns: 2; spacing: Theme.s3
        Repeater {
            model: 4
            ColourBalanceZone {
                required property int index
                width: (parent.width-Theme.s3)/2
                zone: root.zones[index]; title: root.titles[index]; params: root.params; chromaRange: index === 0 ? .12 : index === 3 ? .08 : .25; lumaRange: index === 0 ? .25 : .1; compact: true
                onEnlargeRequested: root.view = index + 1
            }
        }
    }
    Repeater {
        model: 4
        ColourBalanceZone {
            required property int index
            visible: root.open && !root.overview && root.activeZone === index
            x: Theme.s3; width: root.width - Theme.s3*2
            zone: root.zones[index]; title: root.titles[index]; params: root.params
            chromaRange: index === 0 ? .12 : index === 3 ? .08 : .25; lumaRange: index === 0 ? .25 : .1
        }
    }
    // Lum Mix: at 100 a colour push also moves brightness; lower
    // it and the pucks change colour only, the levels still set brightness.
    readonly property var lumMixRow: params.find(p => p.field === "lum_mix")
    EditCoalescer { id: lumMixSend; onSend: v => engine.setParam("omarawgrade", "lum_mix", v) }
    SliderField {
        objectName: "lumMix"
        visible: root.open && root.lumMixRow !== undefined
        x: Theme.s3; width: parent.width - Theme.s3*2
        label: qsTr("Lum Mix"); labelWidth: 72
        from: 0; to: 100; origin: 100; decimals: 0
        value: root.lumMixRow ? root.lumMixRow.value : 100
        resetOnDoubleClick: true; wheelEnabled: true
        tip: qsTr("At 100 a colour push on a wheel also brightens or darkens, Lower it and the colour pushes keep the brightness the levels give; at 0 they change colour only. Double-click resets it.")
        onEdited: v => lumMixSend.push(v)
        onEditingFinished: v => lumMixSend.flush(v)
    }
    Text {
        visible: root.open
        x: Theme.s3; width: parent.width - Theme.s3*2
        text: qsTr("Shift: fine control · Ctrl: lock hue. Double-click a wheel or slider to reset it.")
        color: Theme.textMuted; wrapMode: Text.Wrap
        font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
    }
    Item { width: 1; height: Theme.s2 }
}
