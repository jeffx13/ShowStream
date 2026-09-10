pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import AoNami
import QtQuick.Layouts
import "../Components"
import ".."

Item {
    Component.onCompleted: logList.positionViewAtEnd()

    HoverHandler {
        cursorShape: Qt.ArrowCursor
    }
    Card {
        id: topBar
        height: Math.max(44, parent.height * 0.07)
        radius: 10
        anchors { top: parent.top; left: parent.left; right: parent.right; margins: 10 }

        RowLayout {
            anchors.fill: parent; anchors.margins: 8; spacing: 8

            Text { text: "Logs"; color: Theme.textPrimary; font.pixelSize: Globals.sp(24); Layout.fillWidth: true; verticalAlignment: Text.AlignVCenter; Layout.fillHeight: true }

            Text {
                text: logList.count + " entries"
                color: Theme.textMuted; font.pixelSize: Globals.sp(20)
                verticalAlignment: Text.AlignVCenter
            }

            AppButton {
                text: "Clear"; fontSize: 20; radius: 8
                secondary: true
                Layout.preferredWidth: 70; Layout.fillHeight: true
                onClicked: App.logList.clear()
            }
        }
    }

    ListView {
        id: logList
        anchors { left: parent.left; right: parent.right; top: topBar.bottom; bottom: parent.bottom; margins: 10; topMargin: 6 }
        model: App.logList; clip: true; spacing: 4
        boundsBehavior: Flickable.StopAtBounds
        cacheBuffer: 10000

        ScrollBar.vertical: AppScrollBar {
            parent: logList.parent
            anchors { top: logList.top; left: logList.right; bottom: logList.bottom }
            width: 6
            barColor: Theme.textMuted
        }

        onCountChanged: {
            if (atYEnd || contentY >= contentHeight - height - 100) positionViewAtEnd()
        }

        delegate: Item {
            id: logItem
            width: logList.width
            implicitHeight: card.implicitHeight
            required property string message
            required property string time
            required property string type
            required property string colour

            Rectangle {
                id: card
                width: parent.width; radius: 8
                color: Theme.surface; border.color: Theme.border; border.width: 1
                implicitHeight: col.implicitHeight + 16

                ColumnLayout {
                    id: col
                    anchors { fill: parent; margins: 8 }
                    spacing: 4

                    RowLayout {
                        spacing: 8
                        Text { text: logItem.time; color: Theme.textMuted; font.pixelSize: Globals.sp(20) }
                        Rectangle {
                            radius: 4; color: logItem.colour
                            implicitWidth: badge.implicitWidth + 10; implicitHeight: 18
                            Text { id: badge; anchors.centerIn: parent; text: logItem.type; color: "#fff"; font.pixelSize: Globals.sp(20) }
                        }
                    }

                    RichText {
                        Layout.fillWidth: true
                        text: logItem.message
                        color: logItem.colour
                    }
                }
            }
        }
    }
}