pragma ComponentBehavior: Bound
import QtQuick
import OmaRaw.Ui

// Five stars. Read-only unless `interactive`; click a star to set, click the
// current star again to clear.
Row {
    id: root
    property int rating: 0
    property bool interactive: false
    property int size: 11
    property color onColor: Theme.star
    property color offColor: Theme.starOff
    signal rated(int rating)
    spacing: 1
    Accessible.role: interactive ? Accessible.Slider : Accessible.StaticText
    Accessible.name: qsTr("Rating")
    Accessible.description: rating === 0 ? qsTr("Unrated") : qsTr("%1 of 5 stars").arg(rating)
    Repeater {
        model: 5
        Icon {
            id: star
            required property int index
            name: "star"
            size: root.size
            color: index < root.rating ? root.onColor : root.offColor
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("%1 star%2").arg(index + 1).arg(index === 0 ? "" : "s")
            Accessible.ignored: !root.interactive
            TapHandler {
                enabled: root.interactive
                onTapped: root.rated(parent.index + 1 === root.rating ? 0 : parent.index + 1)
            }
            HoverHandler { id: starHover; enabled: root.interactive }
            Tooltip {
                text: qsTr("%1 star%2").arg(star.index + 1).arg(star.index === 0 ? "" : "s")
                shortcut: String(star.index + 1)
                description: star.index + 1 === root.rating ? qsTr("Click again to clear the rating.") : qsTr("Rates the photo; 0 clears it.")
                visible: starHover.hovered
            }
        }
    }
}
