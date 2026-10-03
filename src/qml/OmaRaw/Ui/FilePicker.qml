// Keep file and folder browsing on the same controls and surface.
PathPicker {
    fileMode: PathPicker.OpenFile
    title: saveMode ? qsTr("Save a file") : qsTr("Choose a file")
}
