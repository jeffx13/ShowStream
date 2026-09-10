pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."
import AoNami

AppPopup {
    id: dialog
    modal: true
    focus: true
    padding: 18
    clip: true
    closePolicy: Popup.CloseOnEscape
    backgroundRadius: 14

    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    width:  Math.min(640, parent ? parent.width  - 60 : 640)
    height: Math.min(660, parent ? parent.height - 60 : 660)

    property int    libraryIndex: -1
    property string showTitle: ""
    property string currentProvider: ""
    property int    resumePrefill: 1

    readonly property int rowHeight: 34

    function openFor(index) {
        const e = App.library.entryAt(index)
        dialog.libraryIndex = index
        dialog.showTitle = e.title || ""
        dialog.currentProvider = e.provider || ""
        dialog.resumePrefill = (e.lastWatchedIndex >= 0 ? e.lastWatchedIndex : 0) + 1
        resumeSpin.value = dialog.resumePrefill
        searchField.text = dialog.showTitle
        resultsView.currentIndex = -1
        App.migrateSearch.cancel()

        providerCombo.currentIndex = 0
        for (let i = 0; i < providerCombo.count; i++) {
            if (providerCombo.textAt(i) !== dialog.currentProvider) {
                providerCombo.currentIndex = i
                break
            }
        }
        dialog.open()
        dialog.runSearch()
    }

    function runSearch() {
        const q = searchField.text.trim()
        if (q.length === 0) return
        resultsView.currentIndex = -1
        App.searchOnProvider(providerCombo.currentText, q)
    }

    onOpened: searchField.forceActiveFocus()

    contentItem: ColumnLayout {
        spacing: 10

        Text {
            Layout.fillWidth: true
            text: "Migrate " + dialog.showTitle
            color: Theme.textPrimary
            font.pixelSize: Globals.sp(22)
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }

        Text {
            Layout.fillWidth: true
            Layout.bottomMargin: 2
            visible: dialog.currentProvider.length > 0
            text: "Currently on " + dialog.currentProvider + " · pick the same show on another provider"
            color: Theme.textMuted
            font.pixelSize: Globals.sp(16)
            elide: Text.ElideRight
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            // implicitHeight, not Layout.preferredHeight: a row can't shrink below its children.
            AppComboBox {
                id: providerCombo
                text: "text"
                fontSize: 16
                model: App.providers
                currentIndex: 0
                implicitHeight: dialog.rowHeight
                Layout.preferredWidth: 150
                onActivated: dialog.runSearch()
            }

            AppTextField {
                id: searchField
                placeholderText: "Search title…"
                showClearButton: true
                fontSize: 16
                implicitHeight: dialog.rowHeight
                Layout.fillWidth: true
                onAccepted: dialog.runSearch()
            }

            AppButton {
                text: "Search"
                fontSize: 16
                radius: 10
                leftPadding: 16
                rightPadding: 16
                implicitHeight: dialog.rowHeight
                onClicked: dialog.runSearch()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 320
            radius: 10
            color: Theme.surfaceDeep
            border.color: Theme.border
            border.width: 1
            clip: true

            ListView {
                id: resultsView
                anchors.fill: parent
                anchors.margins: 4
                model: App.migrateSearch
                spacing: 4
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollIndicator.vertical: ScrollIndicator {}
                currentIndex: -1

                delegate: ItemDelegate {
                    id: row
                    required property int index
                    required property string title
                    required property string cover
                    required property string latestTxt
                    width: ListView.view.width
                    height: 78
                    padding: 8

                    background: Rectangle {
                        radius: 8
                        color: row.ListView.isCurrentItem ? Qt.alpha(Theme.accent, 0.18)
                             : row.hovered ? Theme.surfaceAlt : "transparent"
                        border.color: row.ListView.isCurrentItem ? Theme.accent : "transparent"
                        border.width: 1
                        Behavior on color { ColorAnimation { duration: 80 } }
                    }

                    contentItem: RowLayout {
                        spacing: 10

                        CoverTile {
                            Layout.preferredWidth: 44
                            Layout.preferredHeight: 62
                            Layout.alignment: Qt.AlignVCenter
                            color: Theme.surface
                            source: row.cover
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignVCenter
                            spacing: 3
                            Text {
                                Layout.fillWidth: true
                                text: row.title
                                color: Theme.textPrimary
                                font.pixelSize: Globals.sp(19)
                                wrapMode: Text.Wrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                            }
                            Text {
                                visible: row.latestTxt.length > 0
                                text: row.latestTxt
                                color: Theme.textMuted
                                font.pixelSize: Globals.sp(16)
                            }
                        }

                        AppIcon {
                            visible: row.ListView.isCurrentItem
                            Layout.alignment: Qt.AlignVCenter
                            name: "check"
                            size: 18
                            color: Theme.accent
                        }
                    }

                    onClicked: resultsView.currentIndex = row.index
                }
            }

            AppSpinner {
                anchors.centerIn: parent
                visible: App.migrateSearch.isLoading
                running: visible
            }

            Text {
                anchors.centerIn: parent
                width: parent.width - 40
                visible: !App.migrateSearch.isLoading && App.migrateSearch.count === 0
                text: "Search a provider to find the matching show"
                color: Theme.textMuted
                font.pixelSize: Globals.sp(18)
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Text {
                visible: resultsView.currentIndex >= 0
                text: "Resume at episode"
                color: Theme.textSecondary
                font.pixelSize: Globals.sp(17)
                Layout.alignment: Qt.AlignVCenter
            }

            AppSpinBox {
                id: resumeSpin
                visible: resultsView.currentIndex >= 0
                from: 1
                to: 9999
                value: dialog.resumePrefill
                implicitHeight: dialog.rowHeight
                implicitWidth: 100
                Layout.alignment: Qt.AlignVCenter
            }

            Item { Layout.fillWidth: true }

            AppButton {
                text: "Cancel"
                fontSize: 16
                radius: 10
                backgroundDefaultColor: Theme.border
                contentItemTextColor: Theme.textPrimary
                leftPadding: 18
                rightPadding: 18
                implicitHeight: dialog.rowHeight
                onClicked: dialog.close()
            }

            AppButton {
                text: "Migrate"
                fontSize: 16
                radius: 10
                enabled: resultsView.currentIndex >= 0
                opacity: enabled ? 1.0 : 0.4
                leftPadding: 18
                rightPadding: 18
                implicitHeight: dialog.rowHeight
                onClicked: {
                    App.migrateShow(dialog.libraryIndex, resultsView.currentIndex, resumeSpin.value)
                    dialog.close()
                }
            }
        }
    }
}
