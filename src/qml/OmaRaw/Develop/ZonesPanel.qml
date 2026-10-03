pragma ComponentBehavior: Bound
import QtQuick
import OmaRaw.Ui

// HSL by hue band: eight named bands, each with hue, saturation and
// luminance, over the engine's colour zones module selecting by hue.
Column {
    id: root
    property bool embedded: false
    property bool saveEnabled: true
    signal savePresetRequested(string operation, string label)
    readonly property var zones: engine.zones || ({})
    readonly property bool found: zones.found === true
    readonly property bool moduleOn: zones.enabled === true
    readonly property var bands: zones.bands || []
    StableList { id: bandList; source: root.bands; key: b => b.name }
    // Tab index 0 hue, 1 saturation, 2 luminance → engine channel 2, 1, 0
    property int tab: 0
    readonly property var channelOf: [2, 1, 0]
    readonly property var keyOf: ["shift", "sat", "lum"]
    readonly property int channel: channelOf[tab]
    readonly property string key: keyOf[tab]
    // Folded to its heading until asked for.
    property bool open: false
    spacing: Theme.s1
    visible: found

    BlockHeading {
        visible: !root.embedded
        height: visible ? Theme.hRow + Theme.s1 : 0
        text: qsTr("Colour mixer"); tip: qsTr("Hue, saturation and luminance for each band of colour: make the sky deeper or the grass less yellow without touching the rest.")
        collapsible: true; open: root.open; onToggled: root.open = !root.open
        operation: root.found ? "colorzones" : ""; on: root.moduleOn; saveEnabled: root.saveEnabled
        onEyeClicked: engine.setModuleEnabled("colorzones", !root.moduleOn)
        onSavePresetRequested: (operation, label) => root.savePresetRequested(operation, label)
        IconButton { iconName: "rotate-ccw"; text: qsTr("Reset every band"); tip: qsTr("Puts every colour band's hue, saturation and luminance back to zero."); onClicked: engine.resetZones() }
    }
    Column {
        visible: root.open || height > 0
        width: parent.width; height: root.open ? implicitHeight : 0
        // Clipped while it folds, so it does not paint over the lines below.
        clip: mixerFold.running
        Behavior on height { NumberAnimation { id: mixerFold; duration: Theme.dSlow; easing.type: Theme.easing } }
        spacing: Theme.s1
        Item {
            width: parent.width; height: Theme.hControl
            SegmentedControl {
                anchors.left: parent.left; anchors.leftMargin: Theme.s3
                anchors.verticalCenter: parent.verticalCenter
                labels: [qsTr("Hue"), qsTr("Saturation"), qsTr("Luminance")]
                tips: [qsTr("Shift each band of colour towards its neighbours."), qsTr("Make each band of colour stronger or weaker."), qsTr("Make each band of colour lighter or darker.")]
                currentIndex: root.tab
                onActivated: i => root.tab = i
            }
        }
        Repeater {
            model: bandList.model
            Item {
                id: row
                required property int index
                required property var modelData
                readonly property var live: root.bands[index] || modelData
                width: parent.width; height: Theme.hControl + Theme.s1
                Rectangle {
                    anchors.left: parent.left; anchors.leftMargin: Theme.s3
                    anchors.verticalCenter: parent.verticalCenter
                    width: 10; height: 10; radius: 5
                    color: row.live.colour || Theme.textMuted
                }
                EditCoalescer {
                    id: throttle
                    onSend: v => engine.setZone(row.index, root.channel, v)
                }
                SliderField {
                    anchors.left: parent.left; anchors.leftMargin: Theme.s3 + 18
                    anchors.right: parent.right; anchors.rightMargin: Theme.s3
                    anchors.verticalCenter: parent.verticalCenter
                    label: row.live.name || ""
                    tip: root.tab === 0 ? qsTr("Shifts this colour's hue; right-click resets it.") : root.tab === 1 ? qsTr("Strengthens or weakens this colour; right-click resets it.") : qsTr("Lightens or darkens this colour; right-click resets it.")
                    labelWidth: 78
                    from: -100; to: 100; origin: 0; decimals: 0
                    value: row.live[root.key] !== undefined ? row.live[root.key] : 0
                    opacity: root.moduleOn ? 1 : 0.7
                    onEdited: v => throttle.push(v)
                    onEditingFinished: v => throttle.flush(v)
                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: engine.setZone(row.index, root.channel, 0)
                    }
                }
            }
        }
        Item { width: parent.width; height: Theme.s2 }
    }
}
