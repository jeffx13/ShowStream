import QtQuick
import QtQuick.Controls 2.15
import "../components"
Item {
    id: explorerPage

    SearchBar {
        id:searchBar
        anchors{
            left: parent.left
            right: parent.right
            top:parent.top
            topMargin: 5
        }
        focus: true
        height: 40
    }

    LoadingScreen {
        id:loadingScreen
        anchors.centerIn: parent
        loading: (app.explorer.isLoading || app.currentShow.isLoading) && explorerPage.visible
        onCancelled: {
            app.explorer.cancel()
            app.cancel()
        }
        onTimedOut: {
            notifier.headerText = "Error"
            notifier.text = "Operation took too long"
            notifier.open()
        }
    }

    Dialog {
        id: notifier
        modal: true
        width: parent.width / 3
        height: parent.height / 4
        anchors.centerIn: parent
        focus: false
        property alias headerText:headerText.text
        property alias text:notifierMessage.text

        contentItem: Rectangle {
            color: "#f2f2f2"
            border.color: "#c2c2c2"
            border.width: 1
            radius: 10
            anchors.centerIn: parent
            Text {
                id:headerText
                text: "Error"
                font.pointSize: 16
                font.bold: true
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: 20
            }
            Text {
                id: notifierMessage
                text: "An error has occurred."
                font.pointSize: 14
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.verticalCenter: parent.verticalCenter
            }
            Button {
                text: "OK"
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 10
                anchors.horizontalCenter: parent.horizontalCenter
                onClicked: notifier.close()
            }
        }
    }



    SearchResultsGridView {
        id:showGridView
        model: app.explorer
        focus: false
        anchors {
            top: searchBar.bottom
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }

        Component.onCompleted: {
            contentY = root.searchResultsViewlastScrollY
            forceActiveFocus()
        }
    }

    Keys.enabled: true
    Keys.onPressed: event => handleKeyPress(event)
    function handleKeyPress(event){
        if (event.modifiers & Qt.ControlModifier){
            if (event.key === Qt.Key_R){app.explorer.reload()}
        }else{
            switch (event.key){
            case Qt.Key_Escape:
            case Qt.Key_Alt:
                if (searchBar.textField.activeFocus)
                    explorerPage.forceActiveFocus()
                break;
            case Qt.Key_Tab:
                app.cycleProviders()
                event.accepted = true
                break;
            case Qt.Key_Enter:
                searchBar.search()
                break;
            case Qt.Key_Slash:
                searchBar.textField.forceActiveFocus()
                break;
            case Qt.Key_P:
                app.popular(1)
                break;
            case Qt.Key_L:
                app.latest(1)
                break;
            case Qt.Key_Up:
                showGridView.flick(0,500)
                break;
            case Qt.Key_Down:
                showGridView.flick(0,-500)
                break;

            }
        }

    }
}
