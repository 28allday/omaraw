import QtQuick

// One icon family, one geometry, one stroke weight.
//
// The colour is baked into a generated SVG data URI rather than applied as a
// shader tint. MultiEffect would be the obvious way to recolour a shared
// asset, but it renders nothing under the offscreen backend used for
// screenshots and tests — and an icon that silently disappears in CI is worse
// than a few extra rasterisations. Qt caches by source URL, so each
// (icon, colour) pair is rasterised once for the life of the process.
Item {
    id: root

    // Lucide icon name, e.g. "scissors". No path, no extension.
    property string name: ""
    property color color: Theme.textSecondary
    property int size: Theme.szIcon

    implicitWidth: size
    implicitHeight: size

    // Lucide draws on a 24 grid at stroke 2, which would render as a 1.33 px
    // stroke at 16 px. 2.25 on that grid is the 1.5 px the brief asks for.
    readonly property real strokeWidth: 2.25

    readonly property string shape: Icons.shapes[name] !== undefined
                                    ? Icons.shapes[name] : ""

    Image {
        anchors.fill: parent
        visible: root.shape !== ""
        smooth: true
        fillMode: Image.PreserveAspectFit

        // Rasterise at device pixels so strokes stay crisp on a hidpi output.
        readonly property real dpr: Screen.devicePixelRatio > 0
                                    ? Screen.devicePixelRatio : 1
        sourceSize.width: root.size * dpr
        sourceSize.height: root.size * dpr

        source: root.shape === "" ? "" :
            "data:image/svg+xml;utf8," + encodeURIComponent(
                '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24"'
                + ' fill="none" stroke="' + root.color
                + '" stroke-width="' + root.strokeWidth
                + '" stroke-linecap="round" stroke-linejoin="round">'
                + root.shape + '</svg>')
    }
}
