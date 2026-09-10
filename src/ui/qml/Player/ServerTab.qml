pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../Components"
import AoNami
import ".."

Item {
    id: root

    function centerCurrent() {
        if (serverListView.model)
            serverListView.positionViewAtIndex(serverListView.currentIndex, ListView.Center)
    }

    PanelListView {
        id: serverListView
        anchors { fill: parent; margins: 8 }
        emptyText: qsTr("No servers")
        model: App.playlist.serverList
        currentIndex: model ? model.currentIndex : -1

        onCountChanged: if (model) Qt.callLater(root.centerCurrent)

        // "" means no header.
        section.property: "section"
        section.criteria: ViewSection.FullString
        section.delegate: Item {
            required property string section
            width: serverListView.width
            height: section === "" ? 0 : 26
            visible: section !== ""
            Text {
                anchors { left: parent.left; leftMargin: 12; verticalCenter: parent.verticalCenter }
                text: parent.section
                color: parent.section === "Broken" ? Theme.danger : Theme.onOverlayAccent
                font { pixelSize: Globals.sp(15); weight: Font.DemiBold; letterSpacing: 1 }
            }
        }

        delegate: AbstractButton {
            id: srvBtn
            required property string name
            required property int index
            required property int status
            readonly property bool isCurrent: index === serverListView.currentIndex
            readonly property bool isBroken: status === ServerListModel.Broken

            width: serverListView.width
            height: 44
            focusPolicy: Qt.NoFocus
            // Still clickable: the only way back from a wrong broken verdict.
            opacity: isBroken ? 0.5 : 1.0
            onClicked: App.playlist.loadServer(index)

            background: PanelRow {
                current: srvBtn.isCurrent
                hovered: srvBtn.hovered
            }

            contentItem: RowLayout {
                spacing: 10
                Item {
                    Layout.leftMargin: 10
                    Layout.preferredWidth: 16
                    Layout.preferredHeight: 16
                    Rectangle {
                        anchors.centerIn: parent
                        // Not Layout.*: the parent is a plain Item, so those leave the dot 0px wide.
                        width: 8; height: 8; radius: 4
                        color: (srvBtn.isCurrent || srvBtn.status === ServerListModel.Working) ? Theme.success
                             : srvBtn.isBroken ? Theme.danger
                             : Theme.onOverlayFaint
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: srvBtn.name
                    font.pixelSize: Globals.sp(20)
                    elide: Text.ElideRight
                    color: srvBtn.isCurrent ? Theme.onOverlay
                         : srvBtn.isBroken  ? Theme.onOverlayFaint
                         : srvBtn.hovered   ? Theme.onOverlay
                         : Theme.onOverlayDim
                }
                AppIcon {
                    visible: srvBtn.isCurrent || srvBtn.isBroken
                    name: srvBtn.isCurrent ? "check" : "x"
                    size: 16
                    color: srvBtn.isCurrent ? Theme.success : Theme.danger
                    Layout.rightMargin: 10
                }
            }

            scale: srvBtn.down ? 0.97 : 1.0
            Behavior on scale { NumberAnimation { duration: 80 } }
        }
    }
}
