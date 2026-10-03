import QtQuick
import QtQml.Models

// Read destinations when opened so album creation, renaming and catalogue
// switches are reflected without rebuilding an action while it is triggering.
ContextMenu {
    id: root
    title: qsTr("Add to Album")
    property string tip: qsTr("Add the selected photographs to an existing album.")
    enabled: backend.selectedCount > 0
    implicitWidth: 300
    property var destinations: []

    onAboutToShow: {
        const albums = backend.albums().filter(album => album.name !== "__quick")
        const paths = {}
        destinations = albums.map(album => {
            const path = paths[album.parent] ? paths[album.parent] + " / " + album.name : album.name
            paths[album.id] = path
            return { albumId: album.id, label: path }
        })
    }

    Instantiator {
        model: root.destinations
        delegate: MenuAction {
            required property var modelData
            objectName: "addToAlbum_" + modelData.albumId
            text: modelData.label
            iconName: "book-image"
            onTriggered: backend.addSelectionToAlbum(modelData.albumId)
        }
        onObjectAdded: (index, object) => root.insertItem(index, object)
        onObjectRemoved: (index, object) => root.removeItem(object)
    }
    MenuAction {
        text: qsTr("No albums yet")
        enabled: false
        visible: root.destinations.length === 0
    }
}
