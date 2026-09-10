import QtQuick
import QtQuick.Controls
import ".."

ListView {
    id: view

    property string emptyText: ""

    clip: true
    boundsBehavior: Flickable.StopAtBounds
    spacing: 2

    ScrollBar.vertical: AppScrollBar { width: 4; barOpacity: 0.4; showTrack: false }

    Text {
        anchors.centerIn: parent
        visible: view.count === 0
        text: view.emptyText
        color: Theme.onOverlayDim
        font.pixelSize: Globals.sp(20)
    }
}
