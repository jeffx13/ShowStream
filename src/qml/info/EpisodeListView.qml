pragma ComponentBehavior: Bound
import QtQuick 2.15
// import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../components"
import Kyokou.App.Main
ListView {
    id:episodeListView

    clip: true
    model: App.currentShow.episodeList
    property real lastWatchedIndex: App.currentShow.episodeList.reversed ? episodeListView.count - 1 - App.currentShow.lastWatchedIndex : App.currentShow.lastWatchedIndex
    function correctIndex(index) {
        return App.currentShow.episodeList.reversed ? episodeListView.count - 1 - index : index
    }
    Component.onCompleted: {
        if (App.currentShow.lastWatchedIndex !== -1)
        episodeListView.positionViewAtIndex(lastWatchedIndex, ListView.Center)
    }

    boundsMovement: Flickable.StopAtBounds

    delegate: Rectangle {
        id: delegateRect
        width: episodeListView.width
        height: 60
        border.width: 2
        border.color: "black"
        property bool isCurrent: episodeListView.lastWatchedIndex === index
        color: isCurrent ? "red" : "black"

        required property string fullTitle
        required property int index

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            onEntered: delegateRect.border.color = "white"
            onExited: delegateRect.border.color = Qt.binding(function() { return delegateRect.color })
            onClicked:{
                App.playFromEpisodeList(correctIndex(index))
            }
        }
        RowLayout {
            anchors{
                left:parent.left
                right:parent.right
                top:parent.top
                bottom: parent.bottom
                margins: 3
            }
            Text {
                id: episodeStr
                text:  delegateRect.fullTitle
                font.pixelSize: 20 * root.fontSizeMultiplier
                Layout.fillHeight: true
                Layout.fillWidth: true
                Layout.preferredWidth: 8
                elide: Text.ElideRight
                wrapMode: Text.Wrap
                color: "white"
            }


            ImageButton {
                id: setWatchedButton
                source: "qrc:/resources/images/tv.png"
                Layout.fillHeight: true
                Layout.preferredWidth: 3
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                visible: !delegateRect.isCurrent
                onClicked: {
                    App.currentShow.lastWatchedIndex = correctIndex(index)
                }
            }
            Item {
                Layout.fillHeight: true
                Layout.preferredWidth: 3
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                visible: !setWatchedButton.visible
            }


            ImageButton {
                source: "qrc:/resources/images/download-button.png"
                Layout.fillHeight: true
                Layout.fillWidth: true
                Layout.preferredWidth: 3
                // Layout.preferredHeight: implicitWidth
                Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                onClicked: {
                    enabled = false;
                    source = "qrc:/resources/images/download_selected.png"
                    let correctedIndex = episodeListView.correctIndex(delegateRect.index)
                    App.downloadCurrentShow(correctedIndex, correctedIndex)
                }
            }

        }



    }

}

