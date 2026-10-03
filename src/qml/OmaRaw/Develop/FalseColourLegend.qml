pragma ComponentBehavior: Bound
import QtQuick
import OmaRaw.Ui

// The false colour scope's graph: one column per band, as tall as the band's
// share of the picture, over a strip of the bands from black to white. The
// bands left grey in the picture are grey here. Pointing at a column names it.
Item {
    id: root
    objectName: "falseColourLegend"
    property var bands: []
    readonly property real tallest: Math.max(1, ...bands.map(b => b.share))
    // The band under the pointer, or -1.
    property int pointed: -1
    Accessible.role: Accessible.Graphic
    Accessible.name: qsTr("False colour: share of the picture in each brightness band")

    function greyOf(band) { const v = Math.round((band.from + band.to) / 2 * 2.55); return Qt.rgba(v / 255, v / 255, v / 255, 1) }
    function colourOf(band) { return band.colour !== "" ? band.colour : root.greyOf(band) }
    function rangeOf(band) { return band.lost ? "" : qsTr("%1–%2 %").arg(band.from).arg(band.to) }

    Row {
        id: columns
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        anchors.bottom: caption.top; anchors.bottomMargin: Theme.s1
        spacing: 2
        Repeater {
            model: root.bands
            Item {
                id: column
                required property int index
                required property var modelData
                objectName: "falseColourBand_" + index
                width: (columns.width - columns.spacing * (root.bands.length - 1)) / Math.max(1, root.bands.length)
                height: columns.height
                readonly property real share: modelData.share
                Rectangle {
                    anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: strip.top; anchors.bottomMargin: 2
                    height: Math.max(column.share > 0 ? 2 : 0, (parent.height - strip.height - 2) * column.share / root.tallest)
                    color: root.colourOf(column.modelData)
                    opacity: root.pointed < 0 || root.pointed === column.index ? 1 : 0.45
                }
                Rectangle {
                    id: strip
                    anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                    height: 6
                    color: root.colourOf(column.modelData)
                    border.width: column.modelData.colour === "" ? Theme.hairline : 0; border.color: Theme.border
                }
                HoverHandler { onHoveredChanged: root.pointed = hovered ? column.index : (root.pointed === column.index ? -1 : root.pointed) }
            }
        }
    }
    Text {
        id: caption
        objectName: "falseColourCaption"
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        elide: Text.ElideRight
        readonly property var band: root.pointed >= 0 && root.pointed < root.bands.length ? root.bands[root.pointed] : null
        text: band ? [band.name, root.rangeOf(band), qsTr("%1 % of the picture").arg(band.share.toFixed(1))].filter(t => t !== "").join(" · ")
                   : qsTr("False colour · point at a band")
        font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textMuted
    }
}
