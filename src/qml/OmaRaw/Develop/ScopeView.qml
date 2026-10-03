pragma ComponentBehavior: Bound
import QtQuick
import OmaRaw.Ui
import OmaRaw.Library

// The compact and enlarged views share the same scope data and presentation.
Item {
    id: root
    property bool large: false
    readonly property var labels: [qsTr("Histogram"), qsTr("Waveform"), qsTr("RGB parade"), qsTr("Vectorscope"), qsTr("False colour")]
    readonly property real labelHeight: engine.scopeMode === 2 ? Theme.fsLabel + Theme.s1 : 0
    readonly property real guideHeight: engine.scopeMode === 3 ? Theme.fsLabel + Theme.s1 : 0
    implicitHeight: 96 + Theme.s3 * 2 + labelHeight + guideHeight
    Row {
        id: channels
        objectName: root.large ? "largeParadeChannelLabels" : "paradeChannelLabels"
        visible: engine.scopeMode === 2
        x: Theme.s3; y: Theme.s2; width: parent.width - Theme.s3 * 2
        height: root.labelHeight
        Repeater {
            model: [qsTr("Red"), qsTr("Green"), qsTr("Blue")]
            Text {
                required property int index
                required property string modelData
                width: channels.width / 3; text: modelData
                horizontalAlignment: Text.AlignHCenter
                font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel
                color: [Theme.channelRed, Theme.channelGreen, Theme.channelBlue][index]
            }
        }
    }
    HistogramView {
        objectName: root.large ? "largeScopeHistogram" : "scopeHistogram"
        visible: engine.scopeMode === 0
        anchors.fill: parent; anchors.margins: Theme.s3
        bins: engine.histogram
        source: qsTr("the render after the output transform")
    }
    FalseColourLegend {
        objectName: root.large ? "largeFalseColourLegend" : "falseColourLegend"
        visible: engine.falseColour
        anchors.fill: parent; anchors.margins: Theme.s3
        bands: engine.falseColourBands
    }
    Image {
        objectName: root.large ? "largeScopeImage" : "scopeImage"
        anchors.fill: parent; anchors.margins: Theme.s3
        anchors.topMargin: Theme.s3 + root.labelHeight
        anchors.bottomMargin: Theme.s3 + root.guideHeight
        visible: engine.scopeMode !== 0 && !engine.falseColour
        source: engine.scopeSource; cache: false
        fillMode: engine.scopeMode === 3 ? Image.PreserveAspectFit : Image.Stretch
        Accessible.role: Accessible.Graphic
        Accessible.name: root.labels[engine.scopeMode]
        Accessible.description: engine.scopeMode === 3 ? qsTr("BT.709 colour targets at 75 percent and a dashed skin-tone reference line.") : ""
    }
    Text {
        objectName: root.large ? "largeVectorGuideCaption" : "vectorGuideCaption"
        visible: engine.scopeMode === 3
        anchors.left: parent.left; anchors.right: parent.right
        anchors.bottom: parent.bottom; anchors.bottomMargin: Theme.s1
        text: qsTr("75% targets · Skin reference")
        horizontalAlignment: Text.AlignHCenter
        font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
    }
}
