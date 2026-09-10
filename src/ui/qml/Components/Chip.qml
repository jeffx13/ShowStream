import QtQuick
import ".."

Rectangle {
    id: chip

    property alias label: chipLabel.text
    property bool filled: false

    implicitWidth: chipLabel.implicitWidth + 12
    implicitHeight: 20
    radius: 5
    color: filled ? Theme.accent : Qt.alpha(Theme.accent, 0.16)
    border.color: Qt.alpha(Theme.accent, 0.45)
    border.width: 1

    Text {
        id: chipLabel
        anchors.centerIn: parent
        color: chip.filled ? Theme.onColor(Theme.accent) : Theme.onOverlayAccent
        font.pixelSize: Globals.sp(13)
        font.bold: true
    }
}
