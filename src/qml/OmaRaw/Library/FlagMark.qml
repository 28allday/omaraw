import QtQuick
import OmaRaw.Ui

// Pick (green flag) or reject (red x). Nothing for unflagged.
Icon {
    property int flag: 0
    visible: flag !== 0
    name: flag > 0 ? "flag" : "x"
    color: flag > 0 ? Theme.pick : Theme.reject
    size: 12
}
