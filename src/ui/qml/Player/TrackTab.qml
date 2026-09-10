pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../Components"
import AoNami
import ".."

Item {
    id: root
    required property MpvPlayer player
    property string kind: "video"        // "video" | "audio" | "subs"
    property var    trackModel: null

    readonly property bool isSubs: kind === "subs"

    function centerCurrent() {
        if (listView.model) listView.positionViewAtIndex(listView.currentIndex, ListView.Center)
    }

    PanelListView {
        id: listView
        anchors { fill: parent; margins: 8 }
        emptyText: qsTr("No items")

        model: root.trackModel
        currentIndex: model ? model.currentIndex : -1

        header: Item {
            width: listView.width
            height: root.isSubs ? 40 : 0
            visible: root.isSubs

            Rectangle {
                id: offRow
                readonly property bool active: root.player.primarySubId === 0
                                               && root.player.secondarySubId === 0
                anchors { fill: parent; bottomMargin: 4 }
                radius: 10
                color: active            ? Theme.accentSoft
                     : offHover.hovered  ? Theme.overlayLine : "transparent"

                Text {
                    anchors { verticalCenter: parent.verticalCenter; left: parent.left; leftMargin: 14 }
                    text: qsTr("Off")
                    color: offRow.active ? Theme.accent : Theme.onOverlayMuted
                    font.pixelSize: Globals.sp(19)
                    font.bold: offRow.active
                }
                AppIcon {
                    anchors { verticalCenter: parent.verticalCenter; right: parent.right; rightMargin: 12 }
                    visible: offRow.active
                    name: "check"
                    size: 16
                    color: Theme.accent
                }
                HoverHandler { id: offHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: root.player.clearSubs() }
            }
        }

        onModelChanged: if (model) Qt.callLater(root.centerCurrent)

        delegate: AbstractButton {
            id: trackBtn
            required property string name
            required property int index
            readonly property int  primary:   root.isSubs && listView.model ? listView.model.currentIndex : -1
            readonly property int  secondary: root.isSubs && listView.model ? listView.model.secondaryIndex : -1
            readonly property int  slotNumber: index === primary   ? 1
                                             : index === secondary ? 2 : 0
            readonly property bool isCurrent: root.isSubs ? slotNumber > 0 : index === listView.currentIndex

            width: listView.width
            height: 44
            focusPolicy: Qt.NoFocus

            onClicked: {
                switch (root.kind) {
                case "video": root.player.setVideoIndex(index); break
                case "audio": root.player.setAudioIndex(index); break
                case "subs":  trackBtn.slotNumber === 1 ? root.player.setPrimarySub(0)
                                                        : root.player.setSubIndex(index); break
                }
            }

            background: PanelRow {
                current: trackBtn.isCurrent
                hovered: trackBtn.hovered
            }

            contentItem: RowLayout {
                spacing: 10

                Chip {
                    visible: root.isSubs && trackBtn.hovered && trackBtn.slotNumber !== 2
                    Layout.leftMargin: 4
                    label: "2"
                    filled: secondHover.hovered
                    HoverHandler { id: secondHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: root.player.setSubIndex(trackBtn.index, true) }
                    AppToolTip { text: qsTr("Use as second subtitle"); visible: secondHover.hovered }
                }

                Rectangle {
                    visible: root.isSubs && trackBtn.isCurrent
                    Layout.leftMargin: 4
                    Layout.preferredWidth: 18
                    Layout.preferredHeight: 18
                    radius: 5
                    color: Qt.alpha(Theme.accent, 0.22)
                    border.color: Theme.accent
                    border.width: 1
                    Text {
                        anchors.centerIn: parent
                        text: trackBtn.slotNumber === 2 ? "2" : "1"
                        color: Theme.accent
                        font.pixelSize: Globals.sp(13)
                        font.bold: true
                    }
                }

                Item {
                    Layout.leftMargin: 10
                    Layout.preferredWidth: 16
                    Layout.preferredHeight: 16

                    Rectangle {
                        anchors.centerIn: parent
                        width: 8
                        height: 8
                        radius: 4
                        color: trackBtn.isCurrent ? Theme.success : Theme.onOverlayFaint
                    }

                    Rectangle {
                        visible: trackBtn.isCurrent
                        anchors.centerIn: parent
                        width: 16
                        height: 16
                        radius: 8
                        color: Theme.successSoft
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: trackBtn.name
                    font.pixelSize: Globals.sp(20)
                    elide: Text.ElideRight
                    color: trackBtn.isCurrent || trackBtn.hovered ? Theme.onOverlay : Theme.onOverlayDim
                }

                AppIcon {
                    visible: trackBtn.isCurrent
                    name: "check"
                    size: 18
                    color: Theme.success
                    Layout.rightMargin: 10
                }
            }

            scale: trackBtn.down ? 0.97 : 1.0
            Behavior on scale { NumberAnimation { duration: 80 } }
        }
    }
}
