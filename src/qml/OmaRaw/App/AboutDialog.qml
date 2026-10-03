pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic as C
import OmaRaw.Ui

// About OmaRAW: version, the licence and its warranty terms, and what the
// program is built on. The Licences button opens the installed notices.
C.Popup {
    id: root
    objectName: "aboutDialog"
    modal: true
    parent: C.Overlay.overlay
    anchors.centerIn: parent
    width: 560
    padding: Theme.s5
    background: Rectangle { color: Theme.panelRaised; border.width: Theme.hairline; border.color: Theme.borderStrong; radius: Theme.rMenu }

    Column {
        id: contents
        width: parent.width
        spacing: Theme.s3
        Text {
            objectName: "aboutTitle"
            text: qsTr("OmaRAW %1").arg(backend.version)
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsHeading; font.weight: Theme.wHeading; color: Theme.textPrimary
        }
        Text {
            width: parent.width; wrapMode: Text.Wrap
            text: qsTr("Photography library, RAW development, tethered capture and output for Linux.")
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textSecondary
        }
        Text {
            objectName: "aboutLicence"
            width: parent.width; wrapMode: Text.Wrap
            text: qsTr("Copyright © 2026 Gavin Nugent.\n\nThis program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.\n\nThis program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.")
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textPrimary
        }
        Text {
            width: parent.width; wrapMode: Text.Wrap
            text: engine.available
                  ? qsTr("Built on darktable %1 (GPL-3.0-or-later) with rawspeed and LibRaw, OpenColorIO, libplacebo and Vulkan, Qt 6 and Lucide icons. The licences of every component are installed with the program.").arg(engine.version)
                  : qsTr("Built on darktable (GPL-3.0-or-later) with rawspeed and LibRaw, OpenColorIO, libplacebo and Vulkan, Qt 6 and Lucide icons. The licences of every component are installed with the program.")
            font.family: Theme.fontFamily; font.pixelSize: Theme.fsLabel; color: Theme.textSecondary
        }
        Row {
            spacing: Theme.s2
            ToolButton { objectName: "aboutLicences"; iconName: "file"; text: qsTr("Licences"); showLabel: true; onClicked: backend.revealLicences() }
            ToolButton { iconName: "x"; text: qsTr("Close"); showLabel: true; onClicked: root.close() }
        }
    }
}
